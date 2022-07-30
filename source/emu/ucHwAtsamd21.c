/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "samda1e16b.h"
#include "timebase.h"
#include "printf.h"
#include "atsamd.h"
#include "ucHw.h"

//For pin PA24 and PA25, the GPIO pull-up and pull-down must be disabled
//before enabling alternative functions on them.


/*
	-- second atsamd board --
	nCS for RAMS:		14
	nCS for SD:			28
	CARDDET:			15
	
			RAM0				RAM1				RAM2				RAM3/SD
	MOSI	4(D=SERCOM0[0])		8(D=SERCOM2[0])		16(C=SERCOM1[0])	22(C=SERCOM3[0])
	MISO	6(D=SERCOM0[2])		10(D=SERCOM2[2])	18(C=SERCOM1[2])	19(D=SERCOM3[3])
	CLK		5(D=SERCOM0[1])		9(D=SERCOM2[1])		17(C=SERCOM1[1])	23(C=SERCOM3[1])
				DOPO=0				DOPO=0				DOPO=0				DOPO=0
				DIPO=2				DIPO=2				DIPO=2				DIPO=3
*/

static uint8_t mDmaRxDump;
volatile DmacDescriptor mDmaDescrsWriteback[NUM_DMA_CHANNELS_USED];
volatile DmacDescriptor mDmaDescrsSecond[NUM_DMA_CHANNELS_USED] = {
	[0] = {
		.BTCTRL.reg = DMAC_BTCTRL_VALID | DMAC_BTCTRL_BEATSIZE_BYTE | DMAC_BTCTRL_BLOCKACT_NOACT | DMAC_BTCTRL_EVOSEL_DISABLE,
		.BTCNT.bit.BTCNT = 2,	//crc is always 2 bytes
		.SRCADDR.bit.SRCADDR = (uintptr_t)&SERCOM3->SPI.DATA,
		.DSTADDR.bit.DSTADDR = (uintptr_t)&mDmaRxDump,
	},
};
volatile DmacDescriptor mDmaDescrsInitial[NUM_DMA_CHANNELS_USED] = {
	[0] = {	//sd
		.BTCTRL.reg = DMAC_BTCTRL_VALID | DMAC_BTCTRL_DSTINC | DMAC_BTCTRL_BEATSIZE_BYTE | DMAC_BTCTRL_BLOCKACT_NOACT | DMAC_BTCTRL_EVOSEL_DISABLE,
		.SRCADDR.bit.SRCADDR = (uintptr_t)&SERCOM3->SPI.DATA,
		.DESCADDR.bit.DESCADDR = (uintptr_t)&mDmaDescrsSecond[0],
	},
};

static const uint8_t mDmaTriggers[NUM_DMA_CHANNELS_USED] = {
	[0] = 7,	//SERCOM3.rx
};

void initHwSuperEarly(void)
{
	//this chip starts super slow (1M) speed it up to 8M right away
	SYSCTRL->OSC8M.bit.PRESC = 0;
}

void hwError(uint_fast8_t err)
{
	uint64_t t;
	
	while (1) {
		
		uint_fast8_t i;
		
		for (i = 0; i < err; i++) {
			t = getTime();
			while (getTime() - t < TICKS_PER_SECOND / 2);
			PORT->Group[0].OUTCLR.reg = PORT_PA28;
			while (getTime() - t < TICKS_PER_SECOND);
			PORT->Group[0].OUTSET.reg = PORT_PA28;
		}
		
		t = getTime();
		while (getTime() - t < TICKS_PER_SECOND / 2);
	}
}

