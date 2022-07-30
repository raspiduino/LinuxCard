/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#if defined(CPU_STM32G0)
	#include "stm32g031xx.h"
#elif defined(CPU_SAMD)
	#include "samda1e16b.h"
#elif defined(CPU_CORTEXEMU)
	#include "CortexEmuCpu.h"
#endif

#include <string.h>
#include "../hypercall.h"
#include "timebase.h"
#include "spiRam.h"
#include "printf.h"
#include "decBus.h"
#include "ds1287.h"
#include "usart.h"
#include "ucHw.h"
#include "sdHw.h"
#include "dz11.h"
#include "soc.h"
#include "mem.h"
#include "sd.h"




static uint8_t mDiskBuf[SD_BLOCK_SIZE];

static const uint8_t gRom[] = 
{
	#include "loader.inc"
};


void delayMsec(uint32_t msec)
{
	uint64_t till = getTime() + (uint64_t)msec * (TICKS_PER_SECOND / 1000);
	
	while (getTime() < till);
}


void prPutchar(char chr)
{
	#ifdef SUPPORT_DEBUG_PRINTF
	
		#if (ZWT_ADDR & 3)		//byte
		
			*(volatile uint8_t*)ZWT_ADDR = chr;
					
		#else
			volatile uint32_t *addr = (volatile uint32_t*)ZWT_ADDR;
			
			while(addr[0] & 0x80000000ul);
			addr[0] = 0x80000000ul | (uint8_t)chr;
		#endif
		
	#endif
	
	#if 0
		if (chr == '\n')
			usartTx('\r');
			
		usartTx(chr);
	#endif
}

void fastpathReport(uint32_t val, uint32_t addr)
{
	pr("fastpath repot: [%08x] = %08x\n", addr, val);
}

void dz11charPut(uint_fast8_t line, uint_fast8_t chr)
{
	(void)chr;
	
	#ifdef MULTICHANNEL_UART
		
		usartTxEx(line, chr);
		
	#else
	
		if (line == 3)
			usartTx(chr);
	
	#endif
	
	if (line == 3) {
		/*	--	for benchmarking
		static uint8_t state = 0;
		
		switch (state) {
			case 0:
				if (chr == 'S')
					state = 1;
				break;
			case 1:
				state = (chr == 'C') ? 2 : 0;
				break;
			
			case 2:
				state = (chr == 'S') ? 3 : 1;
				break;
		
			case 3:
				state = (chr == 'I') ? 4 : 0;
				break;
			
			case 4:
				;
				uint64_t time = getTime();
				pr("\ntook 0x%08x%08x\n", (uint32_t)(time >> 32), (uint32_t)time);
				state = 5;
				break;
		}
		
		//*/
	//	prPutchar(chr);
	}
}

