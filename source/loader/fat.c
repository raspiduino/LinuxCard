#include <string.h>
#include <stdio.h>
#include "fat.h"

#define MAX_VOLUMES				1
#define MAX_FILES				1

struct FatVolume {
	FatReadSecProc readSecF;
	void *readSecD;
	
	uint32_t fatSec;
	uint32_t dataStart;		//data cluster start
	uint32_t numClusters;	//actually, this is the number of the fist nonexistent cluster
	
	union {
		struct {
			uint32_t rootDirSec;
			uint16_t rootDirNumSec;
		};
		uint32_t rootDirClus;
	};
	
	uint8_t secPerClus, fatSz;
};

struct FatFile {	//without packing, gcc insists on making this a word too big
	struct FatVolume *vol;
	union {
		struct {
			uint32_t firstSec, numSec, curSec;
			uint16_t inSecOfst;
		} rootDir;
		struct {
			uint32_t size, firstClus, curClus, clusIdx;
			uint16_t inClusOfst;
		} normal;
	};
	uint8_t isFile				: 1;
	uint8_t isSpecialRootDir	: 1;
};

struct FatDirEntry {
	uint8_t name[11];
	uint8_t attr, rfu, crTimeCentisecs;
	uint16_t crTime, crDate, accDate, clusHi, modTime, modDate, clusLo;
	uint32_t fileSz;
};

struct FatFileOpenInfoReal {
	uint32_t clus;
	uint32_t size;
	uint8_t attr;
};

static uint8_t mVolumesUsed = ~((1 << MAX_VOLUMES) - 1);
static uint8_t mFilesUsed = ~((1 << MAX_FILES) - 1);
static struct FatVolume mVolumes[MAX_VOLUMES];
static struct FatFile mFiles[MAX_FILES];
//buffer into
static uint8_t *mBuffer;
static struct FatVolume *mBufVol;
static uint32_t mBufSec;


static int_fast8_t fatPrvFindFreeStruct(uint8_t *maskP)
{
	uint_fast8_t i, mask = 1;
	
	for (i = 0; i < 8; i++, mask <<= 1) {
		if ((*maskP) & mask)
			continue;
		
		*maskP |= mask;
		
		return i;
	}
	
	return -1;
}

void fatInit(void *tmpBuf)
{
	mBuffer = tmpBuf;
	mBufVol = 0;
}

static bool fatPrvVolReadEx(struct FatVolume *vol, uint32_t sec, void* dst)		//read a sector to a buffer. consider cache. 
{
	if (mBufVol == vol && mBufSec == sec) {	//cache :D
		
		if (dst != (void*)mBuffer)
			memcpy(dst, mBuffer, FAT_SECTOR_SIZE);
		
		return true;
	}

	return vol->readSecF(vol->readSecD, sec, dst);
}

static bool fatPrvVolRead(struct FatVolume *vol, uint32_t sec)
{
	if (!fatPrvVolReadEx(vol, sec, mBuffer)) {
		
		mBufVol = NULL;
		return false;
	}
	
	mBufVol = vol;	
	mBufSec = sec;
	
	return true;
}

static uint32_t fatPrvGetFirstInvalClusNo(struct FatVolume *vol)
{
	switch (vol->fatSz) {
		case 12:
			return 0xff6;
			break;
		
		case 16:
			return 0xfff6;
			break;
		
		case 32:
			return 0x0ffffff6;
			break;
		
		default:
			__builtin_unreachable();
			return 0;
	}
}

static bool fatPrvIsValidClusNo(struct FatVolume *vol, uint32_t clus)
{
	return clus >= 2 && clus < fatPrvGetFirstInvalClusNo(vol) && clus < vol->numClusters;
}

