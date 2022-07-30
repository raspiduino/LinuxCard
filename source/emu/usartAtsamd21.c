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



#define MY_VID							('D' + ('G' << 8))	//<voice type="hippie">You can't own numbers, man...</voice>
#define MY_PID							('L' + ('C' << 8))

#define STR_IDX_LANG_ID					0
#define STR_IDX_MANUF					1
#define STR_IDX_PRODNAME				2
#define STR_IDX_SNUM					3
#define STR_IDX_CFGNAME_UARTS			4

#define CFG_IDX_UARTS					1

#define NUM_CDCS						2

#define IF_NO_OFST_CDC_CTRL				0
#define IF_NO_OFST_CDC_DATA				1

#define EP_NO_OFST_CDC_CTRL				1		//1 IN
#define EP_SZ_CDC_CTRL					8
#define EP_NO_OFST_CDC_DATA_IN			2		//2 IN
#define EP_SZ_CDC_DATA_IN				8
#define EP_NO_OFST_CDC_DATA_OUT			2		//2 OUT
#define EP_SZ_CDC_DATA_OUT				8


//RX state
static volatile uint8_t mRxBufLL[EP_SZ_CDC_DATA_OUT];

//TX state
static bool mRunning;




void usbExtEpDataArrivalNotif(uint8_t epNo, uint_fast8_t len)
{
	int_fast8_t dst;
	
	switch (epNo) {
		case EP_NO_OFST_CDC_DATA_OUT + 0 * 2:	//CDC0
			dst = 3;							//TTY3
			break;
		case EP_NO_OFST_CDC_DATA_OUT + 1 * 2:	//CDC1
			dst = 0;							//TTY0
			break;
		case EP_NO_OFST_CDC_DATA_OUT + 2 * 2:	//CDC2
			dst = 1;							//TTY1
			break;
		default:
			dst = -1;
			break;
	}
	
	if (dst >= 0) {
	
		uint_fast8_t i;
		
		for (i = 0; i < len; i++)
			dz11charRx(dst, mRxBufLL[i]);
	}
	usbRxRelease(epNo);
}

void usbExtStateChangedNotif(enum UsbState nowState)
{
	mRunning = nowState == UsbRunning;
}

void usbExtConfigSelected(uint8_t cfg)
{
	if (cfg == CFG_IDX_UARTS) {
		
		uint_fast8_t i;
		
		for (i = 0; i < NUM_CDCS; i++) {
		
			if (!usbEpBulkCfgIn(EP_NO_OFST_CDC_CTRL + 2 * i, EP_SZ_CDC_CTRL))
				pr("failed to configure (1 + 2 * %u).IN\n", i);
			if (!usbEpBulkCfgIn(EP_NO_OFST_CDC_DATA_IN + 2 * i, EP_SZ_CDC_DATA_IN))
				pr("failed to configure (2 + 2 * %u).IN\n", i);
			if (!usbEpBulkCfgOut(EP_NO_OFST_CDC_DATA_OUT + 2 * i, EP_SZ_CDC_DATA_OUT, (void*)mRxBufLL))
				pr("failed to configure (2 + 2 * %u).OUT\n", i);
		}
	}
	else {
		
		//deconfigured... not really supported
	}
}

bool usbExtEp0handler(const struct UsbSetup *setup, void **dataOutP, int16_t *lenOutP)
{
	if ((setup->bmRequestType & 0x7f) == 0x21) {
		
		uint_fast8_t i;
		
		for (i = 0; i < NUM_CDCS; i++) {
			if (setup->wIndex == IF_NO_OFST_CDC_CTRL + 2 * i || setup->wIndex == IF_NO_OFST_CDC_DATA + 2 * i) {
				
				*lenOutP = -1;
				return true;
			}
		}
	}
	
	return false;
}