#ifdef SUPPORT_MULTIBLOCK_ACCESSES_TO_SD		//worked on previous boards, will not anymore since we now share an SPI bus between RAM and SD
	
	static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
	{
		static uint32_t mCurSec = 0xffffffff;
		static uint8_t mCurOp = 0xff;
		uint_fast8_t nRetries;
		
		switch (op) {
			case MASS_STORE_OP_GET_SZ:
				 *(uint32_t*)buf = sdGetNumSecs();
				 return true;
			
			case MASS_STORE_OP_READ:
			//	return sdSecRead(sector, buf);
				
				if (mCurOp == MASS_STORE_OP_READ && mCurSec == sector){
					
					if (sdReadNext(buf)) {
						mCurSec++;
						return true;
					}
					else {
						
						pr("failed to read next\n");
						sdReportLastError();
						mCurOp = 0xff;
						(void)sdReadStop();
						return false;
					}
				}
				
				if (mCurOp == MASS_STORE_OP_WRITE) {
					
					mCurOp = 0xff;
					if (!sdWriteStop()) {
						
						pr("failed to stop a write\n");
						sdReportLastError();
						return false;
					}
				}
				else if (mCurOp == MASS_STORE_OP_READ) {
					
					mCurOp = 0xff;
					if (!sdReadStop()) {
						
						pr("failed to stop a read\n");
						sdReportLastError();
						return false;
					}
				}
				
				for (nRetries = 0; nRetries < 2; nRetries++) {
				
					if (!sdReadStart(sector, 0)) {
						
						(void)sdReadStop();
						pr("failed to start a read\n");
						sdReportLastError();
						continue;
					}
					if (!sdReadNext(buf)) {
						
						(void)sdReadStop();
						pr("failed to read first sec\n");
						sdReportLastError();
						continue;
					}
					mCurOp = MASS_STORE_OP_READ;
					mCurSec = sector + 1;
					return true;
				}
				pr("giving up\n");
				return false;
				
			case MASS_STORE_OP_WRITE:
			
			//	return sdSecWrite(sector, buf);
			
				if (mCurOp == MASS_STORE_OP_WRITE && mCurSec == sector){
					
					if (sdWriteNext(buf)) {
						mCurSec++;
						return true;
					}
					else {
						
						pr("failed to write next\n");
						sdReportLastError();
						mCurOp = 0xff;
						(void)sdWriteStop();
						return false;
					}
				}
				
				if (mCurOp == MASS_STORE_OP_WRITE) {
					
					mCurOp = 0xff;
					if (!sdWriteStop()) {
						
						pr("failed to stop a write\n");
						sdReportLastError();
						return false;
					}
				}
				else if (mCurOp == MASS_STORE_OP_READ) {
					
					mCurOp = 0xff;
					if (!sdReadStop()) {
						
						pr("failed to stop a read\n");
						sdReportLastError();
						return false;
					}
				}
				
				///aaa
				mCurOp = 0xff;
				return sdSecWrite(sector, buf);
				///aaa
				
				
				for (nRetries = 0; nRetries < 2; nRetries++) {
				
					if (!sdWriteStart(sector, 0)) {
						
						pr("failed to start a write\n");
						sdReportLastError();
						(void)sdWriteStop();
						continue;
					}
					if (!sdWriteNext(buf)) {
						
						(void)sdWriteStop();
						pr("failed to write first sec\n");
						sdReportLastError();
						continue;
					}
					mCurOp = MASS_STORE_OP_WRITE;
					mCurSec = sector + 1;
					return true;
				}
				pr("giving up\n");
				return false;
		}
		return false;
	}
	
#else

	static bool massStorageAccess(uint8_t op, uint32_t sector, void *buf)
	{
		uint_fast8_t nRetries;
		
		switch (op) {
			case MASS_STORE_OP_GET_SZ:
				 *(uint32_t*)buf = sdGetNumSecs();
				 return true;
			
			case MASS_STORE_OP_READ:
				return sdSecRead(sector, buf);
				
			case MASS_STORE_OP_WRITE:
				return sdSecWrite(sector, buf);
		}
		return false;
	}

#endif


static bool accessRom(uint32_t pa, uint_fast8_t size, bool write, void* buf, void* userData)
{
	const uint8_t *mem = gRom;

	(void)userData;
	pa -= (ROM_BASE & 0x1FFFFFFFUL);
	
	if (write)
		return false;
	else if (size == 4)
		*(uint32_t*)buf = *(uint32_t*)(mem + pa);
	else if (size == 1)
		*(uint8_t*)buf = mem[pa];
	else if (size == 2)
		*(uint16_t*)buf = *(uint16_t*)(mem + pa);
	else
		memcpy(buf, mem + pa, size);
	
	return true;
}

static bool accessRam(uint32_t pa, uint_fast8_t size, bool write, void* buf, void* userData)
{
	(void)userData;
	
	if (write)
		spiRamWrite(pa, buf, size);
	else
		spiRamRead(pa, buf, size);

	return true;
}