static void loadCalibrationValues(void)
{
	#define FUSENAME(prenm, postnm, suffix)		prenm ## FUSES_ ## postnm ## _ ## suffix
	#define FUSEVAL(prenm, postnm)				(((*(uint32_t*)(FUSENAME(prenm, postnm, ADDR))) & (FUSENAME(prenm, postnm, Msk))) >> (FUSENAME(prenm, postnm, Pos)))
	
	ADC->CALIB.bit.BIAS_CAL = FUSEVAL(ADC_, BIASCAL);
	ADC->CALIB.bit.LINEARITY_CAL = FUSEVAL(ADC_, LINEARITY_0) + (FUSEVAL(ADC_, LINEARITY_1) << 5);
	SYSCTRL->OSC32K.bit.CALIB = FUSEVAL(, OSC32K_CAL);
	USB->DEVICE.PADCAL.bit.TRANSN = FUSEVAL(USB_, TRANSN);
	USB->DEVICE.PADCAL.bit.TRANSP = FUSEVAL(USB_, TRANSP);
	USB->DEVICE.PADCAL.bit.TRIM = FUSEVAL(USB_, TRIM);
	
	SYSCTRL->DFLLCTRL.bit.ONDEMAND = 0;			//Workaround for errata 9905
	while (!SYSCTRL->PCLKSR.bit.DFLLRDY);		//wait for the DFLL clock to be ready
	
	SYSCTRL->DFLLVAL.reg = SYSCTRL_DFLLVAL_COARSE(FUSEVAL(, DFLL48M_COARSE_CAL)) | SYSCTRL_DFLLVAL_FINE(FUSEVAL(, DFLL48M_FINE_CAL));
	#undef FUSEVAL
	#undef FUSENAME
}

static void setupClocking(void)
{
	uint32_t divByM16 = (16ULL * TICKS_PER_SECOND + 500000) / 1000000;		//run PLL at Fcpu
	uint32_t maxWait;
	
	//set flash wait states for expected speed
	NVMCTRL->CTRLB.bit.RWS = (TICKS_PER_SECOND <= 25000000) ? 0 : ( 
								(TICKS_PER_SECOND <= 50000000) ? 1 : (
									(TICKS_PER_SECOND <= 75000000) ? 2 : (
										(TICKS_PER_SECOND <= 100000000) ? 3 : 4
									)
								)
							);
	
	
	//i had no luck starting OSC32K despite wasting one fucking day on it, so fuckit, we'll use OSC32KULP which is always on
	//oh, and fuck you too, Atmel!
	//this code should do it, but the bit never goes high
	/*
		SYSCTRL->OSC32K.bit.EN32K = 1;
		SYSCTRL->OSC32K.bit.ENABLE = 0;
		while (!SYSCTRL->PCLKSR.bit.OSC32KRDY);
	*/
	//clock generator 1 outputs OSC8M / 8 = 1MHz
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(1) | GCLK_GENDIV_DIV(8);
	GCLK->GENCTRL.reg =	GCLK_GENCTRL_ID(1) |
						GCLK_GENCTRL_SRC_OSC8M |
						GCLK_GENCTRL_GENEN;
	
	//clock generator 2 outputs ULP32K / 1 = 32KHz
	GCLK->GENCTRL.reg =	GCLK_GENCTRL_ID(2) |
						GCLK_GENCTRL_SRC_OSCULP32K |
						GCLK_GENCTRL_GENEN;
	
	//clock generator 3 outputs OSC8M / 1 = 8MHz
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(3) | GCLK_GENDIV_DIV(1);
	GCLK->GENCTRL.reg =	GCLK_GENCTRL_ID(3) |
						GCLK_GENCTRL_SRC_OSC8M |
						GCLK_GENCTRL_GENEN;
	
	//clock generator 1 provides DPLL input clock
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_FDPLL |
						GCLK_CLKCTRL_GEN_GCLK1 |
						GCLK_CLKCTRL_CLKEN;
	
	//clock generator 2 provides DPLL lock clock
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_FDPLL32K |
						GCLK_CLKCTRL_GEN_GCLK2 |
						GCLK_CLKCTRL_CLKEN;
	
	//clock generator 3 provides TC4 and TC5 clock
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_TC4_TC5 |
						GCLK_CLKCTRL_GEN_GCLK3 |
						GCLK_CLKCTRL_CLKEN;
	
	//config PLL
	SYSCTRL->DPLLRATIO.reg = SYSCTRL_DPLLRATIO_LDR(divByM16 / 16) | SYSCTRL_DPLLRATIO_LDRFRAC(divByM16 % 16);
	SYSCTRL->DPLLCTRLB.reg = SYSCTRL_DPLLCTRLB_FILTER_DEFAULT |
								SYSCTRL_DPLLCTRLB_REFCLK_GCLK |
								SYSCTRL_DPLLCTRLB_LTIME_10MS |
								SYSCTRL_DPLLCTRLB_LBYPASS;
	SYSCTRL->DPLLCTRLA.bit.ONDEMAND = 0;
	SYSCTRL->DPLLCTRLA.bit.ENABLE = 1;
	for (maxWait = 1000000; maxWait; maxWait--) {
		if (SYSCTRL->DPLLSTATUS.bit.LOCK)
			break;
	}
	if (!maxWait) {
		pr("PLL failed to lock\n");
		while(1);
	}
	
	//switch to PLL / 1 as clock source (CLKGEN0 = PLL / 1)
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(0) | GCLK_GENDIV_DIV(1);
	GCLK->GENCTRL.reg =	GCLK_GENCTRL_ID(0) |
						GCLK_GENCTRL_SRC_FDPLL |
						GCLK_GENCTRL_GENEN;
	
	//clkgen0 also drives all SERCOM units (drivig them from another clock causes sync inssues we cannot fix)
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_SERCOM0_CORE |
						GCLK_CLKCTRL_GEN_GCLK0 |
						GCLK_CLKCTRL_CLKEN;
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_SERCOM1_CORE |
						GCLK_CLKCTRL_GEN_GCLK0 |
						GCLK_CLKCTRL_CLKEN;
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_SERCOM2_CORE |
						GCLK_CLKCTRL_GEN_GCLK0 |
						GCLK_CLKCTRL_CLKEN;
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_SERCOM3_CORE |
						GCLK_CLKCTRL_GEN_GCLK0 |
						GCLK_CLKCTRL_CLKEN;
	
	
	//set up DFLL48M
	SYSCTRL->DFLLMUL.reg = SYSCTRL_DFLLMUL_MUL(48000) | SYSCTRL_DFLLMUL_FSTEP(0xff / 8) | SYSCTRL_DFLLMUL_CSTEP(0x1f / 8);
	SYSCTRL->DFLLCTRL.reg = SYSCTRL_DFLLCTRL_ENABLE | SYSCTRL_DFLLCTRL_USBCRM | SYSCTRL_DFLLCTRL_BPLCKC | SYSCTRL_DFLLCTRL_CCDIS | SYSCTRL_DFLLCTRL_STABLE;

	//clock generator 4 outputs DFLL48M
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(4) | GCLK_GENDIV_DIV(1);
	GCLK->GENCTRL.reg =	GCLK_GENCTRL_ID(4) |
						GCLK_GENCTRL_SRC_DFLL48M |
						GCLK_GENCTRL_GENEN;
	
	//clock generator 4 provides USB clock
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_USB |
						GCLK_CLKCTRL_GEN_GCLK4 |
						GCLK_CLKCTRL_CLKEN;
	
	//enable SERCOMs
	PM->APBCMASK.reg |= PM_APBCMASK_SERCOM0 | PM_APBCMASK_SERCOM1 | PM_APBCMASK_SERCOM2 | PM_APBCMASK_SERCOM3 | PM_APBCMASK_TC4 | PM_APBCMASK_TC5;
	PM->APBBMASK.reg |= PM_APBBMASK_USB;
}