static uint32_t fatPrvWalkFat(struct FatVolume *vol, uint32_t curClus)
{
	uint32_t ret, sec, idx;
	
	if (!fatPrvIsValidClusNo(vol, curClus))
		return 0;
	
	switch (vol->fatSz) {
		case 12:
			idx = curClus * 3 / 2;
			sec = idx / FAT_SECTOR_SIZE;
			idx %= FAT_SECTOR_SIZE;
			
			if (!fatPrvVolRead(vol, vol->fatSec + sec))
				return 0;
			ret = mBuffer[idx];
			if (idx != FAT_SECTOR_SIZE - 1)
				ret += ((uint16_t)mBuffer[idx + 1]) << 8;
			else {
				
				if (!fatPrvVolRead(vol, vol->fatSec + sec + 1))
					return 0;
				
				ret += ((uint16_t)mBuffer[0]) << 8;
			}
			if (curClus & 1)
				ret >>= 4;
			ret &= 0x0fff;
			break;
		
		case 16:
			idx = curClus * 2;
			if (!fatPrvVolRead(vol, vol->fatSec + idx / FAT_SECTOR_SIZE))
				return 0;
			
			ret = *(uint16_t*)(mBuffer + idx % FAT_SECTOR_SIZE);
			break;
		
		case 32:
			idx = curClus * 4;
			if (!fatPrvVolRead(vol, vol->fatSec + idx / FAT_SECTOR_SIZE))
				return 0;
			
			ret = *(uint32_t*)(mBuffer + idx % FAT_SECTOR_SIZE);
			break;
			
		default:
			__builtin_unreachable();
			return 0;
	}
	
	return fatPrvIsValidClusNo(vol, ret) ? ret : 0;
}

struct FatVolume* fatMount(FatReadSecProc readSecF, void *readSecD)
{
	uint32_t startSec = 0, secsPerFat, totalSec;
	struct FatVolume *vol;
	uint16_t rootDirEnts;
	uint8_t numFats;
	int_fast8_t idx;
	
	idx = fatPrvFindFreeStruct(&mVolumesUsed);
	if (idx < 0)
		return NULL;
	
	vol = mVolumes + idx;
	vol->readSecF = readSecF;
	vol->readSecD = readSecD;
	vol->fatSz = 0;
	
retry:
	if (!fatPrvVolRead(vol, startSec))
		goto fail;

	//both an MBR and a PBR need this, so lack of this is an auto-fail
	if (mBuffer[0x1fe] != 0x55 || mBuffer[0x1ff] != 0xAA)
		goto fail;

/*	xxx win10 does not produce this
	//same
	if ((mBuffer[0x00] != 0xEB || mBuffer[0x02] != 0x90) && mBuffer[0x00] != 0xE9) {
		printf("invalid jump\n");
		goto fail;
	}
*/
	
	//maybe an MBR?
	if (mBuffer[0x26] != 0x29 || mBuffer[0x36] != 'F' || mBuffer[0x37] != 'A' || mBuffer[0x38] != 'T') {
		
		if (startSec)	//we are already trying with an MBR
			goto fail;
		
		//check part type for first part
		switch (mBuffer[0x1c2]) {
			case 1:
			case 4:
			case 6:
			case 0x0b:
			case 0x0c:
			case 0x0e:
				break;
			
			default:
				goto fail;
		}
		
		startSec = (startSec << 8) + mBuffer[0x1c9];
		startSec = (startSec << 8) + mBuffer[0x1c8];
		startSec = (startSec << 8) + mBuffer[0x1c7];
		startSec = (startSec << 8) + mBuffer[0x1c6];
		goto retry;
	}
	
	//we have what looks like a PBR... check it
	
	//only proper sector sizes supported
	if (mBuffer[0x0b] != (FAT_SECTOR_SIZE & 0xff) || mBuffer[0x0c] != (FAT_SECTOR_SIZE >> 8))
		goto fail;
	
	//sec per cluster should be a power of two
	vol->secPerClus = mBuffer[0x0d];
	if (!vol->secPerClus || (vol->secPerClus & (vol->secPerClus - 1)))
		goto fail;
	
	//num fats should be sane
	numFats = mBuffer[0x10];
	if (!numFats || numFats > 2)
		goto fail;
	
