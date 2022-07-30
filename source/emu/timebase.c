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


#include "timebase.h"

static uint64_t mTicks = 0;

#ifndef TICKS_PER_IRQ
	#define TICKS_PER_IRQ	0x01000000
#endif

void timebaseInit(void)
{
	//setup SysTick
	SysTick->CTRL = 0;
	SysTick->LOAD = TICKS_PER_IRQ - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
	
	NVIC_SetPriority(SysTick_IRQn, 1);
}

void SysTick_Handler(void)
{
	mTicks += TICKS_PER_IRQ;
}

uint64_t getTime(void)
{
	uint64_t hi1, hi2;
	uint32_t lo;
	
	do {		//this construction brought to you by Cortex-M7
		hi1 = mTicks;
		asm volatile("":::"memory");
		hi2 = mTicks;
		asm volatile("":::"memory");
		lo = SysTick->VAL;
		asm volatile("":::"memory");
	} while (hi1 != hi2 || hi1 != mTicks);
	
	return hi1 + (TICKS_PER_IRQ - lo);
}