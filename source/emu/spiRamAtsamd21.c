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


#define MIN_RAM				(6)

#define MAX_CHIP_SIZE		(16)	//due to addressing
#define MIN_CHIP_SIZE		(1)
#define MAX_NUM_CHIPS		(4)


//no matter how i play it, 64-byte lines make things slower, not faster
#define CACHE_LINE_SIZE_ORDER	5		//must be at least the size of icache line, or else...
#define CACHE_NUM_WAYS			2		//number of lines a given PA can be in
#define CACHE_NUM_SETS			20		//number of buckets of PAs


#define CACHE_LINE_SIZE			(1 << CACHE_LINE_SIZE_ORDER)
#if OPTIMAL_RAM_WR_SZ > CACHE_LINE_SIZE || OPTIMAL_RAM_RD_SZ > CACHE_LINE_SIZE
	#error "you're in for a bad time"
#endif


static uint8_t mTotalRam;


static void spiRamCacheInit(void);


static void sercomPioTestReadMax(uint_fast8_t cmdVal, uint32_t addr, uint8_t *dst, uint32_t nBytes)
{
	//nCS low
	PORT->Group[0].OUTCLR.reg = PORT_PA14;

#define SENDALL(_val)							\
		SERCOM0->SPI.DATA.reg = (_val);			\
		SERCOM2->SPI.DATA.reg = (_val);			\
		SERCOM1->SPI.DATA.reg = (_val);			\
		SERCOM3->SPI.DATA.reg = (_val)

#define CONSUME()								\
		(void)SERCOM0->SPI.DATA.reg;			\
		(void)SERCOM2->SPI.DATA.reg;			\
		(void)SERCOM1->SPI.DATA.reg;			\
		(void)SERCOM3->SPI.DATA.reg

#define WAITCONSUME()							\
		while (!SERCOM3->SPI.INTFLAG.bit.DRE);	\
		CONSUME()

	
	//we get here to a guaranteed empty buffer
	//command and wait
	SENDALL(cmdVal);
	SENDALL(addr >> 16);
	WAITCONSUME();
	SENDALL(addr >> 8);
	WAITCONSUME();
	SENDALL(addr >> 0);
	WAITCONSUME();
	SENDALL(0);
	WAITCONSUME();
	
	while (!SERCOM3->SPI.INTFLAG.bit.RXC);
		
	while (--nBytes) {
		
		//either we are here first time, but tx already started from above or a loop in which case we read a byte from rx buffer and thus have tx space
	//	while (!sercom2->SPI.INTFLAG.bit.DRE);
		SENDALL(0);
		
		*dst++ = SERCOM0->SPI.DATA.reg;
		*dst++ = SERCOM2->SPI.DATA.reg;
		*dst++ = SERCOM1->SPI.DATA.reg;
		*dst++ = SERCOM3->SPI.DATA.reg;
		
		//due to the timing of this loop, e do not need a condition check
	}
	
	while (!SERCOM3->SPI.INTFLAG.bit.RXC);
	*dst++ = SERCOM0->SPI.DATA.reg;
	*dst++ = SERCOM2->SPI.DATA.reg;
	*dst++ = SERCOM1->SPI.DATA.reg;
	*dst++ = SERCOM3->SPI.DATA.reg;
	
	PORT->Group[0].OUTSET.reg = PORT_PA14;
}

static void sercomPioTestWriteMax(uint_fast8_t cmdVal, uint32_t addr, const uint8_t *src, uint32_t nBytes)
{	
	//nCS low
	PORT->Group[0].OUTCLR.reg = PORT_PA14;
	
	//we get here to guaranteed empty buffer
	//command and wait
	SENDALL(cmdVal);
	SENDALL(addr >> 16);
	while (!SERCOM3->SPI.INTFLAG.bit.DRE);
	SENDALL(addr >> 8);
	while (!SERCOM3->SPI.INTFLAG.bit.DRE);
	SENDALL(addr >> 0);
	
	while (nBytes--) {
		
		while (!SERCOM3->SPI.INTFLAG.bit.DRE);
		SERCOM0->SPI.DATA.reg = *src++;
		SERCOM2->SPI.DATA.reg = *src++;
		SERCOM1->SPI.DATA.reg = *src++;
		SERCOM3->SPI.DATA.reg = *src++;
	}
	while (!SERCOM3->SPI.INTFLAG.bit.TXC);
	CONSUME();
	CONSUME();
	CONSUME();
	
	PORT->Group[0].OUTSET.reg = PORT_PA14;
}

