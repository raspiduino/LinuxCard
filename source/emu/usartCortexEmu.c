/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "usbPublic.h"
#include "timebase.h"
#include "usbDev.h"
#include "printf.h"
#include "usart.h"
#include "dz11.h"



void usartSetBuadrate(uint32_t baud)
{
	
}

void usartInit(void)
{
	
}

void usartTx(uint8_t ch)
{
	static uint8_t state = 0;
	
	*(volatile uint8_t*)ZWT_ADDR = ch;
	
	switch (state) {
		case 0:	state = (ch == '/') ? 1 : 0;	break;
		case 1:	state = (ch == ' ') ? 2 : 0;	break;
		case 2:	state = (ch == '#') ? 3 : 0;	break;
		case 3:
			asm volatile(
					"	.hword 0xf7f2 					\n\t"	//smc #2
					"	.hword 0x8000					\n\t"
			);
			break;
	}
}