	vol->fatSec = (((uint16_t)mBuffer[0x0f]) << 8) + mBuffer[0x0e];
	rootDirEnts = (((uint16_t)mBuffer[0x12]) << 8) + mBuffer[0x11];	//for fat12/16 only
	secsPerFat = (((uint16_t)mBuffer[0x17]) << 8) + mBuffer[0x16];	//for fat12/16 only
	
	totalSec = (((uint16_t)mBuffer[0x14]) << 8) + mBuffer[0x13];
	if (!totalSec) {
		
		totalSec = (totalSec << 8) + mBuffer[0x23];
		totalSec = (totalSec << 8) + mBuffer[0x22];
		totalSec = (totalSec << 8) + mBuffer[0x21];
		totalSec = (totalSec << 8) + mBuffer[0x20];
	}
	
	if (!totalSec || !vol->fatSec)
		goto fail;
	
	vol->fatSec += startSec;
	
	//no root dir entries is fat32, for others there are limitations on what the value can be
	if (!rootDirEnts && !secsPerFat) {
		
		secsPerFat = (secsPerFat << 8) + mBuffer[0x27];
		secsPerFat = (secsPerFat << 8) + mBuffer[0x26];
		secsPerFat = (secsPerFat << 8) + mBuffer[0x25];
		secsPerFat = (secsPerFat << 8) + mBuffer[0x24];
		
		if (!secsPerFat)
			goto fail;
		
		vol->fatSz = 32;
		vol->rootDirClus = 0;
		
		vol->rootDirClus = (vol->rootDirClus << 8) + mBuffer[0x2f];
		vol->rootDirClus = (vol->rootDirClus << 8) + mBuffer[0x2e];
		vol->rootDirClus = (vol->rootDirClus << 8) + mBuffer[0x2d];
		vol->rootDirClus = (vol->rootDirClus << 8) + mBuffer[0x2c];
		
		vol->dataStart = vol->fatSec + secsPerFat * numFats;
	}
	else if (!rootDirEnts || !secsPerFat || (rootDirEnts % (FAT_SECTOR_SIZE / sizeof(struct FatDirEntry))))
		goto fail;
	else {	//fat 12/16
		
		vol->rootDirSec = vol->fatSec + secsPerFat * numFats;
		vol->rootDirNumSec = rootDirEnts / (FAT_SECTOR_SIZE / sizeof(struct FatDirEntry));
		vol->dataStart = vol->rootDirSec + vol->rootDirNumSec;
	}
	
	if (totalSec < vol->dataStart)
		goto fail;
	
	vol->numClusters = (totalSec - vol->dataStart) / vol->secPerClus + 2;
	if (!vol->numClusters)
		goto fail;
	
	vol->numClusters += 2;	//offset
	
	if (vol->fatSz == 32) {
		
		if (vol->numClusters < 0xfff5)
			goto fail;
	}
	else
		vol->fatSz = vol->numClusters >= 0xff5 ? 16 : 12;

	return vol;

fail:
	mVolumesUsed &=~ (1 << (vol - mVolumes));
	return NULL;
}

void fatUnmount(struct FatVolume *vol)
{
	mVolumesUsed &=~ (1 << (vol - mVolumes));
}

static struct FatFile* fatPrvGetFreeFile(void)
{
	int_fast8_t idx = fatPrvFindFreeStruct(&mFilesUsed);
	
	return (idx < 0) ? NULL : (mFiles + idx);
}

static uint32_t fatPrvGetClusSz(const struct FatVolume* vol)
{
	return FAT_SECTOR_SIZE * vol->secPerClus;
}

void fatFileRewind(struct FatFile *f)
{
	if (f->isSpecialRootDir) {
		
		f->rootDir.curSec = f->vol->rootDirSec;
		f->rootDir.inSecOfst = 0;
	}
	else {
		
		f->normal.curClus = f->normal.firstClus;
		f->normal.clusIdx = 0;
		f->normal.inClusOfst = 0;
	}
}