static void spiRamPrvCachelineReadQuadChannel(uint32_t addr, void *dstP)
{
	sercomPioTestReadMax(0x03, addr / 4, dstP, CACHE_LINE_SIZE / 4);
}

static void spiRamPrvCachelineWriteQuadChannel(uint32_t addr, const void *srcP)
{
	sercomPioTestWriteMax(0x02, addr / 4, srcP, CACHE_LINE_SIZE / 4);
}

bool __attribute__((noinline)) spiRamInit(void)
{
	#define SIZEOF_ID		8
		
	uint8_t id[MAX_NUM_CHIPS * SIZEOF_ID] = {};
	uint8_t szs[MAX_NUM_CHIPS] = {};
	uint_fast8_t i, j;
	
	sercomPioTestReadMax(0x9f, 0, id, SIZEOF_ID);

	pr("RAMs:\n");
	for (i = 0; i < MAX_NUM_CHIPS; i++) {
		
		const char *manuf = 0;
		uint8_t szBase;
		
		pr("   ");
		for (j = 0; j < SIZEOF_ID; j++)
			pr("%02x ", id[i + MAX_NUM_CHIPS * j]);
		
		if (id[i + MAX_NUM_CHIPS * 1]) switch (id[i + MAX_NUM_CHIPS * 0]) {
			case 0x0d:
				manuf = "AP Memory";
				szBase = 2;
				break;
			
			case 0x9d:
				manuf = "ISSI";
				szBase = 1;
				break;
		}
		
		if (!manuf)
			pr("(unrecognized device");
		else {
			uint_fast16_t sz = szBase << (id[i + MAX_NUM_CHIPS * 2] >> 5);
			
			pr(" (%uMB by '%s'", sz, manuf);
			
			if (sz > MAX_CHIP_SIZE)
				pr(" - too big");
			else if (sz < MIN_CHIP_SIZE)
				pr(" - too small");
			else
				szs[i] = sz;
		}
		pr(")\n", szs[i]);
	}
	
	//I used to support multiple ram configs, but it was a pain in my ass
	// so i don't anymore. so sue me!
	if (!szs[0] || !szs[1] || !szs[2] || !szs[3]) {
		
		pr("Cannot run with no RAM0-3 populated\n");
		return false;
	}
	else {
		
		mTotalRam = szs[0];
		if (mTotalRam > szs[1])
			mTotalRam = szs[1];
		if (mTotalRam > szs[2])
			mTotalRam = szs[2];
		if (mTotalRam > szs[3])
			mTotalRam = szs[3];
		
		mTotalRam *= 4;
	}
	
	if (mTotalRam < MIN_RAM) {
		pr("RAM works, but there is too little of it. We have %uMB, need %uMB min\n", mTotalRam, MIN_RAM);
		return false;
	}
	
	pr("Will run with %uMB of RAM\n", mTotalRam);
	
	spiRamCacheInit();
	return true;
}

const uint8_t* spiRamGetMap(uint32_t *nBitsP, uint32_t *eachBitSzP)
{
	static uint8_t mMap[(MAX_CHIP_SIZE * 2 + 7) / 8];
	uint_fast8_t i;
	
	if (!mMap[0]) {
		
		for (i = 0; i < mTotalRam / 8; i++)
			mMap[i] = 0xff;
		
		if (mTotalRam % 8)
			mMap[i++] = (1 << (mTotalRam % 8)) - 1;
		
		for (;i < sizeof(mMap); i++)
			mMap[i] = 0x00;
	}
	
	*eachBitSzP = 1 << 20;
	*nBitsP = sizeof(mMap) * 8;
	return mMap;
}

///cache