const void* usbExtGetGescriptor(uint_fast8_t descrType, uint_fast8_t descrIdx, uint_fast16_t langIdx, uint16_t *lenP)
{
	if (descrType == DESCR_TYP_DEVICE && descrIdx == 0) {
		
		static struct UsbDeviceDescriptor __attribute__((aligned(4))) mDevDescr = {
			.bLength = sizeof(struct UsbDeviceDescriptor),
			.bDescriptorType = DESCR_TYP_DEVICE,
			.bcdUSB = 0x0200,
			.bMaxPacketSize0 = EP0_SZ,
			.idVendor = MY_VID,
			.idProduct = MY_PID,
			.bcdDevice = 0x0100,
			.iManufacturer = STR_IDX_MANUF,
			.iProduct = STR_IDX_PRODNAME,
			.iSerialNumber = STR_IDX_SNUM,
			.bNumConfigurations = 1,
		};
		
		*lenP = sizeof(mDevDescr);
		return &mDevDescr;
	}
	else if (descrType == DESCR_TYP_CONFIG && descrIdx == 0) {
		
		static struct {
			struct UsbConfigDescriptor cfg;
			struct {
				struct UsbInterfaceDescriptor ifCdcCtrl;
				struct UsbCdcHeaderFunctionalDescriptor ifCdcHdr;
				struct UsbCdcAcmFunctionalDescriptor ifCdcAcm;
				struct UsbCdcUnionFunctionalDescriptor ifCdcUnion;
	  			struct UsbCdcCallManagementFunctionalDescriptor ifCdcCallMgmnt;
	  			struct UsbEndpointDescriptor epCdcCtrl;
				struct UsbInterfaceDescriptor ifCdcData;
	  			struct UsbEndpointDescriptor epCdcDataOut;
	  			struct UsbEndpointDescriptor epCdcDataIn;
	  		} __attribute__((packed)) cdc[NUM_CDCS];
		} __attribute__((packed, aligned(4))) myDescriptor = {
			.cfg = {
				.bLength = sizeof(struct UsbConfigDescriptor),
				.bDescriptorType = DESCR_TYP_CONFIG,
				.numBytesReturned =		sizeof(struct UsbConfigDescriptor) +
										NUM_CDCS * (
											sizeof(struct UsbInterfaceDescriptor) +
											sizeof(struct UsbCdcHeaderFunctionalDescriptor) +
											sizeof(struct UsbCdcAcmFunctionalDescriptor) +
											sizeof(struct UsbCdcUnionFunctionalDescriptor) +
											sizeof(struct UsbCdcCallManagementFunctionalDescriptor) +
											sizeof(struct UsbEndpointDescriptor) +
											sizeof(struct UsbInterfaceDescriptor) +
											sizeof(struct UsbEndpointDescriptor) +
											sizeof(struct UsbEndpointDescriptor)
										),
				.numIfaces = 2 * NUM_CDCS,	//2 per CDC instance
				.thisCfgIdx = CFG_IDX_UARTS,
				.iCfgNameIdx = STR_IDX_CFGNAME_UARTS,
				.bAttributes = 0x80,
				.bCurrent = 50,
			},
			.cdc = {
			
			#define CDC_DEFINITION(_idx)																	\
				[_idx] = {																					\
					.ifCdcCtrl = {																			\
						.bLength = sizeof(struct UsbInterfaceDescriptor),									\
						.bDescriptorType = DESCR_TYP_IFACE,													\
						.bInterfaceNumber = IF_NO_OFST_CDC_CTRL + _idx * 2,									\
						.bNumEndpoints = 1,			/* CDC.ACM control uses 1 EP */							\
						.bInterfaceClass = 2,		/* CDC */												\
						.bInterfaceSubClass = 2,	/* ACM */												\
					},																						\
					.ifCdcHdr = {																			\
						.bFunctionLength = sizeof(struct UsbCdcHeaderFunctionalDescriptor),					\
						.bDescriptorType = DESCR_TYP_CDC_CS_IFACE,											\
						.bDescriptorSubtype = DESCR_SUBTYP_CDC_HDR,											\
						.bcdCDC = 0x0110,																	\
					},																						\
					.ifCdcAcm = {																			\
						.bFunctionLength = sizeof(struct UsbCdcAcmFunctionalDescriptor),					\
						.bDescriptorType = DESCR_TYP_CDC_CS_IFACE,											\
						.bDescriptorSubtype = DESCR_SUBTYP_CDC_ACM,											\
						.bmCapabilities = 0,																\
					},																						\
					.ifCdcUnion = {																			\
						.bFunctionLength = sizeof(struct UsbCdcUnionFunctionalDescriptor),					\
						.bDescriptorType = DESCR_TYP_CDC_CS_IFACE,											\
						.bDescriptorSubtype = DESCR_SUBTYP_CDC_UNION,										\
						.bMasterInterface = IF_NO_OFST_CDC_CTRL + _idx * 2,									\
						.bSlaveInterface0 = IF_NO_OFST_CDC_DATA + _idx * 2,									\
					},																						\
					.ifCdcCallMgmnt = {																		\
						.bFunctionLength = sizeof(struct UsbCdcCallManagementFunctionalDescriptor),			\
						.bDescriptorType = DESCR_TYP_CDC_CS_IFACE,											\
						.bDescriptorSubtype = DESCR_SUBTYP_CDC_CALL_MGMNT,									\
						.bmCapabilities = 0x03,	/* we handle shit ourselves */								\
						.bDataInterface = IF_NO_OFST_CDC_DATA + _idx * 2,									\
					},																						\
					.epCdcCtrl = {																			\
						.bLength = sizeof(struct UsbEndpointDescriptor),									\
						.bDescriptorType = DESCR_TYP_ENDPT,													\
						.bEndpointAddress = (EP_NO_OFST_CDC_CTRL + _idx * 2) | USB_DESCR_EP_NO_MASK_IN,		\
						.bmAttributes = USB_EP_DESCR_EP_TYP_INTR,											\
						.wMaxPacketSize = EP_SZ_CDC_CTRL,													\
						.bInterval = 32,																	\
					},																						\
					.ifCdcData = {																			\
						.bLength = sizeof(struct UsbInterfaceDescriptor),									\
						.bDescriptorType = DESCR_TYP_IFACE,													\
						.bInterfaceNumber = IF_NO_OFST_CDC_DATA + _idx * 2,									\
						.bNumEndpoints = 2,																	\
						.bInterfaceClass = 0x0A,	/* CDC DATA */											\
					},																						\
					.epCdcDataOut = {																		\
						.bLength = sizeof(struct UsbEndpointDescriptor),									\
						.bDescriptorType = DESCR_TYP_ENDPT,													\
						.bEndpointAddress = EP_NO_OFST_CDC_DATA_OUT + _idx * 2,								\
						.bmAttributes = USB_EP_DESCR_EP_TYP_BULK,											\
						.wMaxPacketSize = EP_SZ_CDC_DATA_OUT,												\
					},																						\
					.epCdcDataIn = {																		\
						.bLength = sizeof(struct UsbEndpointDescriptor),									\
						.bDescriptorType = DESCR_TYP_ENDPT,													\
						.bEndpointAddress = (EP_NO_OFST_CDC_DATA_IN + _idx * 2) | USB_DESCR_EP_NO_MASK_IN,	\
						.bmAttributes = USB_EP_DESCR_EP_TYP_BULK,											\
						.wMaxPacketSize = EP_SZ_CDC_DATA_IN,												\
					},																						\
				}
				
				CDC_DEFINITION(0),
			#if NUM_CDCS >= 2
				CDC_DEFINITION(1),
			#endif
			#if NUM_CDCS >= 3
				CDC_DEFINITION(2),
			#endif
				#undef CDC_DEFINITION
			},
		};
		
		*lenP = sizeof(myDescriptor);
		return &myDescriptor;
	}
	else if (descrType == DESCR_TYP_STRING) {
		
		static struct UsbStringDescriptor __attribute__((aligned(4))) mStrSupportedLangs = {.bLength = sizeof(struct UsbStringDescriptor) + 2 * 1, .bDescriptorType = DESCR_TYP_STRING, .chars = {0x0409},};
		static struct UsbStringDescriptor __attribute__((aligned(4))) mStrManufacturer = {.bLength = sizeof(struct UsbStringDescriptor) + 2 * 9, .bDescriptorType = DESCR_TYP_STRING, .chars = {'D', 'm', 'i', 't', 'r', 'y', '.', 'G', 'R', }};
		static struct UsbStringDescriptor __attribute__((aligned(4))) mStrProduct = {.bLength = sizeof(struct UsbStringDescriptor) + 2 * 9, .bDescriptorType = DESCR_TYP_STRING, .chars = {'L', 'i', 'n', 'u', 'x', 'C', 'a', 'r', 'd', }};
		static struct UsbStringDescriptor __attribute__((aligned(4))) mStrSerialNumber = {.bLength = sizeof(struct UsbStringDescriptor) + 2 * 14, .bDescriptorType = DESCR_TYP_STRING, .chars = {'3', '1', '4', '1', '5', '9', '2', '6', '5', '3', '5', '8', '9', '8', }};
		static struct UsbStringDescriptor __attribute__((aligned(4))) mStrCfgNameUarts = {.bLength = sizeof(struct UsbStringDescriptor) + 2 * 12, .bDescriptorType = DESCR_TYP_STRING, .chars = {'V', 'i', 'r', 't', 'u', 'a', 'l', 'U', 'A', 'R', 'T', 'S', }};
		
		static const struct UsbStringDescriptor* mDescrs[] = {
			[STR_IDX_LANG_ID] = &mStrSupportedLangs,
			[STR_IDX_MANUF] = &mStrManufacturer,
			[STR_IDX_PRODNAME] = &mStrProduct,
			[STR_IDX_SNUM] = &mStrSerialNumber,
			[STR_IDX_CFGNAME_UARTS] = &mStrCfgNameUarts,
		};
		
		if (descrIdx < sizeof(mDescrs) / sizeof(*mDescrs) && mDescrs[descrIdx]) {
			
			*lenP = mDescrs[descrIdx]->bLength;
			return mDescrs[descrIdx];
		}
		
		return NULL;
	}

	if ((descrType != 6 || descrIdx != 0))
		pr("no descr %u.%u\n", descrType, descrIdx);
	return NULL;
}

