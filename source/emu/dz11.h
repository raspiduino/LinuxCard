/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _DZ11_H_
#define _DZ11_H_

#include <stdbool.h>
#include <stdint.h>

bool dz11init(void);

//feed chars
void dz11charRx(uint_fast8_t line, uint_fast8_t chr);

//externally provided
extern void dz11charPut(uint_fast8_t line, uint_fast8_t chr);

#endif
