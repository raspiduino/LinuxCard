/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <string.h>
#include "samda1e16b.h"
#include "usbDev.h"
#include "printf.h"



#define GET_STATUS				0
#define CLEAR_FEATURE			1
#define SET_FEATURE				3
#define SET_ADDRESS				5
#define GET_DESCRIPTOR			6
#define SET_DESCRIPTOR			7
#define GET_CONFIGURATION		8
#define SET_CONFIGURATION		9
#define GET_INTERFACE			10
#define SET_INTERFACE			11
#define SYNCH_FRAME				12

#define BM_REQ_IS_TO_HOST(r)	(!!((r) & 0x80))




#define BM_REQ_IS_DEVIC(r)		(((r) & 0x1F) == 0x00)
#define BM_REQ_IS_INTFC(r)		(((r) & 0x1F) == 0x01)
#define BM_REQ_IS_ENDPT(r)		(((r) & 0x1F) == 0x02)
#define BM_REQ_IS_OTHER(r)		(((r) & 0x1F) == 0x03)

#define BM_REQ_IS_STD(r)		(((r) & 0x60) == 0x00)
#define BM_REQ_IS_CLS(r)		(((r) & 0x60) == 0x20)
#define BM_REQ_IS_VEN(r)		(((r) & 0x60) == 0x40)

#define EP_SZ_TO_EPSZ_VAL(x)	(28 - __builtin_clz(x))

static enum UsbState mUsbState;
static uint8_t mCfgIdx, mEpSz[USB_MAX_EP_IDX + 1][2];
static uint32_t mAlreadyNotifiedRx;

static volatile uint8_t mEp0bufRx[EP0_SZ];

static volatile UsbDeviceDescBank mEpCfgs[USB_MAX_EP_IDX + 1][2] = {};	//always 2 banks per EP :(



static void usbPrvSetState(enum UsbState newState)
{
	mUsbState = newState;
	usbExtStateChangedNotif(newState);
}

static void usbPrvControlTxWait(void)
{
	while(!USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.bit.TRCPT1);
}

static bool usbPrvEpStallNocheck(uint_fast8_t epNo, uint_fast8_t structIdx, bool stall)
{
	if (stall)
		USB->DEVICE.DeviceEndpoint[epNo].EPSTATUSSET.reg = structIdx ? USB_DEVICE_EPSTATUSSET_STALLRQ1 : USB_DEVICE_EPSTATUSSET_STALLRQ0;
	else
		USB->DEVICE.DeviceEndpoint[epNo].EPSTATUSCLR.reg = structIdx ? USB_DEVICE_EPSTATUSCLR_STALLRQ1 : USB_DEVICE_EPSTATUSCLR_STALLRQ0;
	
	return true;
}

static bool usbPrvEpStall(uint_fast8_t epNo, uint_fast8_t structIdx, bool stall)
{
	if (!epNo || epNo > USB_MAX_EP_IDX)
		return false;
	
	return usbPrvEpStallNocheck(epNo, structIdx, stall);
}

static void usbPrvControlStall(bool stall)
{
	usbPrvEpStallNocheck(0, 1, stall);
}

static bool usbPrvEpTx(uint_fast8_t epNo, const void *data, uint_fast16_t len, bool autoZlp)
{
	if (USB->DEVICE.DeviceEndpoint[epNo].EPSTATUS.bit.BK1RDY) {
		pr("sending on ep%u while busy\n", epNo);
		return false;
	}
	
	mEpCfgs[epNo][1].ADDR.reg = (uintptr_t)data;
	mEpCfgs[epNo][1].PCKSIZE.reg = USB_DEVICE_PCKSIZE_SIZE(mEpSz[epNo][1]) |
									USB_DEVICE_PCKSIZE_BYTE_COUNT(len) | 
									(autoZlp ? USB_DEVICE_PCKSIZE_AUTO_ZLP : 0);
	
	asm volatile("":::"memory");
	USB->DEVICE.DeviceEndpoint[epNo].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT1 | USB_DEVICE_EPINTFLAG_TRFAIL1 | USB_DEVICE_EPINTFLAG_STALL1;
	USB->DEVICE.DeviceEndpoint[epNo].EPSTATUSSET.reg = USB_DEVICE_EPSTATUSSET_BK1RDY;

	return true;
}

bool usbEpTx(uint_fast8_t epNo, const void *data, uint_fast16_t len, bool autoZlp)
{
	if (!epNo || epNo > USB_MAX_EP_IDX)
		return false;
	
	if (len >= 16384)
		return false;
	
	return usbPrvEpTx(epNo, data, len, autoZlp);
}

