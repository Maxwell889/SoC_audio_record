
#ifndef __MEM_H
#define __MEM_H

#include "soc.h"

#define mem1 (*(volatile unsigned int *)0x20012000u)
#define mem2 (*(volatile unsigned int *)0x20012004u)

#ifdef 	_MEM_C_
#define GLOBAL
#else
#define GLOBAL extern
#endif




#undef GLOBAL

void memwrite(unsigned int *addr, unsigned int num);


#endif

