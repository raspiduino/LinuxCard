#include  <stdint.h>
#define VEC_(nm, pfx)	void nm##pfx(void) __attribute__ ((weak, alias ("IntDefaultHandler"))) 
#define VEC(nm)		VEC_(nm, _Handler)
#define VECI(nm)	VEC_(nm, _IRQHandler)


void __attribute__ ((weak)) IntDefaultHandler(void);
VEC(NMI);
VEC(HardFault);
VEC(SVC);
VEC(PendSV);
VEC(SysTick);

VECI(PM);
VECI(SYSCTRL);
VECI(WDT);
VECI(RTC);
VECI(EIC);
VECI(NVMCTRL);
VECI(DMAC);
VECI(USB);
VECI(EVSYS);
VECI(SERCOM0);
VECI(SERCOM1);
VECI(SERCOM2);
VECI(SERCOM3);
VECI(SERCOM4);
VECI(SERCOM5);
VECI(TCC0);
VECI(TCC1);
VECI(TCC2);
VECI(TC3);
VECI(TC4);
VECI(TC5);
VECI(TC6);
VECI(TC7);
VECI(ADC);
VECI(AC);
VECI(DAC);
VECI(PTC);
VECI(I2S);





//main must exist
extern int main(void);

//stack top (provided by linker)
extern void __stack_top();
extern uint32_t __data_data[];
extern uint32_t __data_start[];
extern uint32_t __data_end[];
extern uint32_t __bss_start[];
extern uint32_t __bss_end[];




void __attribute__((noreturn)) IntDefaultHandler(void)
{
	while (1) {		
		asm("wfi":::"memory");
	}
}

void __attribute__((noreturn)) ResetISR(void)
{
	uint32_t *dst, *src, *end;

	//copy data
	dst = __data_start;
	src = __data_data;
	end = __data_end;
	while(dst != end)
		*dst++ = *src++;

	//init bss
	dst = __bss_start;
	end = __bss_end;
	while(dst != end)
		*dst++ = 0;

	main();

//if main returns => bad
	while(1);
}


__attribute__ ((section(".vectors"))) void (*const __VECTORS[]) (void) =
{
	&__stack_top,
	ResetISR,
	NMI_Handler,
	HardFault_Handler,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	SVC_Handler,		// SVCall handler
	0,					// Reserved
	0,					// Reserved
	PendSV_Handler,		// The PendSV handler
	SysTick_Handler,	// The SysTick handler
	
	// Chip Level - ATSAMD21
	PM_IRQHandler,
	SYSCTRL_IRQHandler,
	WDT_IRQHandler,
	RTC_IRQHandler,
	EIC_IRQHandler,
	NVMCTRL_IRQHandler,
	DMAC_IRQHandler,
	USB_IRQHandler,
	EVSYS_IRQHandler,
	SERCOM0_IRQHandler,
	SERCOM1_IRQHandler,
	SERCOM2_IRQHandler,
	SERCOM3_IRQHandler,
	SERCOM4_IRQHandler,
	SERCOM5_IRQHandler,
	TCC0_IRQHandler,
	TCC1_IRQHandler,
	TCC2_IRQHandler,
	TC3_IRQHandler,
	TC4_IRQHandler,
	TC5_IRQHandler,
	TC6_IRQHandler,
	TC7_IRQHandler,
	ADC_IRQHandler,
	AC_IRQHandler,
	DAC_IRQHandler,
	PTC_IRQHandler,
	I2S_IRQHandler,
};