bool usbEpCanTx(uint_fast8_t epNo)
{
	if (!epNo || epNo > USB_MAX_EP_IDX)
		return false;
	
	return !USB->DEVICE.DeviceEndpoint[epNo].EPSTATUS.bit.BK1RDY;
}

static void usbPrvControlSend(const void *data, uint_fast16_t len)
{
	usbPrvControlStall(false);
	if (!usbPrvEpTx(0, data, len, false))
		pr("unable to TX for ep0...\n");
}

static bool usbPrvHandleGetDescriptor(uint_fast8_t descrType, uint_fast8_t descrIdx, uint_fast16_t langIdx, uint_fast16_t maxLen)
{
	const void *descr;
	uint16_t descrLen;
	
	descr = usbExtGetGescriptor(descrType, descrIdx, langIdx, &descrLen);
	if (descr) {
		if (descrLen > maxLen)
			descrLen = maxLen;
		
		usbPrvControlSend(descr, descrLen);
		return true;
	}
	
	return false;
}

bool usbEpPrvIsStalled(uint_fast8_t epNo, uint_fast8_t structIdx)
{
	uint32_t sta = USB->DEVICE.DeviceEndpoint[epNo].EPSTATUS.reg;
	
	return !!(sta & (structIdx ? USB_DEVICE_EPSTATUS_STALLRQ1 : USB_DEVICE_EPSTATUS_STALLRQ0));
}

static void usbPrvHandleSetup(void)
{
	struct UsbSetup *pSetup = (struct UsbSetup*)mEp0bufRx, setup = *pSetup;
	uint_fast8_t rxLen = mEpCfgs[0][0].PCKSIZE.bit.BYTE_COUNT;
	static uint32_t mTxBuf;	//a small buffer for small TXs
	void *cliDataP = &mTxBuf;
	int16_t cliSz;
	
	asm volatile("":::"memory");
	mEpCfgs[0][0].PCKSIZE.reg = USB_DEVICE_PCKSIZE_SIZE(mEpSz[0][0]);
	USB->DEVICE.DeviceEndpoint[0].EPSTATUSCLR.reg = USB_DEVICE_EPSTATUSSET_BK0RDY;
	
	memset(pSetup, 0x55, sizeof(*pSetup));
	
	if (BM_REQ_IS_STD(setup.bmRequestType)) {
		
		if (BM_REQ_IS_TO_HOST(setup.bmRequestType) && setup.bRequest == GET_DESCRIPTOR) {
			
			//wValueH is descriptor type
			//wValueL is descriptor index (only for config & string descriptors, else zero)
			//wIndex is languageID for string, zero else
			//wLength is max num bytes to return
			if (usbPrvHandleGetDescriptor(setup.wValueH, setup.wValueL, setup.wIndex, setup.wLength))
				goto success;
		}
		if (!BM_REQ_IS_TO_HOST(setup.bmRequestType) && setup.bRequest == SET_CONFIGURATION){
		
			//wValueL is desired config
			mCfgIdx = setup.wValueL;
			usbExtConfigSelected(setup.wValueL);
			usbPrvSetState(UsbRunning);
			usbPrvControlSend(&setup, 0);	//send ZLP
			goto success;
		}
		if (!BM_REQ_IS_TO_HOST(setup.bmRequestType) && setup.bRequest == SET_ADDRESS){
		
			//wValueL is address
			usbPrvSetState(UsbAssigningAddress);
			USB->DEVICE.DADD.reg = USB_DEVICE_DADD_DADD(setup.wValueL);
			usbPrvControlSend(&setup, 0);	//send ZLP
			usbPrvControlTxWait();	//ths sucks, but TRCPT1 doesnt always fire, so we are stuck busy-waiting here
			USB->DEVICE.DADD.bit.ADDEN = 1;
			usbPrvSetState(UsbWaitingForConfig);
			goto success;
		}
		if (BM_REQ_IS_TO_HOST(setup.bmRequestType) && setup.bRequest == GET_CONFIGURATION){
		
			mTxBuf = mCfgIdx;
			usbPrvControlSend(&mTxBuf, 1);
			goto success;
		}
		if (BM_REQ_IS_ENDPT(setup.bmRequestType) && (setup.bRequest == CLEAR_FEATURE || setup.bRequest == SET_FEATURE)) {	//clear stall
				
			bool in = !!(setup.wIndexL & 0x80);
			uint_fast8_t epNo = setup.wIndexL & 0x7f;
			
			if (in)
				usbEpStallIn(epNo, setup.bRequest == SET_FEATURE);
			else
				usbEpStallOut(epNo, setup.bRequest == SET_FEATURE);
			
			goto success;
		}
		if (BM_REQ_IS_ENDPT(setup.bmRequestType) && BM_REQ_IS_TO_HOST(setup.bmRequestType) && setup.bRequest == GET_STATUS && setup.wLength == 2) {
				
			mTxBuf = usbEpPrvIsStalled(setup.wIndexL & 0x7f, (setup.wIndexL & 0x80) ? 1 : 0) ? 1 : 0;
			usbPrvControlSend(&mTxBuf, 2);
			goto success;
		}
	}
	else if (usbExtEp0handler(&setup, &cliDataP, &cliSz)) {
		
		if (cliSz < 0)
			goto stall_quietly;
		else {
			usbPrvControlSend(cliDataP, cliSz);
			goto success;
		}
	}

	pr("SETUP %u bytes\n", rxLen);
	pr("unknown request (%u b) %02x.%02x, 0x%04x 0x%04x 0x%04x\n", rxLen, setup.bmRequestType, setup.bRequest,
		setup.wValue, setup.wIndex, setup.wLength);

stall_quietly:
	usbPrvControlStall(true);
	return;

success:
	return;
}