uint32_t fatFileTell(const struct FatFile *f)
{
	if (f->isSpecialRootDir)
		return FAT_SECTOR_SIZE * (f->rootDir.curSec - f->vol->rootDirSec) + f->rootDir.inSecOfst;
	else
		return fatPrvGetClusSz(f->vol) * f->normal.clusIdx + f->normal.inClusOfst;
}

struct FatFile* fatPrvOpen(struct FatVolume* vol, uint32_t clus, uint32_t size, bool isDir)
{
	struct FatFile *f = fatPrvGetFreeFile();
		
	if (f) {
		
		f->vol = vol;
		f->normal.size = size;
		f->normal.firstClus = clus;
		f->isSpecialRootDir = false;
		f->isFile = !isDir;
		fatFileRewind(f);
	}
	
	return f;
}

struct FatFile* fatOpenRoot(struct FatVolume* vol)
{
	if (vol->fatSz == 32)
		return fatPrvOpen(vol, vol->rootDirClus, 0, true);
	else {
	
		struct FatFile *f = fatPrvGetFreeFile();
		
		if (f) {
			
			f->vol = vol;
			f->rootDir.firstSec = vol->rootDirSec;
			f->rootDir.numSec = vol->rootDirNumSec;
			f->isSpecialRootDir = true;
			f->isFile = false;
			fatFileRewind(f);
		}
		return f;
	}
}

struct FatFile* fatFileOpen(struct FatVolume* vol, struct FatFileOpenInfoOpaque *oiP)
{
	struct FatFileOpenInfoReal *oi = (struct FatFileOpenInfoReal*)oiP;
	
	return fatPrvOpen(vol, oi->clus, oi->size, !!(oi->attr & FAT_FLAG_DIR));
}

void fatFileClose(struct FatFile* file)
{
	mFilesUsed &=~ (1 << (file - mFiles));
}

static uint32_t fatPrvRead(struct FatFile *f, void *dstP, uint32_t len, bool forDir)
{
	uint32_t wantedOrig = len;
	uint8_t *dst = dstP;
	
	//verify intent
	if (!f->isFile != forDir)
		return false;
	
	//verify size
	if (f->isFile) {
		
		uint32_t curPos = fatFileTell(f);
		uint32_t left = f->normal.size - curPos;
		
		//limit to file size
		if (len > left)
			len = left;
	}
	
	while (len) {
		
		uint32_t now = len, avail, secNo;
		uint_fast16_t secOfst;
		uint16_t *ofstP;
		
		if (now > FAT_SECTOR_SIZE)	//we never read more than a sec
			now = FAT_SECTOR_SIZE;
		
		if (f->isSpecialRootDir) {
			
			avail = FAT_SECTOR_SIZE - f->rootDir.inSecOfst;
			if (!avail) {
				
				if (f->rootDir.curSec == f->rootDir.numSec - 1)
					break;
				
				f->rootDir.curSec++;
				f->rootDir.inSecOfst = 0;
				avail = FAT_SECTOR_SIZE;
			}
			secNo = f->rootDir.curSec;
			secOfst = f->rootDir.inSecOfst % FAT_SECTOR_SIZE;
			ofstP = &f->rootDir.inSecOfst;
		}
		else {
			
			avail = fatPrvGetClusSz(f->vol) - f->normal.inClusOfst;
			if (!avail) {
				
				f->normal.curClus = fatPrvWalkFat(f->vol, f->normal.curClus);
				if (!f->normal.curClus)
					break;
				f->normal.clusIdx++;
				f->normal.inClusOfst = 0;
				avail = fatPrvGetClusSz(f->vol);
			}
			else if (!fatPrvIsValidClusNo(f->vol, f->normal.curClus))	//empty files have no chain
				break;
			
			secNo = f->vol->dataStart + (f->normal.curClus - 2) * f->vol->secPerClus + (f->normal.inClusOfst / FAT_SECTOR_SIZE);
			secOfst = f->normal.inClusOfst % FAT_SECTOR_SIZE;
			ofstP = &f->normal.inClusOfst;
			
			//XXX: fix
			//as we read one sec at a time, by definition we cannot have more avail than till end of cur sector
			//if these 2 lines are removed, we'll seemingly return garbage at every 512-byte boundary of the file,
			//whose length depends on where the read started and how much was requested. After said garbage, proper
			//file data from proper offset will resume (no offset). So it'll looks like a few bytes in a file were
			//corrupted, but no movement occured.
			if (avail > FAT_SECTOR_SIZE - secOfst)
				avail = FAT_SECTOR_SIZE - secOfst;
		}
		
		//limit to availability
		if (now > avail)
			now = avail;
		
		if (secOfst == 0 && now >= FAT_SECTOR_SIZE) {		//fast path
			
			if (dst) {
				if (!fatPrvVolReadEx(f->vol, secNo, dst))
					break;
				dst += FAT_SECTOR_SIZE;
			}
			now = FAT_SECTOR_SIZE;
		}
		else {
			
			if (!fatPrvVolRead(f->vol, secNo))
				break;
			
			if (dst) {
				memcpy(dst, mBuffer + secOfst, now);
				dst += now;
			}
		}
		
		len -= now;
		(*ofstP) += now;
	}
	
	return wantedOrig - len;
}

