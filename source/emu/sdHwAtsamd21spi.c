/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "samda1e16b.h"
#include <string.h>
#include "timebase.h"
#include "atsamd.h"
#include "printf.h"
#include "sdHw.h"

#define MAX_SPEED			17000000
#define SERCOM_SD			SERCOM3

static uint16_t mTimeoutBytes;
static uint32_t mRdTimeoutTicks, mWrTimeoutTicks;

static const uint8_t mCrcTab7[] = {		//generated from the iterative func :)
	0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f, 0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
	0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26, 0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
	0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d, 0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
	0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14, 0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
	0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b, 0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
	0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42, 0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
	0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69, 0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
	0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70, 0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
	0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e, 0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
	0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67, 0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
	0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c, 0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
	0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55, 0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
	0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a, 0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
	0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03, 0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
	0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28, 0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
	0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31, 0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79,
};

void sdHwNotifyRCA(uint_fast16_t rca)
{
	//nothing
}

void sdHwSetTimeouts(uint_fast16_t timeoutBytes, uint32_t rdTimeoutTicks, uint32_t wrTimeoutTicks)
{
	mTimeoutBytes = timeoutBytes;
	mRdTimeoutTicks = rdTimeoutTicks;
	mWrTimeoutTicks = wrTimeoutTicks;
}

bool sdHwSetBusWidth(bool useFourWide)
{
	return !useFourWide;
}

static void setupSdSercom(uint32_t speed)
{
	SERCOM_SD->SPI.CTRLA.reg = 0;
	while (SERCOM_SD->SPI.SYNCBUSY.bit.ENABLE);
	SERCOM_SD->SPI.CTRLB.bit.RXEN = 1;
	SERCOM_SD->SPI.BAUD.reg = (TICKS_PER_SECOND / 2  + speed - 1) / speed - 1;	//round multiple up so we do not overspeed
	while (SERCOM_SD->SPI.SYNCBUSY.bit.CTRLB);
	SERCOM_SD->SPI.CTRLA.reg = SERCOM_SPI_CTRLA_DIPO(0) | SERCOM_SPI_CTRLA_DOPO(2) | SERCOM_SPI_CTRLA_MODE_SPI_MASTER;
	SERCOM_SD->SPI.CTRLA.reg = SERCOM_SPI_CTRLA_DIPO(0) | SERCOM_SPI_CTRLA_DOPO(2) | SERCOM_SPI_CTRLA_MODE_SPI_MASTER | SERCOM_SPI_CTRLA_ENABLE;
	while (SERCOM_SD->SPI.SYNCBUSY.bit.ENABLE);
}

static void sdHwPrvSetBrg(uint_fast8_t brg)
{
	SERCOM_SD->SPI.CTRLA.bit.ENABLE = 0;
	while(SERCOM_SD->SPI.SYNCBUSY.bit.ENABLE);
	SERCOM_SD->SPI.BAUD.reg = brg;
	SERCOM_SD->SPI.CTRLA.bit.ENABLE = 1;
	while(SERCOM_SD->SPI.SYNCBUSY.bit.ENABLE);
}

void sdHwSetSpeed(uint32_t maxSpeed)
{
	sdHwPrvSetBrg(2);
}

uint32_t sdHwInit(void)
{
	sdHwPrvSetBrg(((TICKS_PER_SECOND / 2) + 400000 - 1) / 400000);
	
	//default timeouts
	sdHwSetTimeouts(10000, 0, 0);
	
	return SD_HW_FLAG_INITED;
}

static uint8_t sdSpiByte(uint_fast8_t val)
{
	Sercom *sercom = SERCOM_SD;
	
	while (!sercom->SPI.INTFLAG.bit.DRE);
	sercom->SPI.DATA.reg = val;
	while (!sercom->SPI.INTFLAG.bit.RXC);
	return sercom->SPI.DATA.reg;
}

void sdHwGiveInitClocks(void)
{
	uint_fast8_t i;
	
	for (i = 0; i < 16; i++)	//give card time to init with CS deasserted
		sdSpiByte(0xff);
}

static void sdChipSelect(void)
{
	PORT->Group[0].OUTCLR.reg = PORT_PA28;
}

void sdHwChipDeselect(void)
{
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	PORT->Group[0].OUTSET.reg = PORT_PA28;
}

