#ifndef _ENTRY_H_
#define _ENTRY_H_

#include <stdbool.h>
#include <stdint.h>


//entrypoint
void start(void);



//provided
uint32_t getMemMap(uint32_t index);
void consoleWrite(char ch);
uint32_t getStoreSz(void);							//in 512-byte blocks
bool readblock(uint32_t blkNo, void *dst);
bool writeblock(uint32_t blkNo, const void *src);



#endif
