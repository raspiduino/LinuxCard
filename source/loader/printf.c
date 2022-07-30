#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "printf.h"


static void StrPrvPrintfEx_number(uint32_t number, bool zeroExtend, uint32_t padToLength, uint32_t base, bool signedNum)
{
	char buf[64];
	uint32_t idx = sizeof(buf) - 1;
	uint32_t chr, i;
	bool neg = false;
	uint32_t numPrinted = 0;
	
	if (signedNum && number >> 31) {
		neg = true;
		number = -number;
	}
	
	if(padToLength > 31)
		padToLength = 31;
	
	buf[idx--] = 0;	//terminate
	
	do{
		chr = number % base;
		number = number / base;
		
		buf[idx--] = (chr >= 10)?(chr + 'A' - 10):(chr + '0');
		
		numPrinted++;
		
	}while(number);
	
	if (neg) {
	
		buf[idx--] = '-';
		numPrinted++;
	}
	
	if (padToLength > numPrinted)
		padToLength -= numPrinted;
	else
		padToLength = 0;
	
	while(padToLength--) {
		
		buf[idx--] = zeroExtend?'0':' ';
		numPrinted++;
	}
	
	idx++;
	
	for (i = 0; i < numPrinted; i++)
		prPutchar((buf + idx)[i]);
}

static uint32_t StrVPrintf_StrLen_withMax(const char* s,uint32_t max){
	
	uint32_t len = 0;
	
	while((*s++) && (len < max)) len++;
	
	return len;
}

void pr(const char* fmtStr, ...){
	
	char c, t;
	uint32_t i,cc, val32;
	va_list vl;
	
	va_start(vl, fmtStr);

	
	while((c = *fmtStr++) != 0){
		
		if(c == '\n'){
			prPutchar(c);
		}
		else if(c == '%'){
			
			bool zeroExtend = false, useLong = false, bail = false;
			uint32_t padToLength = 0,len, i;
			const char* str;
			
more_fmt:
			
			c = *fmtStr++;
			
			switch(c){
				
				case '%':
					
					prPutchar(c);
					break;
				
				case 'c':
					
					t = va_arg(vl,unsigned int);
					prPutchar(t);
					break;
				
				case 's':
					
					str = va_arg(vl,char*);
					if(!str) str = "(null)";
					while((c = *str++))
						prPutchar(c);
					break;
				
				case '0':
					
					if(!zeroExtend && !padToLength){
						
						zeroExtend = true;
						goto more_fmt;
					}
				
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					
					padToLength = (padToLength * 10) + c - '0';
					goto more_fmt;
				
				case 'd':
					val32 = va_arg(vl,int32_t);
					StrPrvPrintfEx_number(val32, zeroExtend, padToLength, 10, true);
					break;
					
				case 'u':
					val32 = va_arg(vl,uint32_t);
					StrPrvPrintfEx_number(val32, zeroExtend, padToLength, 10, false);
					break;
					
				case 'x':
				case 'X':
					val32 = va_arg(vl,uint32_t);
					StrPrvPrintfEx_number(val32, zeroExtend, padToLength, 16, false);
					break;
					
				case 'l':
					useLong = true;
					goto more_fmt;
				
				default:
					prPutchar(c);
					break;
				
			}
		}
		else
			prPutchar(c);
	}

	va_end(vl);
}