void __attribute__((used)) USB_IRQHandler(void)
{
	uint32_t sta = USB->DEVICE.INTFLAG.reg, clr = 0;
	uint32_t ep0sta = USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg, ep0clr = 0;
	uint_fast8_t i;
	
	if (sta & USB_DEVICE_INTFLAG_EORST) {
	
		USB->DEVICE.DADD.reg = 0;
		clr |= USB_DEVICE_INTENCLR_EORST;
		USB->DEVICE.DeviceEndpoint[0].EPCFG.reg = USB_DEVICE_EPCFG_EPTYPE0(1) | USB_DEVICE_EPCFG_EPTYPE1(1);
		USB->DEVICE.DeviceEndpoint[0].EPINTENSET.reg = USB_DEVICE_EPINTENSET_TRCPT0 | USB_DEVICE_EPINTENSET_RXSTP;
		mCfgIdx = 0;
		usbPrvSetState(UsbWaitForEnum);
	}
	else {
	
		if (ep0sta & USB_DEVICE_EPINTFLAG_RXSTP) {
			
			ep0clr |= USB_DEVICE_EPINTFLAG_RXSTP;
			usbPrvHandleSetup();
		}
		if (ep0sta & USB_DEVICE_EPINTFLAG_TRCPT0) {
			
			ep0clr |= USB_DEVICE_EPINTFLAG_TRCPT0;
		}

		
		if (ep0sta &~ (ep0clr | USB_DEVICE_EPINTFLAG_TRCPT1 | USB_DEVICE_EPINTFLAG_STALL1))
			pr("usb irq EPINT = 0x%04x, ep0sta = 0x%08x\n", USB->DEVICE.EPINTSMRY.reg, ep0sta);
		
		for (i = 1; i <= USB_MAX_EP_IDX; i++) {
			
			if (USB->DEVICE.DeviceEndpoint[i].EPSTATUS.bit.BK0RDY && !(mAlreadyNotifiedRx & (1 << i))) {
				
				mAlreadyNotifiedRx |= 1 << i;
				usbExtEpDataArrivalNotif(i, mEpCfgs[i][0].PCKSIZE.bit.BYTE_COUNT);
			}
			USB->DEVICE.DeviceEndpoint[i].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT0;
		}
	}
	
	USB->DEVICE.INTFLAG.reg = clr;
	USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = ep0clr;
}

bool usbRxRelease(uint_fast8_t epNo)
{
	if (!epNo || epNo > USB_MAX_EP_IDX)
		return false;
	
	if (!USB->DEVICE.DeviceEndpoint[epNo].EPSTATUS.bit.BK0RDY)
		pr("releaseing unused rx buffer\n");
	
	mAlreadyNotifiedRx &=~ (1 << epNo);
	asm volatile("":::"memory");
	mEpCfgs[epNo][0].PCKSIZE.reg = USB_DEVICE_PCKSIZE_SIZE(mEpSz[epNo][0]);
	USB->DEVICE.DeviceEndpoint[epNo].EPSTATUSCLR.reg = USB_DEVICE_EPSTATUSSET_BK0RDY;
	
	return true;
}

