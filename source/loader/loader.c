#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "printf.h"
#include "entry.h"
#include "fat.h"

#define MY_SYS_TYPE			0x00010000	//DS2100/3100
#define STRINGIFY2(x)		#x
#define STRINGIFY(x)		STRINGIFY2(x)
#define MY_SYS_TYPE_STR		STRINGIFY(MY_SYS_TYPE)

#define MIN_SAFE_ADDR		0x80004000	//based on our linker script

struct PartitionEntry {
	uint8_t active;
	uint8_t chsStart[3];
	uint8_t type;
	uint8_t chsEnd[3];
	uint32_t lbaStart;
	uint32_t lbaLen;
} __attribute__((packed));

struct Mbr {
	uint8_t crap[0x1be];
	struct PartitionEntry part[4];
	uint16_t sig;	//aa55
} __attribute__((packed));

struct ElfHeader {
	uint8_t		e_ident[16];
	uint16_t	e_type;
	uint16_t	e_machine;
	uint32_t	e_version;
	uint32_t	e_entry;
	uint32_t	e_phoff;		// -> program header table offset
	uint32_t	e_shoff;		// -> ElfSection[]
	uint32_t	e_flags;
	uint16_t	e_ehsize;		//this header's size in bytes
	uint16_t	e_phentsize;	//size of program header table entries
	uint16_t	e_phnum;		//number of entries in program header table
	uint16_t	e_shentsize;	//sizeof(ElfSection) (size of entry of section table
	uint16_t	e_shnum;		//num sections in section table
	uint16_t	e_shstrndx;		//section header table index of the entry associated with the section name string table. If the file has no section name string table, this member holds the value SHN_UNDEF. See ‘‘Sections’’ and ‘‘String Table’’ below for more information.
} __attribute__((packed));

#define EI_MAG0         0               /* e_ident[] indexes */
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_PAD          7

#define ELFMAG0         0x7F
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'

#define ELFCLASS32      1
#define ELFCLASS64      2

#define ELFDATA2LSB     1
#define ELFDATA2MSB     2

#define ET_EXEC   		2

#define EM_MIPS         8 
#define EM_MIPS_RS4_BE  10

#define EV_CURRENT      1


struct ElfProgHdr{
	uint32_t	p_type;
	uint32_t	p_offset;
	uint32_t	p_vaddr;
	uint32_t	p_paddr;
	uint32_t	p_filesz;
	uint32_t	p_memsz;
	uint32_t	p_flags;
	uint32_t	p_align;
} __attribute__((packed));


#define PHF_EXEC		0x1
#define PHF_WRITE		0x2
#define PHF_READ		0x4

#define PT_NULL			0x0
#define PT_LOAD			0x1
#define PT_DYNAMIC		0x2
#define PT_INTERP		0x3
#define PT_NOTE			0x4
#define PT_SHLIB		0x5
#define PT_PHDR			0x6

struct CoffHdr {
	
};

struct DecMemBitmap {
	uint32_t pgSz;
	uint8_t bmp[];
};

struct DecPromVectors {
	uint32_t padding0[9];
	int (*getchar)(void);								//0x24
	uint32_t padding1[2];
	void (*printf)(const char * fmt, ...);				//0x30
	uint32_t padding2[12];
	const char* (*getenv)(const char *);				//0x64
	uint32_t padding3[1];
	uint32_t* (*slotaddr)(int32_t slot);				//0x6c
	uint32_t padding4[3];
	void (*clearcache)(void);							//0x7c
	uint32_t (*getsysid)(void);							//0x80
	uint32_t (*getmembitmap)(struct DecMemBitmap *out);	//0x84	returns bmp size
	uint32_t padding5[7];
	const void* (*gettcinfo)(void);						//0xa4
};



typedef void (*KernelEntry)(int argc, const char **argv, uint32_t magic, const struct DecPromVectors *vecs);



void prPutchar(char chr)
{
	consoleWrite(chr);
}

static void* v2p(void* addr)
{
	return (void*)(((uintptr_t)addr) & 0x1fffffff);	//assumes a lot of things :D
}