static void sdPrvSendCmd(uint8_t cmd, uint32_t param)
{
	uint_fast8_t crc = 0;
	
	crc = mCrcTab7[crc * 2 ^ (0x40 + cmd)];
	crc = mCrcTab7[crc * 2 ^ (uint8_t)(param >> 24)];
	crc = mCrcTab7[crc * 2 ^ (uint8_t)(param >> 16)];
	crc = mCrcTab7[crc * 2 ^ (uint8_t)(param >> 8)];
	crc = mCrcTab7[crc * 2 ^ (uint8_t)param];
	SERCOM_SD->SPI.DATA.reg = 0x40 | cmd;
	while (!SERCOM_SD->SPI.INTFLAG.bit.DRE);
	SERCOM_SD->SPI.DATA.reg = param >> 24;
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	(void)SERCOM_SD->SPI.DATA.reg;	//from cmd
	while (!SERCOM_SD->SPI.INTFLAG.bit.DRE);
	SERCOM_SD->SPI.DATA.reg = param >> 16;
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	(void)SERCOM_SD->SPI.DATA.reg;	//from param.hi
	while (!SERCOM_SD->SPI.INTFLAG.bit.DRE);
	SERCOM_SD->SPI.DATA.reg = param >> 8;
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	(void)SERCOM_SD->SPI.DATA.reg;	//from param.midhi
	while (!SERCOM_SD->SPI.INTFLAG.bit.DRE);
	SERCOM_SD->SPI.DATA.reg = param;
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	(void)SERCOM_SD->SPI.DATA.reg;	//from param.midlo
	while (!SERCOM_SD->SPI.INTFLAG.bit.DRE);
	SERCOM_SD->SPI.DATA.reg = crc * 2 + 1;
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	(void)SERCOM_SD->SPI.DATA.reg;	//from param.lo
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	(void)SERCOM_SD->SPI.DATA.reg;	//from crc
}

enum SdHwCmdResult sdHwCmd(uint_fast8_t cmd, uint32_t param, bool cmdCrcRequired, enum SdHwRespType respTyp, void *respBufOut, enum SdHwDataDir dataDir, uint_fast16_t blockSz, uint32_t numBlocks)
{
	uint8_t *rsp = (uint8_t*)respBufOut;
	uint_fast8_t ret, i = 0;
	
	(void)cmdCrcRequired;
	
	sdChipSelect();
	
	sdPrvSendCmd(cmd, param);
	
	if (cmd == 12)		//do not ask!
		sdSpiByte(0xff);
	
	while ((ret = sdSpiByte(0xff)) == 0xff) {
		
		if (++i == 128) {
			sdHwChipDeselect();
			return SdHwCmdResultRespTimeout;
		}
	}
	
	switch (respTyp) {
		case SdRespTypeNone:
			break;
		
		case SdRespTypeR1:
		case SdRespTypeR1withBusy:
			*rsp++ = ret;
			break;
		
		case SdRespTypeR3:
		case SdRespTypeR7:
			if (ret & FLAG_ILLEGAL_CMD) {
				sdHwChipDeselect();
				return SdCmdInvalid;
			}
			if (ret &~ FLAG_IN_IDLE_MODE) {
				sdHwChipDeselect();
				return SdCmdInternalError;
			}
			for (i = 0; i < 4; i++)
				*rsp++ = sdSpiByte(0xff);
			break;
		
		case SdRespTypeSpiR2:
			if (sdSpiByte(0xff))
				ret |= FLAG_MISC_ERR;
			*rsp = ret;
			if (ret &~ FLAG_IN_IDLE_MODE) {
				sdHwChipDeselect();
				return SdCmdInternalError;
			}
			break;
		
		default:
			sdHwChipDeselect();
			return SdCmdInternalError;
	}
	
	if (dataDir == SdHwDataNone)
		sdHwChipDeselect();
		
	return SdHwCmdResultOK;
}

