
#define _GPIO_C_

#include "gpio.h"
#include "delay.h"

void gpio_init(void)
{
	GPIO_DIR = LED_PIN_OUT;
	GPIO_OUT = 0x00000;
}

void led_blink(void)
{
	GPIO_OUT = LED_ON;
	delay_ms(500);
	GPIO_OUT = LED_OFF;
	delay_ms(500);
}

void led_flow(void)
{

	uint32_t LED_out = 0x8000;

	led_num ++;
	GPIO_OUT = (LED_out << led_num);

	delay_ms(500);
	if(led_num == 4){
		led_num = 0;
	}
}