bool cpuExtHypercall(void)	//call type in $at, params in $a0..$a3, return in $v0, if any
{
	uint32_t hyperNum = cpuGetRegExternal(MIPS_REG_AT), t,  ramMapNumBits, ramMapEachBitSz;
	const uint8_t *mRamMap;
	uint_fast16_t ofst;
	uint32_t blk, pa;
	uint8_t chr;
	bool ret;

	switch (hyperNum) {
		case H_GET_MEM_MAP:
			//a0 is byte index index if >= 2, [0] is nBits, [1] is eachBitSz
			mRamMap = spiRamGetMap(&ramMapNumBits, &ramMapEachBitSz);
			
			switch (pa = cpuGetRegExternal(MIPS_REG_A0)) {
				case 0:
					pa = ramMapNumBits;
					break;
				
				case 1:
					pa = ramMapEachBitSz;
					break;
				
				default:
					pa = mRamMap[pa - 2];
					break;
			}
			cpuSetRegExternal(MIPS_REG_V0, pa);
			break;
		
		case H_CONSOLE_WRITE:
			chr = cpuGetRegExternal(MIPS_REG_A0);
			if (chr == '\n') {
				prPutchar('\r');
				usartTx('\r');
			}
			usartTx(chr);
			prPutchar(chr);
			break;
		
		case H_STOR_GET_SZ:
			if (!massStorageAccess(MASS_STORE_OP_GET_SZ, 0, &t))
				return false;
			cpuSetRegExternal(MIPS_REG_V0, t);
			break;
		
		case H_STOR_READ:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			ret = massStorageAccess(MASS_STORE_OP_READ, blk, mDiskBuf);
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_WR_SZ)
				spiRamWrite(pa + ofst, mDiskBuf + ofst, OPTIMAL_RAM_WR_SZ);
			cpuSetRegExternal(MIPS_REG_V0, ret);
			if (!ret)
				pr(" rd_block(%u, 0x%08x) -> %d\n", blk, pa, ret);
			break;
		
		case H_STOR_WRITE:
			blk = cpuGetRegExternal(MIPS_REG_A0);
			pa = cpuGetRegExternal(MIPS_REG_A1);
			for (ofst = 0; ofst < SD_BLOCK_SIZE; ofst += OPTIMAL_RAM_RD_SZ)
				spiRamRead(pa + ofst, mDiskBuf + ofst, OPTIMAL_RAM_RD_SZ);
			ret = massStorageAccess(MASS_STORE_OP_WRITE, blk, mDiskBuf);
			cpuSetRegExternal(MIPS_REG_V0, ret);
			if (!ret) {
				pr(" wr_block(%u, 0x%08x) -> %d\n", blk, pa, ret);
				while(1);
			}
			break;
		
		case H_TERM:
			pr("termination requested\n");
			while(1);
			break;

		default:
			pr("hypercall %u @ 0x%08x\n", hyperNum, cpuGetRegExternal(MIPS_EXT_REG_PC));
			return false;
	}
	return true;
}

void cycleReport(uint32_t instr, uint32_t addr)
{
	static bool mReportCy = true;
	uint_fast8_t i;
	
	if (mReportCy){
	
		//proper save/restore state not beig done. some regs will not read properly, like pc
		pr("[%08x]=%08x {", addr, instr);
		
		for (i = 0; i < 32; i++) {
			if (!(i & 7))
				pr("%u: ", i);
			pr(" %08x", cpuGetRegExternal(i));
		}
		pr("}\n");
	}
}

void usartExtRx(uint8_t val)
{
	dz11charRx(3, val);
}

void reportInvalid(uint32_t pc, uint32_t instr)
{
	//inval used for emulation
	if (instr == 0x0000000F)
		return;
	
	//rdhwr
	if (instr == 0x7C03E83B)
		return;
	
	pr("INVAL [%08x] = %08x\n", pc, instr);
}

static void showBuf(uint_fast8_t idx, const uint8_t *buf)
{
	uint_fast16_t i;
	
	pr("BUFFER %u", idx);
	
	for (i = 0; i < 512; i++) {
		
		if (!(i & 15))
			pr("\n  %03x  ", i);
		pr(" %02x", *buf++);
	}
	pr("\n");
}