static void setupGpios(void)
{
	#define SET_MUX(_idx, _val)		do { if ((_idx) & 1) PORT->Group[0].PMUX[(_idx) / 2].bit.PMUXO = (_val); else PORT->Group[0].PMUX[(_idx) / 2].bit.PMUXE = (_val); } while(0)
	
	PORT->Group[0].DIRSET.reg =	PORT_PA14 |					//nCS for ram
								PORT_PA28 |					//nCS for SD
								PORT_PA04 | PORT_PA05 |		//SERCOM0
								PORT_PA16 | PORT_PA17 |		//SERCOM1
								PORT_PA08 | PORT_PA09 |		//SERCOM2
								PORT_PA22 | PORT_PA23 | 	//SERCOM3
								PORT_PA24 | PORT_PA25;	 	//USB
	PORT->Group[0].DIRCLR.reg = PORT_PA15 |					//CARDDET
								PORT_PA06 |					//SERCOM0
								PORT_PA18 |					//SERCOM1
								PORT_PA10 |					//SERCOM2
								PORT_PA19;					//SERCOM3

	//function outputs
	PORT->Group[0].PINCFG[PIN_PA04].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA05].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA08].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA09].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA16].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA17].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA22].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA23].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA24].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;
	PORT->Group[0].PINCFG[PIN_PA25].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR | PORT_PINCFG_PMUXEN;

	//function inputs
	PORT->Group[0].PINCFG[PIN_PA06].reg = PORT_PINCFG_INEN | PORT_PINCFG_PMUXEN | PORT_PINCFG_PULLEN;
	PORT->Group[0].PINCFG[PIN_PA10].reg = PORT_PINCFG_INEN | PORT_PINCFG_PMUXEN | PORT_PINCFG_PULLEN;
	PORT->Group[0].PINCFG[PIN_PA18].reg = PORT_PINCFG_INEN | PORT_PINCFG_PMUXEN | PORT_PINCFG_PULLEN;
	PORT->Group[0].PINCFG[PIN_PA19].reg = PORT_PINCFG_INEN | PORT_PINCFG_PMUXEN | PORT_PINCFG_PULLEN;
	
	//gpio outputs
	PORT->Group[0].PINCFG[PIN_PA14].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR;
	PORT->Group[0].PINCFG[PIN_PA28].reg = PORT_PINCFG_INEN | PORT_PINCFG_DRVSTR;
	
	//gpio inputs
	PORT->Group[0].PINCFG[PIN_PA15].reg = PORT_PINCFG_INEN | PORT_PINCFG_PULLEN;
	
	//pull and output values
	PORT->Group[0].OUTCLR.reg = PORT_PA06 | PORT_PA10 | PORT_PA18 | PORT_PA19;
	PORT->Group[0].OUTSET.reg = PORT_PA15 | PORT_PA14 | PORT_PA28;
	
	//functions
	SET_MUX(PIN_PA04, 3);
	SET_MUX(PIN_PA05, 3);
	SET_MUX(PIN_PA06, 3);
	
	SET_MUX(PIN_PA08, 3);
	SET_MUX(PIN_PA09, 3);
	SET_MUX(PIN_PA10, 3);
	
	SET_MUX(PIN_PA16, 2);
	SET_MUX(PIN_PA17, 2);
	SET_MUX(PIN_PA18, 2);
	
	SET_MUX(PIN_PA19, 3);
	SET_MUX(PIN_PA22, 2);
	SET_MUX(PIN_PA23, 2);
	
	SET_MUX(PIN_PA24, 6);
	SET_MUX(PIN_PA25, 6);
	
	#undef SET_MUX
}