struct CacheLine {
	uint32_t	dirty	:  1;
	uint32_t	addr	: 31;
	union {
		uint32_t dataW[CACHE_LINE_SIZE / sizeof(uint32_t)];
		uint16_t dataH[CACHE_LINE_SIZE / sizeof(uint16_t)];
		uint8_t dataB[CACHE_LINE_SIZE / sizeof(uint8_t)];
	};
};

struct CacheSet {
	struct CacheLine line[CACHE_NUM_WAYS];
};

struct Cache {
	struct CacheSet set[CACHE_NUM_SETS];
};

static struct Cache mCache;

static void spiRamCacheInit(void)
{
	uint_fast16_t way, set;
	
	for (set = 0; set < CACHE_NUM_SETS; set++) {
		for (way = 0; way < CACHE_NUM_WAYS; way++) {
			
			mCache.set[set].line[way].addr = 0x7fffffff;	//definitely invalid
		}
	}
}

static uint_fast16_t spiRamCachePrvHash(uint32_t addr)
{
	#if (CACHE_NUM_SETS & (CACHE_NUM_SETS - 1))
	
		#if CACHE_NUM_SETS == 3
			#define RECIP 	0xaaab
			#define SHIFT	17
		#elif CACHE_NUM_SETS == 5
			#define RECIP 	0xcccd
			#define SHIFT	18
		#elif CACHE_NUM_SETS == 6
			#define RECIP 	0xaaab
			#define SHIFT	18
		#elif CACHE_NUM_SETS == 9
			#define RECIP 	0xe38f
			#define SHIFT	19
		#elif CACHE_NUM_SETS == 10
			#define RECIP 	0xcccd
			#define SHIFT	19
		#elif CACHE_NUM_SETS == 11
			#define RECIP 	0xba2f
			#define SHIFT	19
		#elif CACHE_NUM_SETS == 12
			#define RECIP 	0xaaab
			#define SHIFT	19
		#elif CACHE_NUM_SETS == 13
			#define RECIP 	0x9d8a
			#define SHIFT	19
		#elif CACHE_NUM_SETS == 15
			#define RECIP 	0x8889
			#define SHIFT	19
		#elif CACHE_NUM_SETS == 18
			#define RECIP 	0xe38f
			#define SHIFT	20
		#elif CACHE_NUM_SETS == 20
			#define RECIP 	0xcccd
			#define SHIFT	20
		#elif CACHE_NUM_SETS == 22
			#define RECIP 	0xba2f
			#define SHIFT	20
		#elif CACHE_NUM_SETS == 24
			#define RECIP 	0xaaab
			#define SHIFT	20
		#elif CACHE_NUM_SETS == 26
			#define RECIP 	0x9d8a
			#define SHIFT	20
		#elif CACHE_NUM_SETS == 30
			#define RECIP 	0x8889
			#define SHIFT	20
		#else
			#error "we lack a reciprocal for this value - it will be slow. refusing!"
		#endif
		
		uint32_t div;
		
		addr /= CACHE_LINE_SIZE;
		addr = (uint16_t)addr;
		
		div = addr * RECIP;
		div >>= SHIFT;
		addr -= div * CACHE_NUM_SETS;
		
	#else
	
		addr /= CACHE_LINE_SIZE;
		addr %= CACHE_NUM_SETS;
	
	#endif
	
	return addr;
}

static uint_fast16_t spiRamCachePrvPickVictim(struct Cache *cache)
{
	return SysTick->VAL % CACHE_NUM_WAYS;
}

static struct CacheLine* spiRamCachePrvFillLine(struct Cache *cache, struct CacheSet *set, uint32_t addr, bool loadFromRam)
{
	uint_fast16_t idx = spiRamCachePrvPickVictim(cache);
	struct CacheLine *line = &set->line[idx];
	
//	pr("picked victim way %u currently holding addr 0x%08x, %s\n", idx, line->addr * CACHE_LINE_SIZE, line->dirty ? "DIRTY" : "CLEAN");
	if (line->dirty) {	//clean line
		

//		pr(" flushing -> %08x\n", line->addr * CACHE_LINE_SIZE);
		spiRamPrvCachelineWriteQuadChannel(line->addr * CACHE_LINE_SIZE, line->dataW);
	}
	