int main(void)
{
	volatile uint32_t t = 0;
	
	pr("running!!\n");
	
	initHwSuperEarly();
	
	for (t = 0; t < 0x80000; t += 2)
		t--;
	
	
	initHw();
	timebaseInit();
	usartInit();
	
	pr("ready, time is 0x%016llx\n", getTime());
	pr("ready, time is 0x%016llx\n", getTime());

	if (!sdCardInit(mDiskBuf)) {
		
		hwError(2);
		pr("SD card init fail\n");
	}
	else if (!spiRamInit()) {
		
		hwError(3);
		pr("SPI ram init issue\n");
	}
	else {
		
		uint32_t ramMapNumBits, ramMapEachBitSz;
		
		spiRamGetMap(&ramMapNumBits, &ramMapEachBitSz);
		
		if (!memRegionAdd(RAM_BASE, ramMapNumBits * ramMapEachBitSz, accessRam, NULL))
			pr("failed to add RAM\n");
		else if (!memRegionAdd(ROM_BASE & 0x1FFFFFFFUL, sizeof(gRom), accessRom, NULL))
			pr("failed to add ROM\n");
		else if (!decBusInit())
			pr("failed init DEC BUS\n");
		else if (!dz11init())
			pr("failed init DZ11\n");
		else if (!ds1287init())
			pr("failed init DS1287\n");
		else {
			
			cpuInit();
			cpuCycle();
			while(1);
		}
		hwError(4);
	}
	
	while(1);
}

void __attribute__((used)) report_hard_fault(uint32_t* regs, uint32_t ret_lr, uint32_t *user_sp)
{
	uint32_t *push = (ret_lr == 0xFFFFFFFD) ? user_sp : (regs + 8), *sp = push + 8;
	unsigned i;
	
	pr("============ HARD FAULT ============\n");
	pr("R0  = 0x%08X    R8  = 0x%08X\n", (unsigned)push[0], (unsigned)regs[0]);
	pr("R1  = 0x%08X    R9  = 0x%08X\n", (unsigned)push[1], (unsigned)regs[1]);
	pr("R2  = 0x%08X    R10 = 0x%08X\n", (unsigned)push[2], (unsigned)regs[2]);
	pr("R3  = 0x%08X    R11 = 0x%08X\n", (unsigned)push[3], (unsigned)regs[3]);
	pr("R4  = 0x%08X    R12 = 0x%08X\n", (unsigned)regs[4], (unsigned)push[4]);
	pr("R5  = 0x%08X    SP  = 0x%08X\n", (unsigned)regs[5], (unsigned)sp);
	pr("R6  = 0x%08X    LR  = 0x%08X\n", (unsigned)regs[6], (unsigned)push[5]);
	pr("R7  = 0x%08X    PC  = 0x%08X\n", (unsigned)regs[7], (unsigned)push[6]);
	pr("RA  = 0x%08X    SR  = 0x%08X\n", (unsigned)ret_lr,  (unsigned)push[7]);
	pr("SHCSR = 0x%08X\n", SCB->SHCSR);
	#if defined(CPU_CM7)
    	pr("CFSR  = 0x%08X    HFSR  = 0x%08X\n", SCB->CFSR, SCB->HFSR);
    	pr("MMFAR = 0x%08X    BFAR  = 0x%08X\n", SCB->MMFAR, SCB->BFAR);
	#endif
    
	pr("WORDS @ SP: \n");
	
	for (i = 0; i < 32; i++)
		pr("[sp, #0x%03x = 0x%08x] = 0x%08x\n", i * 4, (unsigned)&sp[i], (unsigned)sp[i]);
	
	
	pr("\n\n");
	
	while(1);
}

void __attribute__((noreturn, naked, noinline)) HardFault_Handler(void)
{
	asm volatile(
			"push {r4-r7}				\n\t"
			"mov  r0, r8				\n\t"
			"mov  r1, r9				\n\t"
			"mov  r2, r10				\n\t"
			"mov  r3, r11				\n\t"
			"push {r0-r3}				\n\t"
			"mov  r0, sp				\n\t"
			"mov  r1, lr				\n\t"
			"mrs  r2, PSP				\n\t"
			"bl   report_hard_fault		\n\t"
			:::"memory");
}