static bool usbPrvEpCfg(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr, uint_fast8_t epType, uint_fast8_t structIdx, bool makeAvailable)
{
	if (!epSz || epSz > 64 || (epSz & (epSz - 1)))
		return false;
	
	if (!epNo || epNo > USB_MAX_EP_IDX)
		return false;
	
	pr("ep %u.%u sz %u type %u\n", epNo, structIdx, epSz, epType);
	epSz = EP_SZ_TO_EPSZ_VAL(epSz);
	
	mEpSz[epNo][structIdx] = epSz;
	
	mEpCfgs[epNo][structIdx].ADDR.reg = (uintptr_t)dataPtr;
	mEpCfgs[epNo][structIdx].PCKSIZE.reg = USB_DEVICE_PCKSIZE_SIZE(epSz);
	
	if (structIdx) {	//IN
		USB->DEVICE.DeviceEndpoint[epNo].EPCFG.bit.EPTYPE1 = epType;
	}
	else {				//OUT
		USB->DEVICE.DeviceEndpoint[epNo].EPCFG.bit.EPTYPE0 = epType;
		USB->DEVICE.DeviceEndpoint[epNo].EPINTENSET.reg = USB_DEVICE_EPINTENSET_TRCPT0;
	}
	
	return true;
}

bool usbEpStallIn(uint_fast8_t epNo, bool stall)
{
	return usbPrvEpStall(epNo, 1, stall);
}

bool usbEpStallOut(uint_fast8_t epNo, bool stall)
{
	return usbPrvEpStall(epNo, 0, stall);
}

bool usbEpIntrCfgIn(uint_fast8_t epNo, uint_fast16_t epSz)
{
	return usbPrvEpCfg(epNo, epSz, NULL, 4, 1, false);
}

bool usbEpIntrCfgOut(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr)
{
	return usbPrvEpCfg(epNo, epSz, dataPtr, 4, 0, true);
}

bool usbEpBulkCfgIn(uint_fast8_t epNo, uint_fast16_t epSz)
{
	return usbPrvEpCfg(epNo, epSz, NULL, 3, 1, false);
}

bool usbEpBulkCfgOut(uint_fast8_t epNo, uint_fast16_t epSz, void *dataPtr)
{
	return usbPrvEpCfg(epNo, epSz, dataPtr, 3, 0, true);
}

bool usbInit(void)
{
	usbPrvSetState(UsbDisconnected);
	
	if (EP0_SZ < 8 || EP0_SZ > 64 || (EP0_SZ & (EP0_SZ - 1)))
		return false;
	
	mEpCfgs[0][0].ADDR.reg = (uintptr_t)mEp0bufRx,
	mEpCfgs[0][0].PCKSIZE.reg = USB_DEVICE_PCKSIZE_SIZE(EP_SZ_TO_EPSZ_VAL(EP0_SZ));
	
	mEpSz[0][0] = EP_SZ_TO_EPSZ_VAL(EP0_SZ);
	mEpSz[0][1] = EP_SZ_TO_EPSZ_VAL(EP0_SZ);
	
	//reset
	USB->DEVICE.CTRLA.bit.SWRST = 1;
	while (USB->DEVICE.SYNCBUSY.bit.SWRST);
	USB->DEVICE.CTRLA.bit.SWRST = 0;
	while (USB->DEVICE.SYNCBUSY.bit.SWRST);
	
	//configs
	USB->DEVICE.CTRLA.reg = USB_CTRLA_RUNSTDBY | USB_CTRLA_MODE_DEVICE;
	USB->DEVICE.CTRLB.reg = USB_DEVICE_CTRLB_DETACH | USB_DEVICE_CTRLB_SPDCONF_FS | USB_DEVICE_CTRLB_LPMHDSK_NO;
	USB->DEVICE.DESCADD.reg = (uintptr_t)mEpCfgs;
	USB->DEVICE.DADD.reg = 0;
	USB->DEVICE.INTENSET.reg = USB_DEVICE_INTENSET_EORST;
	USB->DEVICE.CTRLA.bit.ENABLE = 1;
	
	NVIC_EnableIRQ(USB_IRQn);
	
	return true;
}

bool usbAttach(bool attached)
{
	if (attached) {
		if (mUsbState != UsbDisconnected)
			return false;
		
		usbPrvSetState(UsbWaitForReset);
		USB->DEVICE.CTRLB.bit.DETACH = 0;
		return true;
	}
	else if (mUsbState == UsbDisconnected)
		return false;
	else {
		
		USB->DEVICE.CTRLB.bit.DETACH = 1;
		usbPrvSetState(UsbDisconnected);
		return true;
	}
}