	if (loadFromRam) {
//		uint32_t *dst = line->dataW;

		spiRamPrvCachelineReadQuadChannel(addr / CACHE_LINE_SIZE * CACHE_LINE_SIZE, line->dataW);
		
//		pr(" filled %08x -> %08x %08x %08x %08x %08x %08x %08x %08x\n", addr, 
//				dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
	}
	line->dirty = 0;
	line->addr = addr / CACHE_LINE_SIZE;
	
	return line;
}

#if CACHE_NUM_WAYS == 2
//this code hardwired for 2 ways, can be adjusted

	#if CACHE_NUM_SETS == 24
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0xaaab				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #20					\n\t"	\
			"	movs  r3, #24					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 22
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0xba2f				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #20					\n\t"	\
			"	movs  r3, #22					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 20
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0xcccd				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #20					\n\t"	\
			"	movs  r3, #20					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 18
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0xe38f				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #20					\n\t"	\
			"	movs  r3, #18					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 16
		#define HASH_N_STUFF							\
			"   lsls  r4, #28					\n\t"	\
			"   lsrs  r4, #28					\n\t"
	#elif CACHE_NUM_SETS == 15
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0x8889				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #19					\n\t"	\
			"	movs  r3, #15					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 13
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0x9d8a				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #19					\n\t"	\
			"	movs  r3, #13					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 12
		#define HASH_N_STUFF							\
			"	uxth  r4, r4					\n\t"	\
			"	ldr   r5, =0xaaab				\n\t"	\
			"	muls  r5, r4					\n\t"	\
			"	lsrs  r5, #19					\n\t"	\
			"	movs  r3, #12					\n\t"	\
			"	muls  r3, r5					\n\t"	\
			"	subs  r4, r3					\n\t"
	#elif CACHE_NUM_SETS == 8
		#define HASH_N_STUFF							\
			"   lsls  r4, #29					\n\t"	\
			"   lsrs  r4, #28					\n\t"
	#else
	
		#error "bad setting for fast path"
	#endif




	void __attribute__((naked, noinline)) spiRamRead(uint32_t addr, void *dataP, uint_fast16_t sz)
	{
		asm volatile(
			".syntax unified					\n\t"
			"	push  {r4, r5}					\n\t"
			"	lsrs  r4, r0, %2				\n\t"	//r4 = addr / CACHE_LINE_SIZE
			
			HASH_N_STUFF
			
			"	ldr   r3, =%0					\n\t"
			"	ldr   r5, =%3 * 2 + 4 * 2		\n\t"	//size of each set
			"	muls  r4, r5					\n\t"
			"	adds  r4, r3					\n\t"	//points to proper set
			"	ldmia r4!, {r5}					\n\t"	//get info[0], increment past it
			"	lsrs  r3, r0, %2				\n\t"	//addr to compare to
			"	lsrs  r5, #1					\n\t"	//hide dirty bit
			"	cmp   r3, r5					\n\t"
			"	beq   1f						\n\t"
			"	ldr   r5, [r4, %3]				\n\t"	//get info [1]
			"	lsrs  r5, #1					\n\t"	//hide dirty bit
			"	cmp   r3, r5					\n\t"
			"	bne   2f						\n\t"	//not found
			//found in set 2
			"	adds  r4, %3 + 4				\n\t"
			"1:									\n\t"	//common path for "found"
			"	lsls  r0, %4					\n\t"
			"	lsrs  r0, %4					\n\t"
			"	cmp   r2, #4					\n\t"
			"	bne   5f						\n\t"
			"	ldr   r5, [r4, r0]				\n\t"
			"	str   r5, [r1]					\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #1					\n\t"
			"	bne   5f						\n\t"
			"	ldrb  r5, [r4, r0]				\n\t"
			"	strb  r5, [r1]					\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #2					\n\t"
			"	bne   5f						\n\t"
			"	ldrh  r5, [r4, r0]				\n\t"
			"	strh  r5, [r1]					\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #32					\n\t"
			"	bne   5f						\n\t"
			"	adds  r4, r0					\n\t"
			"	ldmia r4!, {r0, r2, r3, r5}		\n\t"
			"	stmia r1!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r4!, {r0, r2, r3, r5}		\n\t"
			"	stmia r1!, {r0, r2, r3, r5}		\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #64					\n\t"
			"	bne   5f						\n\t"
			"	adds  r4, r0					\n\t"
			"	ldmia r4!, {r0, r2, r3, r5}		\n\t"
			"	stmia r1!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r4!, {r0, r2, r3, r5}		\n\t"
			"	stmia r1!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r4!, {r0, r2, r3, r5}		\n\t"
			"	stmia r1!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r4!, {r0, r2, r3, r5}		\n\t"
			"	stmia r1!, {r0, r2, r3, r5}		\n\t"
			"	b     4f						\n\t"
	
			"5: 								\n\t"		//not supported - use slow path
			"	pop   {r4, r5}					\n\t"
			"	b     3f						\n\t"
			
			"4:									\n\t"
			"	pop   {r4, r5}					\n\t"
			"	bx    lr						\n\t"
			"2:									\n\t"	//not found
			"	pop   {r4, r5}					\n\t"
			"	cmp   r2, %3					\n\t"
			"	bne   3f						\n\t"	//slow path
			//direct read path					\n\t"
			"	ldr   r3, =%1					\n\t"
			"	bx    r3						\n\t"
			"3:									\n\t"	//slow
			"	ldr   r3, =spiRamRead_slowpath	\n\t"
			"	bx    r3						\n\t"
			:
			:"i"(mCache.set), "i"(&spiRamPrvCachelineReadQuadChannel), "i"(CACHE_LINE_SIZE_ORDER), "i"(CACHE_LINE_SIZE), "i"(32 - CACHE_LINE_SIZE_ORDER)
		);
	}
	
	
	void __attribute__((used)) spiRamRead_slowpath(uint32_t addr, void *dataP, uint_fast16_t sz)
	{
		struct Cache *cache = &mCache;
		struct CacheSet *set = &cache->set[spiRamCachePrvHash(addr)];
		uint32_t *dptr, *dst = (uint32_t*)dataP, dummy1, dummy2;
		struct CacheLine *line; 
	
	//	pr("slow read %u @ 0x%08x -> set %u\n", sz, addr, set - &cache->set[0]);
		
	//	pr("fill\n", sz, addr);
		line = spiRamCachePrvFillLine(cache, set, addr, true);
	
		
		switch (sz) {
			case 1:
				*(uint8_t*)dataP = line->dataB[(addr % CACHE_LINE_SIZE) / sizeof(uint8_t)];
	//			pr("read %u @ 0x%08x idx %u -> %02x\n", sz, addr, line - &set->line[0], *(uint8_t*)dataP);
				break;
			
			case 2:
				*(uint16_t*)dataP = line->dataH[(addr % CACHE_LINE_SIZE) / sizeof(uint16_t)];
	//			pr("read %u @ 0x%08x idx %u -> %04x\n", sz, addr, line - &set->line[0], *(uint16_t*)dataP);
				break;
			
			case 4:
				*(uint32_t*)dataP = line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
	//			pr("read %u @ 0x%08x idx %u -> %08x\n", sz, addr, line - &set->line[0], *(uint32_t*)dataP);
				break;
			
			case 8:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1}	\n"
					"	stmia %1!, {r0, r1}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1]);
				break;
			
			case 16:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1", "r2", "r3"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1], dst[2], dst[3]);
				break;
	
	#if CACHE_LINE_SIZE >= 32
			case 32:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1", "r2", "r3"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
				break;
	#endif
	#if CACHE_LINE_SIZE >= 64
			case 64:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1", "r2", "r3"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7],
	//				dst[8], dst[9], dst[10], dst[11], dst[12], dst[13], dst[14], dst[15]);
				break;
	#endif
	
			default:
				pr("unknown size %u\n", sz);
				while(1);
		}
	}

	void __attribute__((naked, noinline)) spiRamWrite(uint32_t addr, const void *dataP, uint_fast16_t sz)
	{
		asm volatile(
			".syntax unified					\n\t"
			"	push  {r4, r5}					\n\t"
			"	lsrs  r4, r0, %2				\n\t"	//r4 = addr / CACHE_LINE_SIZE
			
			HASH_N_STUFF
			
			"	ldr   r3, =%0					\n\t"
			"	ldr   r5, =%3 * 2 + 4 * 2		\n\t"	//size of each set
			"	muls  r4, r5					\n\t"
			"	adds  r4, r3					\n\t"	//points to proper set
			"	ldmia r4!, {r5}					\n\t"	//get info[0], increment past it
			"	lsrs  r3, r0, %2				\n\t"	//addr to compare to
			"	lsrs  r5, #1					\n\t"	//hide dirty bit
			"	cmp   r3, r5					\n\t"
			"	beq   1f						\n\t"
			"	ldr   r5, [r4, %3]				\n\t"	//get info [1]
			"	lsrs  r5, #1					\n\t"	//hide dirty bit
			"	cmp   r3, r5					\n\t"
			"	bne   2f						\n\t"	//not found
			//found in set 2
			"	adds  r4, %3 + 4				\n\t"
			"1:									\n\t"	//common path for "found"
			"	lsls  r0, %4					\n\t"
			"	lsrs  r0, %4					\n\t"
			"	cmp   r2, #4					\n\t"
			"	bne   5f						\n\t"
			"	ldr   r5, [r1]					\n\t"
			"	str   r5, [r4, r0]				\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #1					\n\t"
			"	bne   5f						\n\t"
			"	ldrb  r5, [r1]					\n\t"
			"	strb  r5, [r4, r0]				\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #2					\n\t"
			"	bne   5f						\n\t"
			"	ldrh  r5, [r1]					\n\t"
			"	strh  r5, [r4, r0]				\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #32					\n\t"
			"	bne   5f						\n\t"
			"	mov   r12, r4					\n\t"
			"	adds  r4, r0					\n\t"
			"	ldmia r1!, {r0, r2, r3, r5}		\n\t"
			"	stmia r4!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r1!, {r0, r2, r3, r5}		\n\t"
			"	stmia r4!, {r0, r2, r3, r5}		\n\t"
			"	mov   r4, r12					\n\t"
			"	b     4f						\n\t"
			"5:									\n\t"
			"	cmp   r2, #64					\n\t"
			"	bne   5f						\n\t"
			"	mov   r12, r4					\n\t"
			"	adds  r4, r0					\n\t"
			"	ldmia r1!, {r0, r2, r3, r5}		\n\t"
			"	stmia r4!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r1!, {r0, r2, r3, r5}		\n\t"
			"	stmia r4!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r1!, {r0, r2, r3, r5}		\n\t"
			"	stmia r4!, {r0, r2, r3, r5}		\n\t"
			"	ldmia r1!, {r0, r2, r3, r5}		\n\t"
			"	stmia r4!, {r0, r2, r3, r5}		\n\t"
			"	mov   r4, r12					\n\t"
			"	b     4f						\n\t"
	
			"5: 								\n\t"		//not supported - use slow path
			"	pop   {r4, r5}					\n\t"
			"	b     3f						\n\t"
			
			"4:									\n\t"		//mark as dirty
			"	subs  r4, #4					\n\t"
			"	ldr   r0, [r4]					\n\t"
			"	movs  r1, #1					\n\t"
			"	orrs  r0, r1					\n\t"
			"	str   r0, [r4]					\n\t"
			
			"	pop   {r4, r5}					\n\t"
			"	bx    lr						\n\t"
			"2:									\n\t"	//not found
			"	pop   {r4, r5}					\n\t"
			"	cmp   r2, %3					\n\t"
			"	bne   3f						\n\t"	//slow path
			//direct read path					\n\t"
			"	ldr   r3, =%1					\n\t"
			"	bx    r3						\n\t"
			"3:									\n\t"	//slow
			"	ldr   r3, =spiRamWrite_slowpath	\n\t"
			"	bx    r3						\n\t"
			:
			:"i"(mCache.set), "i"(&spiRamPrvCachelineWriteQuadChannel), "i"(CACHE_LINE_SIZE_ORDER), "i"(CACHE_LINE_SIZE), "i"(32 - CACHE_LINE_SIZE_ORDER)
			:"memory", "cc"
		);
	}
	

	void __attribute__((used)) spiRamWrite_slowpath(uint32_t addr, const void *dataP, uint_fast16_t sz)
	{
		struct Cache *cache = &mCache;
		struct CacheSet *set = &cache->set[spiRamCachePrvHash(addr)];
		const uint32_t *src = (const uint32_t*)dataP;
		uint32_t dummy1, dummy2;
		struct CacheLine *line; 
		uint32_t *dptr;
		
		
	//	pr("slow write %u @ 0x%08x -> set %u\n", sz, addr, set - cache->set);
		
		//not found
	//	pr("fill\n", sz, addr);
		line = spiRamCachePrvFillLine(cache, set, addr, sz != CACHE_LINE_SIZE);
		
		switch (sz) {
			case 1:
	//			pr("write %u @ 0x%08x idx %u <- %02x\n", sz, addr, line - &set->line[0], *(uint8_t*)dataP);
				line->dataB[(addr % CACHE_LINE_SIZE) / sizeof(uint8_t)] = *(uint8_t*)dataP;
				break;
			
			case 2:
	//			pr("write %u @ 0x%08x idx %u <- %04x\n", sz, addr, line - &set->line[0], *(uint16_t*)dataP);
				line->dataH[(addr % CACHE_LINE_SIZE) / sizeof(uint16_t)] = *(uint16_t*)dataP;
				break;
			
			case 4:
	//			pr("write %u @ 0x%08x idx %u <- %08x\n", sz, addr, line - &set->line[0], *(uint32_t*)dataP);
				line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)] = *(uint32_t*)dataP;
				break;
			
			case 8:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1}	\n"
					"	stmia %1!, {r0, r1}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1"
				);
				break;
			
			case 16:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1], src[2], src[3]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1", "r2", "r3"
				);
				break;
	#if CACHE_LINE_SIZE >= 32
			case 32:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1", "r2", "r3"
				);
				break;
	#endif
	#if CACHE_LINE_SIZE >= 64
			case 64:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
	//				src[8], src[9], src[10], src[11], src[12], src[13], src[14], src[15]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1", "r2", "r3"
				);
				break;
	#endif
			default:
				pr("unknown size %u\n", sz);
				while(1);
		}
		line->dirty = 1;
	}


