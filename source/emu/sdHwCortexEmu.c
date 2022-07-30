/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "CortexEmuCpu.h"
#include <string.h>
#include "timebase.h"
#include "printf.h"
#include "sdHw.h"


#define MODE_IDLE				0
#define MODE_SPECIAL_READ		1
#define MODE_READ				2
#define MODE_READ_MANY			3
#define MODE_WRITE				4
#define MODE_WRITE_MANY			5

static uint16_t mSpecialReadSz;
static uint8_t mSpecialReadBuf[64];
static uint8_t mCurMode = MODE_IDLE;
static uint32_t mCurSec;
static bool isACmd;



static uint32_t __attribute__((naked, noinline)) cmPrvSmc(uint32_t r0, ...)
{
	uint32_t ret;
	
	asm volatile(	".hword 0xf7f0	\n"
					".hword 0x8000	\n"
					"bx lr			\n"
					"mov %0, r0		\n\t"	//make gcc happy
					:"=r"(ret)::"memory"
	);
	
	return ret;
}

static bool cmPrvSdPresent(void)
{
	return !!cmPrvSmc(0);
}

static uint32_t cmPrvGetCardSectors(void)
{
	return cmPrvSmc(1);
}

static void cmPrvRead(void *dst, uint32_t sec)
{
	cmPrvSmc(2, sec, dst);
}

static void cmPrvWrite(const void *src, uint32_t sec)
{
	cmPrvSmc(3, sec, src);
}

void sdHwNotifyRCA(uint_fast16_t rca)
{
	//nothing
}

void sdHwSetTimeouts(uint_fast16_t timeoutBytes, uint32_t rdTimeoutTicks, uint32_t wrTimeoutTicks)
{
	//nothing
}

bool sdHwSetBusWidth(bool useFourWide)
{
	return !useFourWide;
}

void sdHwSetSpeed(uint32_t maxSpeed)
{
	//nothing
}

uint32_t sdHwInit(void)
{
	return cmPrvSdPresent() ? SD_HW_FLAG_INITED : 0;
}

void sdHwGiveInitClocks(void)
{
	//nothing
}

void sdHwChipDeselect(void)
{
	//nothing
}

//not even close to a proper SD state machine, just enough to fool "sd.c"
enum SdHwCmdResult sdHwCmd(uint_fast8_t cmd, uint32_t param, bool cmdCrcRequired, enum SdHwRespType respTyp, void *respBufOut, enum SdHwDataDir dataDir, uint_fast16_t blockSz, uint32_t numBlocks)
{
	switch (cmd) {
		
		case 0:
			((uint8_t*)respBufOut)[0] = FLAG_IN_IDLE_MODE;
			return SdHwCmdResultOK;
		
		case 9:	//get CSD
			mCurMode = MODE_SPECIAL_READ;
			memset(mSpecialReadBuf, 0, 16);
			mSpecialReadBuf[0] = 0x40;	//CSD ver = 2 (encoded as 0b01)
			mSpecialReadBuf[7] = ((cmPrvGetCardSectors() / 1024 - 1) >> 16) & 0x3f;
			mSpecialReadBuf[8] = ((cmPrvGetCardSectors() / 1024 - 1) >> 8) & 0xff;
			mSpecialReadBuf[9] = ((cmPrvGetCardSectors() / 1024 - 1) >> 0) & 0xff;
			mSpecialReadSz = 16;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		case 10:	//get CID
			mCurMode = MODE_SPECIAL_READ;
			memset(mSpecialReadBuf, 0, 16);
			mSpecialReadBuf[0] = 'd';	//mid
			mSpecialReadBuf[1] = 0x34;	//oid.hi
			mSpecialReadBuf[2] = 0x12;	//oid.lo
			mSpecialReadBuf[9] = 0x78;	//snum.hi
			mSpecialReadBuf[10] = 0x56;	//snum.midhi
			mSpecialReadBuf[11] = 0x34;	//snum.midlo
			mSpecialReadBuf[12] = 0x12;	//snum.lo
			mSpecialReadSz = 16;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		case 51:	//read SCR
			if (!isACmd)
				return SdHwCmdResultRespTimeout;
			mCurMode = MODE_SPECIAL_READ;
			memset(mSpecialReadBuf, 0, 8);
			mSpecialReadBuf[1] = 1;		//supports 1 bit mode
			mSpecialReadSz = 8;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		case 13:	//read sd status
			if (!isACmd)
				return SdHwCmdResultRespTimeout;
			mCurMode = MODE_SPECIAL_READ;
			memset(mSpecialReadBuf, 0, 64);
			mSpecialReadSz = 64;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		case 12:
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			if (mCurMode == MODE_READ_MANY || mCurMode == MODE_WRITE_MANY)
				return SdHwCmdResultOK;
			return SdHwCmdResultRespTimeout;
		
		case 16:
			return param == 512 ? SdHwCmdResultOK : SdHwCmdResultRespTimeout;
		
		case 55:
			isACmd = true;
		case 59:
		case 41:
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		case 58:
			((uint8_t*)respBufOut)[0] = 0x40;	//HC
			return SdHwCmdResultOK;
		
		case 8:
			((uint8_t*)respBufOut)[3] = 0xaa;
			return SdHwCmdResultOK;
		
		case 17:
			mCurMode = MODE_READ;
			mCurSec = param;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
			
		case 18:
			mCurMode = MODE_READ_MANY;
			mCurSec = param;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		case 24:
			mCurMode = MODE_WRITE;
			mCurSec = param;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
			
		case 25:
			mCurMode = MODE_WRITE_MANY;
			mCurSec = param;
			((uint8_t*)respBufOut)[0] = 0;	//no errors
			return SdHwCmdResultOK;
		
		default:
			return SdHwCmdResultRespTimeout;
	}
}

bool sdHwReadData(uint8_t* data, uint_fast16_t sz)	//length must be even, pointer must be halfword aligned
{
	if (mCurMode == MODE_READ || mCurMode == MODE_READ_MANY) {
		
		if (sz == 512) {
			cmPrvRead(data, mCurSec++);
			if (mCurMode == MODE_READ)
				mCurMode = MODE_IDLE;
			
			return true;
		}
	}
	else if (mCurMode == MODE_SPECIAL_READ) {
		
		if (sz == mSpecialReadSz) {
			
			memcpy(data, mSpecialReadBuf, sz);
			mCurMode = MODE_IDLE;
			return true;
		}
	}
	
	return false;
}

enum SdHwWriteReply sdHwWriteData(const uint8_t *data, uint_fast16_t sz, bool isMultiblock)
{
	
	if (mCurMode == MODE_WRITE || mCurMode == MODE_WRITE_MANY) {
		
		if (sz == 512) {
			cmPrvWrite(data, mCurSec++);
			if (mCurMode == MODE_WRITE)
				mCurMode = MODE_IDLE;
			
			return SdHwWriteAccepted;
		}
	}
	
	return SdHwTimeout;
}

bool sdHwPrgBusyWait(void)
{
	return true;
}

void sdHwRxRawBytes(void *dstP /* can be NULL*/, uint_fast16_t numBytes)
{
	//nothing
}

bool sdHwMultiBlockWriteSignalEnd(void)
{
	//stoptran token
	
	return true;
}

bool sdHwMultiBlockReadSignalEnd(void)
{
	//nothing
	
	return true;
}


