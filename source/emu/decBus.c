/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include "decBus.h"
#include "mem.h"

static uint32_t mBusErrorAddr;
static uint16_t mBusCsr;


void decReportBusErrorAddr(uint32_t pa)
{
	mBusErrorAddr = pa;
}


static bool accessDecBusErrorReporter(uint32_t pa, uint_fast8_t size, bool write, void* buf, void* userData)
{
	(void)userData;
	pa &= 0x00ffffff;
	
	if (size == 4 && !pa && !write) {
		*(uint32_t*)buf = mBusErrorAddr;
		return true;
	}
	return false;
}

static bool accessDecWriteBuffer(uint32_t pa, uint_fast8_t size, bool write, void* buf, void* userData)
{
	(void)userData;
	pa &= 0x00ffffff;
	
	if (size != 2 || pa)
		return false;
	
	if (write)
		mBusCsr = *(uint16_t*)buf;
	else
		*(uint16_t*)buf = mBusCsr;
	
	return true;
}

bool decBusInit(void)
{
	return memRegionAdd(0x17000000, 0x01000000, accessDecBusErrorReporter, (void*)0) && 
			memRegionAdd(0x1e000000, 0x01000000, accessDecWriteBuffer, (void*)0);
}