void usartSetBuadrate(uint32_t baud)
{
	
}

void usartInit(void)
{
	if (!usbInit())
		pr("failed to init USB\n");
	else {
		uint64_t time = getTime();
		
		while (getTime() - time < TICKS_PER_SECOND / 2);
		
		if (!usbAttach(true))
			pr("failed to attach USB\n");
		else
			pr("usb up\n");
	}
}

void usartTxEx(uint8_t channel, uint8_t ch)
{
	static volatile uint32_t mTxByte[NUM_CDCS];	//for usb, alignment of 4 bytes is needed, so use 32-bit vals
	uint_fast8_t cdcIdx;
	uint64_t time;
	
	switch (channel) {
		
		case 3:
			cdcIdx = 0;
			break;
#if NUM_CDCS >= 2
		case 0:
			cdcIdx = 1;
			break;
#endif
#if NUM_CDCS >= 3
		case 1:
			cdcIdx = 2;
			break;
#endif
		default:
			return;
	}
	
	if (!mRunning)
		return;
		
	time = getTime();
	while (getTime() - time < TICKS_PER_SECOND / 20) {		//allow 50ms per char
		if (usbEpCanTx(EP_NO_OFST_CDC_DATA_IN + 2 * cdcIdx)) {
			
			mTxByte[cdcIdx] = ch;
			usbEpTx(EP_NO_OFST_CDC_DATA_IN + 2 * cdcIdx, (void*)(mTxByte + cdcIdx), 1, true);
			return;
		}
	}
}

void usartTx(uint8_t ch)
{
	return usartTxEx(3, ch);
}
