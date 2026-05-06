
#ifndef __GPIO_H
#define __GPIO_H

#include "soc.h"


#define GPIO   GPIO_BASE

#define GPIO_DIR   (*(volatile unsigned int *)0x40004004u)  // 原来是 0x40019004
#define GPIO_OUT   (*(volatile unsigned int *)0x40004000u)  // 原来是 0x40019000
#define GPIO_IN		(*(volatile unsigned int *)0x40004008u)  /// GPIO输入寄存器

#define LED_PIN_OUT	(0x0000f)   //GPIO只有低8位有效
#define LED_PIN_IN	(0x00000)
 
#define LED_ON	(0x0000f)
#define LED_OFF	(0x00000)

#define LED0_ON (0x00001)


//全局路径定义
#ifdef  _GPIO_C_

#define GLOBAL
#else
#define GLOBAL extern
#endif

GLOBAL uint8_t led_num;





#undef GLOBAL


typedef struct {
    __IOM uint32_t SWPORT_DR;                     /* Offset: 0x000 (W/R)  PortA data register */
    __IOM uint32_t SWPORT_DDR;                    /* Offset: 0x004 (W/R)  PortA data direction register */
    __IOM uint32_t PORT_CTL;                      /* Offset: 0x008 (W/R)  PortA source register */

} dw_gpio_reg_t;



void gpio_init(void);
void led_blink(void);
void led_flow(void);




#endif


