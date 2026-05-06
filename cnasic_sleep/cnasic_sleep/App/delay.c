
#define _DELAY_C_

//#include "soc.h"
#include "delay.h"

//-------使用systick中断来做延时---------//

static __IO uint32_t usTicks;
static __IO uint32_t msTicks;

// SysTick_Handler systick中断函数
//void SysTick_Handler()
//{
//	if(msTicks != 0){
//		msTicks --;
//	}
//	
//	uwTick ++;
////	if(usTicks != 0){
////		usTicks --;
////		msTicks ++;
////	}
//}

void HardFault_Handler()
{
	while(1);
}

void SysTick_Handler()
{
	uwTick ++;
}

uint32_t GetTick(void)
{
	return uwTick;
}

//配置systick中断时间
void delay_init(void)
{
	//1 us定时中断
	//SysTick_Config(SYSTEM_CLOCK / 1000000);
	//1ms 定时中断
	SysTick_Config(SYSTEM_CLOCK / 10);
}

void SysTick_init()
{
	SysTick_Config(SYSTEM_CLOCK / 10);
}

void delay_us(uint32_t us)
{
	usTicks = us;
	
	while(us--);
}

void delay_ms(uint16_t ms)
{
	msTicks = ms;
	
//	while(msTicks);
	
	while(ms --){
		
		delay_us(1000);
	}
}


void Delay_ms(int tmp)
{
	int i;
	
	for(i = 0; i < (tmp * 5000); i ++){
		
	}
}