bool fatDirEnum(struct FatFile *dir, char *nameP, uint8_t *attrP, uint32_t *sizeP, struct FatFileOpenInfoOpaque *oiP)
{
	struct FatFileOpenInfoReal *oi = (struct FatFileOpenInfoReal*)oiP;
	struct FatDirEntry de;
	uint_fast8_t i;
	bool ret;
	
	while (fatPrvRead(dir, &de, sizeof(struct FatDirEntry), true)) {
		
		if (!de.name[0])
			break;
		if (de.name[0] == '.')
			continue;
		if (de.name[0] == 0xe5)
			continue;
		if (de.attr & FAT_FLAG_VOL_LBL)	//also skips LFN entries
			continue;
		
		if (de.name[0] == 0x05)
			de.name[0] = 0xe5;
		
		if (nameP) {
			for (i = 0; i < 8 && de.name[i] != ' '; i++)
				*nameP++ = de.name[i];
			if (de.name[8] != ' ') {
				
				*nameP++ = '.';
				for (i = 8; i < 11 && de.name[i] != ' '; i++)
					*nameP++ = de.name[i];
			}
			*nameP = 0;
		}
		
		if (attrP)
			*attrP = de.attr;
		
		if (sizeP)
			*sizeP = de.fileSz;
		
		if (oi) {
			
			oi->clus = (dir->vol->fatSz == 32) ? de.clusHi : 0;
			oi->clus = (oi->clus << 16) + de.clusLo;
			oi->attr = de.attr;
			oi->size = (de.attr & FAT_FLAG_DIR) ? 0 : de.fileSz;
		}
		return true;
	}
	return false;
}

uint32_t fatFileRead(struct FatFile *f, void *buf, uint32_t len)
{
	return fatPrvRead(f, buf, len, false);
}

uint32_t fatFileSkip(struct FatFile *f, uint32_t len)
{
	return fatPrvRead(f, NULL, len, false);
}

bool fatFileSeek(struct FatFile *f, uint32_t pos)
{
	uint32_t curPos = fatFileTell(f);
	
	if (curPos > pos) {
		
		uint32_t backBy = curPos - pos;
		
		//it is not simple to go backwards, but IN the current cluster/sector we can
		//special root dirs can also be rewound easily
		
		if (f->isSpecialRootDir) {
			
			f->rootDir.curSec = f->vol->rootDirSec + pos / FAT_SECTOR_SIZE;
			f->rootDir.inSecOfst = pos % FAT_SECTOR_SIZE;
			
			return true;
		}
		
		if (backBy <= f->normal.inClusOfst) {
			
			f->normal.inClusOfst -= backBy;
			return true;
		}
		
		//else go back to the start and fast forward...
		
		fatFileRewind(f);
		curPos = 0;
	}
	
	if (curPos == pos)
		return true;
	
	return fatFileSkip(f, pos - curPos) == pos - curPos;
}

