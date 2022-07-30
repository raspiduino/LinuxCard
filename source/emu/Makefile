#
#	(c) 2021 Dmitry Grinberg   https://dmitry.gr
#	Non-commercial use only OR licensing@dmitry.gr
#

SOURCES		= mem.c decBus.c dz11.c fpu.c
LDFLAGS		= -lm -g
CCFLAGS		= -fno-math-errno -Wno-unused-but-set-variable -Wno-unused-variable
CPU			?= atsamd21


ifeq ($(CPU),CortexEmu)
	ZWT_ADDR = 0x4fffffff
	CCFLAGS	+= -Ofast -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_CORTEXEMU -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=100000U
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=5 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	#CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_CortexEmu.c printf.c sd.c ucHwCortexEmu.c timebase.c main_uc.c spiRamCortexEmu.c usartCortexEmu.c ds1287CortexEmu.c sdHwCortexEmu.c
	SOURCES += cpuJit.c
	#SOURCES += cpuAsm.S
	LKR		= linker_CortexEmu.lkr
else ifeq ($(CPU),atsamd21)
	ZWT_ADDR = 0x20001FF8
	CCFLAGS	+= -Ofast -Wall -Wextra -Werror -mthumb -fsingle-precision-constant -ffast-math -march=armv6-m -mcpu=cortex-m0plus -I. -mfloat-abi=soft
	CCFLAGS	+= -ffunction-sections -fdata-sections -fomit-frame-pointer -Wno-unused-function -Wno-unused-parameter
	CCFLAGS	+= -D"err_str(...)=pr(__VA_ARGS__)"
	CCFLAGS	+= -DSLOW_FLASH -DCPU_SAMD -DCPU_TYPE_CM0 -DZWT_ADDR=$(ZWT_ADDR) -DTICKS_PER_SECOND=90000000U
	CCFLAGS	+= -DICACHE_NUM_SETS_ORDER=5 -DOPTIMAL_RAM_WR_SZ=32 -DOPTIMAL_RAM_RD_SZ=32
	CCFLAGS	+= -DMULTICHANNEL_UART
	#CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	LDFLAGS += -Wl,--gc-sections -Wl,-T $(LKR) -lm
	CC		= arm-none-eabi-gcc
	SOURCES += crt_atsamd21.c printf.c sd.c ucHwAtsamd21.c timebase.c main_uc.c spiRamAtsamd21.c usartAtsamd21.c ds1287atsamd21.c usbDev.c
	SOURCES += cpuAsm.S
	SOURCES += sdHwAtsamd21spi.c
	LKR		= linker_atsamd21.lkr
else
	CCFLAGS	+= -O2 -g -ggdb3 -fvar-tracking -Wall -Wextra -Werror -D"err_str(...)=fprintf(stderr, __VA_ARGS__)" -DGDB_SUPPORT
	CCFLAGS	+= -D_FILE_OFFSET_BITS=64 -D__USE_LARGEFILE64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
	CCFLAGS	+= -DSUPPORT_DEBUG_PRINTF
	CC		= gcc
	SOURCES	+= cpu.c soc_pc.c main.c ds1287.c
endif


ifneq ($(LTO),0)
	CCFLAGS += -flto
endif

LDFLAGS += $(CCFLAGS)

APP		= uMIPS

#no changes below please

OBJS	= $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SOURCES)))

$(APP): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

loader.inc: ../romboot/loader.bin Makefile
	xxd -i $< | grep 0x > $@

soc_stm.c: loader.inc

%.o : %.c Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

%.o : %.S Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(APP) $(OBJS)

%.bin: %
	arm-none-eabi-objcopy -O binary $< $@ -j.text -j.rodata -j.data -j.vectors

test: uMIPS.bin
	sudo CortexProg power on write $< trace $(ZWT_ADDR) info

trace:
	sudo CortexProg power on trace $(ZWT_ADDR) info