#else
	
	#warning "using slow path";
	
	void spiRamRead(uint32_t addr, void *dataP, uint_fast16_t sz)	//assume not called with zero
	{
		struct Cache *cache = &mCache;
		struct CacheSet *set = &cache->set[spiRamCachePrvHash(addr)];
		uint32_t *dptr, *dst = (uint32_t*)dataP, dummy1, dummy2;
		struct CacheLine *line; 
		uint_fast16_t i;
	
	//	pr("read %u @ 0x%08x -> set %u\n", sz, addr, set - &cache->set[0]);
		
		for (i = 0, line = &set->line[0]; i < CACHE_NUM_WAYS; i++, line++) {
			
			if (line->addr == addr / CACHE_LINE_SIZE)
				goto found;
		}
		
		
		//not found
		
		if (sz == CACHE_LINE_SIZE) {
			
			//no point allocating it
			spiRamPrvCachelineReadQuadChannel(addr, dataP);
			return;
		}
			
	//	pr("fill\n", sz, addr);
		line = spiRamCachePrvFillLine(cache, set, addr, true);
	
	found:
		
		switch (sz) {
			case 1:
				*(uint8_t*)dataP = line->dataB[(addr % CACHE_LINE_SIZE) / sizeof(uint8_t)];
	//			pr("read %u @ 0x%08x idx %u -> %02x\n", sz, addr, line - &set->line[0], *(uint8_t*)dataP);
				break;
			
			case 2:
				*(uint16_t*)dataP = line->dataH[(addr % CACHE_LINE_SIZE) / sizeof(uint16_t)];
	//			pr("read %u @ 0x%08x idx %u -> %04x\n", sz, addr, line - &set->line[0], *(uint16_t*)dataP);
				break;
			
			case 4:
				*(uint32_t*)dataP = line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
	//			pr("read %u @ 0x%08x idx %u -> %08x\n", sz, addr, line - &set->line[0], *(uint32_t*)dataP);
				break;
			
			case 8:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1}	\n"
					"	stmia %1!, {r0, r1}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1]);
				break;
			
			case 16:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1", "r2", "r3"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1], dst[2], dst[3]);
				break;
	
	#if CACHE_LINE_SIZE >= 32
			case 32:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1", "r2", "r3"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7]);
				break;
	#endif
	#if CACHE_LINE_SIZE >= 64
			case 64:
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(dptr), "1"(dst)
					:"memory", "r0", "r1", "r2", "r3"
				);
	//			pr("read %u @ 0x%08x idx %u -> %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				dst[0], dst[1], dst[2], dst[3], dst[4], dst[5], dst[6], dst[7],
	//				dst[8], dst[9], dst[10], dst[11], dst[12], dst[13], dst[14], dst[15]);
				break;
	#endif
	
			default:
				pr("unknown size %u\n", sz);
				while(1);
		}
	}

	void spiRamWrite(uint32_t addr, const void *dataP, uint_fast16_t sz)
	{
		struct Cache *cache = &mCache;
		struct CacheSet *set = &cache->set[spiRamCachePrvHash(addr)];
		const uint32_t *src = (const uint32_t*)dataP;
		uint32_t dummy1, dummy2;
		struct CacheLine *line; 
		uint32_t *dptr;
		uint_fast16_t i;
		
		
	//	pr("write %u @ 0x%08x -> set %u\n", sz, addr, set - cache->set);
		
		for (i = 0, line = &set->line[0]; i < CACHE_NUM_WAYS; i++, line++) {
			
			if (line->addr == addr / CACHE_LINE_SIZE)
				goto found;
		}
		
		
		if (sz == CACHE_LINE_SIZE)	//missed writes of cache line size or more
			return spiRamPrvCachelineWriteQuadChannel(addr, dataP);
			
		//not found
	//	pr("fill\n", sz, addr);
		line = spiRamCachePrvFillLine(cache, set, addr, sz != CACHE_LINE_SIZE);
	
	found:
		
		switch (sz) {
			case 1:
	//			pr("write %u @ 0x%08x idx %u <- %02x\n", sz, addr, line - &set->line[0], *(uint8_t*)dataP);
				line->dataB[(addr % CACHE_LINE_SIZE) / sizeof(uint8_t)] = *(uint8_t*)dataP;
				break;
			
			case 2:
	//			pr("write %u @ 0x%08x idx %u <- %04x\n", sz, addr, line - &set->line[0], *(uint16_t*)dataP);
				line->dataH[(addr % CACHE_LINE_SIZE) / sizeof(uint16_t)] = *(uint16_t*)dataP;
				break;
			
			case 4:
	//			pr("write %u @ 0x%08x idx %u <- %08x\n", sz, addr, line - &set->line[0], *(uint32_t*)dataP);
				line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)] = *(uint32_t*)dataP;
				break;
			
			case 8:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1}	\n"
					"	stmia %1!, {r0, r1}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1"
				);
				break;
			
			case 16:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1], src[2], src[3]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1", "r2", "r3"
				);
				break;
	#if CACHE_LINE_SIZE >= 32
			case 32:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1", "r2", "r3"
				);
				break;
	#endif
	#if CACHE_LINE_SIZE >= 64
			case 64:
	//			pr("write %u @ 0x%08x idx %u <- %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", sz, addr, line - &set->line[0], 
	//				src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
	//				src[8], src[9], src[10], src[11], src[12], src[13], src[14], src[15]);
				dptr = &line->dataW[(addr % CACHE_LINE_SIZE) / sizeof(uint32_t)];
				asm volatile(
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					"	ldmia %0!, {r0, r1, r2, r3}	\n"
					"	stmia %1!, {r0, r1, r2, r3}	\n"
					:"=l"(dummy1), "=l"(dummy2)
					:"0"(src), "1"(dptr)
					:"memory", "r0", "r1", "r2", "r3"
				);
				break;
	#endif
			default:
				pr("unknown size %u\n", sz);
				while(1);
		}
		line->dirty = 1;
	}

#endif


