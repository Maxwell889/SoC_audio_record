#ifndef __DELAY_H
#define __DELAY_H

#include "soc.h"

#ifdef	_DELAY_C_
#define	GLOBAL
#else
#define GLOBAL extern
#endif	

GLOBAL uint32_t uwTick;
//GLOBAL int count;


#undef GLOBAL

void Delay_ms(int tmp);

void delay_init(void);
void delay_us(uint32_t us);
void delay_ms(uint16_t ms);

//void SysTick_init();
uint32_t GetTick(void);


#endif