static bool loaderPrvFatReadSecProc(void *userData, uint32_t sec, void *dst)
{
	uint32_t startSec = *(const uint32_t*)userData;
	
	return readblock(sec + startSec, v2p(dst));
}

static void fatal(const char *str)
{
	pr(str);
	while(1);
}

static void fileRead(struct FatFile *file, void* dst, uint32_t len)
{
	if (!fatFileRead(file, dst, len))
		fatal("File read unexpectedly failed\n");
}

static void fileSeek(struct FatFile *file, uint32_t pos)
{
	if (!fatFileSeek(file, pos))
		fatal("File seek unexpectedly failed\n");
}

static uint32_t promGetMemBitmap(struct DecMemBitmap *out)
{
	//linux will only use complete bytes so we need to unpack bits into bytes
	//output is also num bytes which nicely matches our "num bits" input
	//issues come up if our "eachBitSz" is not divisible by 8. then we lose memory
	//i can live with that
	
	uint32_t nBits = getMemMap(0), eachBitSz = getMemMap(1);
	uint8_t *outPtr = out->bmp;
	uint32_t i;

	out->pgSz = eachBitSz / 8;
	
	for (i = 0; i < nBits; i++) {
		
		*outPtr++ = -((getMemMap(2 + i / 8) >> (i % 8)) & 1);	//negation is faster than ternary operator :D
	}
	
	return nBits;
}

static const char* promGetenv(const char *envVar)
{
	if (!strcmp(envVar, "systype"))
		return MY_SYS_TYPE_STR;
	
	pr("got asked for unknown env type '%s', ", envVar);
	fatal("not sure how to reply\n");
}

static int promGetchar(void)
{
	return -1;
}

static uint32_t* promSlotAddr(int32_t slot)
{
	return NULL;	//no turbochannel slots here....
}

static void promClearCache(void)
{
	//nothing
}

static uint32_t promGetSysId(void)
{
	return MY_SYS_TYPE;
}

static const void* promGetTcInfo(void)
{
	return NULL;	//no turbochannel slots here....
}

