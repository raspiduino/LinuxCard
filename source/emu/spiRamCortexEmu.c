/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "samda1e16b.h"
#include <string.h>
#include <stddef.h>
#include "printf.h"
#include "atsamd.h"
#include "spiRam.h"

#define RAM_AMOUNT			16		//in MB
#define RAM_BASE			0x70000000




bool __attribute__((noinline)) spiRamInit(void)
{
	return true;
}

const uint8_t* spiRamGetMap(uint32_t *nBitsP, uint32_t *eachBitSzP)
{
	static uint8_t mMap = 0xff;
	
	*eachBitSzP = RAM_AMOUNT << 17;
	*nBitsP = 8;
	
	return &mMap;
}


void spiRamRead(uint32_t addr, void *dataP, uint_fast16_t sz)	//assume not called with zero
{
	memcpy(dataP, (const void*)(RAM_BASE + addr), sz);
}

void spiRamWrite(uint32_t addr, const void *dataP, uint_fast16_t sz)
{
	memcpy((void*)(RAM_BASE + addr), dataP, sz);
}