static bool sdHwPrvDataWait(void)
{
	uint_fast16_t tries, timeoutBytes = mTimeoutBytes;
	uint_fast8_t byte;
	uint64_t time, rt;
	
	for (tries = 0; tries < timeoutBytes; tries++) {
		
		byte = sdSpiByte(0xFF);
		
		if (!(byte & 0xf0))
			return false;
		
		if (byte == 0xfe)
			return true;
	}
	
	time = getTime();
	do {
		byte = sdSpiByte(0xFF);
		
		if (!(byte & 0xf0))
			return false;
		
		if (byte == 0xfe)
			return true;
	
	} while ((rt = getTime()) - time < mRdTimeoutTicks);
	
	pr("read timeout. waited %u ticks, %lu msec. max was %u ticks\n",
		(uint32_t)(rt - time), (uint32_t)(((getTime() - time) * 1000) / TICKS_PER_SECOND), mRdTimeoutTicks);
	
	return false;
}

bool sdHwReadData(uint8_t* data, uint_fast16_t sz)	//length must be even, pointer must be halfword aligned
{
	uint32_t num = sz + 2;
	
	if (!sdHwPrvDataWait())
		return false;

	mDmaDescrsInitial[0].BTCNT.bit.BTCNT = sz;
	mDmaDescrsInitial[0].DSTADDR.bit.DSTADDR = ((uintptr_t)data) + sz;
	asm volatile("":::"memory");
	DMAC->CHID.bit.ID = 0;
	DMAC->CHCTRLA.bit.ENABLE = 1;

	do {
		while (!SERCOM_SD->SPI.INTFLAG.bit.DRE);
		SERCOM_SD->SPI.DATA.reg = 0xff;
	} while (--num);
	
	while (!SERCOM_SD->SPI.INTFLAG.bit.TXC);
	
	asm volatile("":::"memory");
	
	return true;
}

enum SdHwWriteReply sdHwWriteData(const uint8_t *data, uint_fast16_t sz, bool isMultiblock)
{
	uint_fast16_t tries, timeoutBytes = mTimeoutBytes;
	uint_fast8_t byte;
	uint64_t time;
	
	sdSpiByte(isMultiblock ? 0xFC : 0xFE);	//start block
	while (sz--)
		sdSpiByte(*data++);
	//crc
	sdSpiByte(0xff);
	sdSpiByte(0xff);
	
	//wait for a reply
	
	for (tries = 0; tries < timeoutBytes; tries++) {
		
		byte = sdSpiByte(0xFF);
		
		if ((byte & 0x11) == 0x01) {
		
			switch (byte & 0x1f) {
				case 0x05:
					return SdHwWriteAccepted;
				
				case 0x0b:
					return SdHwWriteCrcErr;
				
				case 0x0d:
					return SdHwWriteError;
				
				default:
					return SdHwCommErr;
			}
		}
	}
	
	time = getTime();
	do {
		byte = sdSpiByte(0xFF);
		
		if ((byte & 0x11) == 0x01) {
		
			switch (byte & 0x1f) {
				case 0x05:
					return SdHwWriteAccepted;
				
				case 0x0b:
					return SdHwWriteCrcErr;
				
				case 0x0d:
					return SdHwWriteError;
				
				default:
					return SdHwCommErr;
			}
		}
	
	} while (getTime() - time < mWrTimeoutTicks);
	
	return SdHwTimeout;
}

bool sdHwPrgBusyWait(void)
{
	uint_fast16_t tries, timeoutBytes = mTimeoutBytes;
	uint32_t timeoutTicks = mWrTimeoutTicks;
	uint64_t time;
	
	for (tries = 0; tries < timeoutBytes; tries++) {
		
		if (sdSpiByte(0xFF) == 0xff)
			return true;
	}
	
	time = getTime();
	do {
		if (sdSpiByte(0xFF) == 0xff)
			return true;
	
	} while (getTime() - time < timeoutTicks);
	
	return false;
}

void sdHwRxRawBytes(void *dstP /* can be NULL*/, uint_fast16_t numBytes)
{
	uint8_t *dst = (uint8_t*)dstP;
	
	while (numBytes--) {
		
		uint_fast8_t val = sdSpiByte(0xff);
		
		if (dst)
			*dst++ = val;
	}
}

bool sdHwMultiBlockWriteSignalEnd(void)
{
	//stoptran token
	(void)sdSpiByte(0xFD);
	
	return true;
}

bool sdHwMultiBlockReadSignalEnd(void)
{
	//nothing
	
	return true;
}


