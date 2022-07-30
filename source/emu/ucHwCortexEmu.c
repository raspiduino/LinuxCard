/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "CortexEmuCpu.h"
#include "timebase.h"
#include "printf.h"
#include "ucHw.h"




void hwError(uint_fast8_t err)
{
	
}

void initHwSuperEarly(void)
{
	#ifdef __MPU_PRESENT
		uint32_t i, nregions = (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
		
		for (i = 0; i < nregions; i++) {
			MPU->RBAR = 0x10 + i;
			MPU->RASR = 0x030f003f;	//all perms, on, 4GB	
		}
		
		MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_HFNMIENA_Msk | MPU_CTRL_PRIVDEFENA_Msk;
	#endif
}


void __attribute__((noinline)) initHw(void)
{
	SCB->VTOR = (uint32_t)0x08000000;
}
