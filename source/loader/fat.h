#ifndef _FAT_H_
#define _FAT_H_

#include <stdbool.h>
#include <stdint.h>

#define FAT_SECTOR_SIZE		(512)

typedef bool (*FatReadSecProc)(void *userData, uint32_t sec, void *dst);

struct FatDirEntry;
struct FatVolume;
struct FatFile;

struct FatFileOpenInfoOpaque {
	//opaque
	uint32_t data[3];
};

#define FAT_FLAG_READONLY		0x01
#define FAT_FLAG_HIDDEN			0x02
#define FAT_FLAG_SYSTEM			0x04
#define FAT_FLAG_VOL_LBL		0x08
#define FAT_FLAG_DIR			0x10
#define FAT_FLAG_ARCHIVE		0x20

void fatInit(void *tmpBuf);		//FAT_SECTOR_SIZE bytes

struct FatVolume* fatMount(FatReadSecProc readSecF, void *readSecD);
void fatUnmount(struct FatVolume *vol);

struct FatFile* fatOpenRoot(struct FatVolume* vol);
struct FatFile* fatFileOpen(struct FatVolume* vol, struct FatFileOpenInfoOpaque *oiP);
void fatFileClose(struct FatFile* file);

bool fatDirEnum(struct FatFile *dir, char *nameP, uint8_t *attrP, uint32_t *sizeP, struct FatFileOpenInfoOpaque *oiP);

uint32_t fatFileRead(struct FatFile *file, void *buf, uint32_t len);
uint32_t fatFileSkip(struct FatFile *file, uint32_t len);	//seek forwards
uint32_t fatFileTell(const struct FatFile *file);
bool fatFileSeek(struct FatFile *file, uint32_t pos);
void fatFileRewind(struct FatFile *file);

#endif