void start(void)
{
	static const char *argv[] = {
			"vmlinux",
			"unused_arg",
			"earlyprintk=prom0",
			"console=ttyS3",
		//	"lpj=100000",
			"root=/dev/pvd3",
			"rootfstype=ext4",
			"rw",
			"init=/bin/sh"
	};
	static const struct DecPromVectors vecs = {
		.printf = pr,
		.getchar = promGetchar,
		.getenv = promGetenv,
		.slotaddr = promSlotAddr,
		.clearcache = promClearCache,
		.getsysid = promGetSysId,
		.getmembitmap = promGetMemBitmap,
		.gettcinfo = promGetTcInfo,
	};
	
	static uint8_t __attribute__((aligned(512))) mFatBuf[FAT_SECTOR_SIZE];	//align guarantees no kilobyte crossings for sector read
	uint32_t startSec = 0, fSize, entryPt, i;
	struct Mbr *mbr = (struct Mbr*)mFatBuf;
	struct FatFileOpenInfoOpaque foi, foik;
	int_fast8_t foundIdx = -1;
	struct FatFile *dir, *fil;
	struct ElfProgHdr phdr;
	struct ElfHeader ehdr;
	struct FatVolume *vol;
	uint8_t fAttr;
	char fName[13];
	
	
	pr("hello, world\n");

	if (!readblock(0, v2p(mbr)))
		fatal("Failed to read MBR\n");
	if (mbr->sig != 0xaa55)
		fatal("MBR sig missing\n");
	
	for (i = 0; i < 4; i++) {
		
		//we are looking for a partition.
		
		//must be active
		if (mbr->part[i].active != 0x80)
			continue;
		
		//must have type 0x0e (fat16)
		if (mbr->part[i].type != 0x0e)
			continue;
		
		//must have a sane size (1MB+)
		if (mbr->part[i].lbaLen < 1024 * 1024 / FAT_SECTOR_SIZE)
			continue;
		
		//must be the only one
		if (foundIdx < 0)
			foundIdx = i;
		else
			fatal("More than one candidate boot partition found\n");
		
		startSec = mbr->part[i].lbaStart;
	}
	
	if (foundIdx < 0)
		fatal("No candidate boot partition found\n");

	fatInit(mFatBuf);
	
	vol = fatMount(loaderPrvFatReadSecProc, &startSec);
	if (!vol)
		fatal("mount failed\n");
	
	dir = fatOpenRoot(vol);
	if (!dir)
		fatal("failed to open root dir\n");

	foundIdx = 0;
	while (fatDirEnum(dir, fName, &fAttr, &fSize, &foi)) {
		pr(" > '%12s' 0x%02x, %u bytes\n", fName, fAttr, fSize);
		
		if (!strcmp(fName, "VMLINUX")) {
			
			if (foundIdx)
				fatal("Too many candidate kernels found\n");
			foundIdx = 1;
			foik = foi;
		}
	}
	fatFileClose(dir);
	
	if (!foundIdx)
		fatal("No candidate kernels found\n");
	
	fil = fatFileOpen(vol, &foik);
	if (!fil)
		fatal("Failed to open kernel\n");
	
	pr("kernel open\n");
	
	fileRead(fil, &ehdr, sizeof(ehdr));
	
	if (ehdr.e_ident[EI_MAG0] == ELFMAG0 && ehdr.e_ident[EI_MAG1] == ELFMAG1 && ehdr.e_ident[EI_MAG2] == ELFMAG2 && ehdr.e_ident[EI_MAG3] == ELFMAG3) {
		
		if (ehdr.e_ehsize < sizeof(struct ElfHeader) || ehdr.e_phentsize < sizeof(struct ElfProgHdr))
			fatal("Kernel not a valid ELF file\n");
		
		if (ehdr.e_ident[EI_CLASS] != ELFCLASS32 || ehdr.e_ident[EI_DATA] != ELFDATA2LSB || ehdr.e_ident[EI_VERSION] != EV_CURRENT ||
				ehdr.e_type != ET_EXEC || (ehdr.e_machine != EM_MIPS && ehdr.e_machine != EM_MIPS_RS4_BE))
			fatal("Only v1 MIPS LE32 executable elf files supported\n");
		
		
		entryPt = ehdr.e_entry;
		pr("Entrypoint will be 0x%08x\n", entryPt);
		
		for (i = 0; i < ehdr.e_phnum; i++) {
			
			uint32_t now, j;
			
			fileSeek(fil, ehdr.e_phoff + i * ehdr.e_phentsize);
			fileRead(fil, &phdr, sizeof(phdr));
			
			if (phdr.p_type != PT_LOAD)
				continue;
			
			if (phdr.p_filesz > phdr.p_memsz)
				fatal("This load command is impossible\n");
			
			pr("loading 0x%08x bytes from offset 0x%08x to 0x%08x + 0x%08x...\n", phdr.p_filesz, phdr.p_offset, phdr.p_paddr, phdr.p_memsz);
			
			if (phdr.p_paddr < MIN_SAFE_ADDR)
				fatal("Cannot load this low\n");
			
			fileSeek(fil, phdr.p_offset);
			
			for (j = 0; j < phdr.p_filesz; j += now) {
				
				now = phdr.p_filesz - j;
				if (now > 32768)
					now = 32768;
				pr("\r0x%08x / 0x%08x -> %3u%%", j, phdr.p_filesz, j * 100 / phdr.p_filesz);
				fileRead(fil, (void*)(phdr.p_paddr + j), now);
			}
			pr("\nclearing BSS...\n");
			memset((void*)(phdr.p_paddr + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);
			pr("LOADED\n");
		}
	}
	else if (
	
	pr("jumping. Kernel boot should take a couple of minutes...\n");
	((KernelEntry)entryPt)(sizeof(argv) / sizeof(*argv), argv, 0x30464354, &vecs);
	
	while(1);
}