static void setupSercom(Sercom *sercom)
{
	uint32_t ctrla;
	sercom->SPI.CTRLA.reg = 0;
	while (sercom->SPI.SYNCBUSY.bit.ENABLE);
	sercom->SPI.CTRLB.bit.RXEN = 1;
	//https://microchipsupport.force.com/s/article/SPI-max-clock-frequency-in-SAMD-SAMR-devices
	sercom->SPI.BAUD.reg = 2;
	while (sercom->SPI.SYNCBUSY.bit.CTRLB);
	sercom->SPI.CTRLA.reg = ctrla = SERCOM_SPI_CTRLA_DIPO((sercom == SERCOM3 ? 3 : 2)) | SERCOM_SPI_CTRLA_DOPO(0) | SERCOM_SPI_CTRLA_MODE_SPI_MASTER;
	sercom->SPI.CTRLA.reg = ctrla | SERCOM_SPI_CTRLA_ENABLE;
	while (sercom->SPI.SYNCBUSY.bit.ENABLE);
}

static void setupDmac(void)
{
	uint_fast8_t i;
	
	DMAC->BASEADDR.bit.BASEADDR = (uintptr_t)&mDmaDescrsInitial;
	DMAC->WRBADDR.bit.WRBADDR = (uintptr_t)&mDmaDescrsWriteback;
	DMAC->CTRL.reg = DMAC_CTRL_DMAENABLE | DMAC_CTRL_LVLEN0 | DMAC_CTRL_LVLEN1 | DMAC_CTRL_LVLEN2 | DMAC_CTRL_LVLEN3;

	for (i = 0; i < NUM_DMA_CHANNELS_USED; i++) {
		
		//config channel, clear flag
		DMAC->CHID.bit.ID = i;
		DMAC->CHCTRLB.reg = DMAC_CHCTRLB_LVL_LVL1 | DMAC_CHCTRLB_TRIGSRC(mDmaTriggers[i]) | DMAC_CHCTRLB_TRIGACT_BEAT;
		DMAC->CHINTFLAG.reg = DMAC_CHINTFLAG_TERR | DMAC_CHINTFLAG_TCMPL | DMAC_CHINTFLAG_SUSP;
	}
}

void __attribute__((noinline)) initHw(void)
{
	loadCalibrationValues();
	setupClocking();
	setupGpios();
	setupDmac();
	setupSercom(SERCOM0);
	setupSercom(SERCOM1);
	setupSercom(SERCOM2);
	setupSercom(SERCOM3);
}
