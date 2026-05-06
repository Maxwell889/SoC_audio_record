
/**************************************************************************//**
 * @file     soc.h
 * @brief    soc 
 *           
 * @version  V1.0
 * @date     2018-4-4
 ******************************************************************************/


#ifndef SOC_H
#define SOC_H

#ifdef __cplusplus
extern "C"	{
#endif

#define SYSTEM_CLOCK   (25000000)


	
typedef enum IRQn
{
/******  Cortex-M3 Processor Exceptions Numbers ***************************************************/
  NonMaskableInt_IRQn         = -14,    /*!< 2 Non Maskable Interrupt                             */
  MemoryManagement_IRQn       = -12,    /*!< 4 Cortex-M3 Memory Management Interrupt              */
  BusFault_IRQn               = -11,    /*!< 5 Cortex-M3 Bus Fault Interrupt                      */
  UsageFault_IRQn             = -10,    /*!< 6 Cortex-M3 Usage Fault Interrupt                    */
  SVCall_IRQn                 = -5,     /*!< 11 Cortex-M3 SV Call Interrupt                       */
  DebugMonitor_IRQn           = -4,     /*!< 12 Cortex-M3 Debug Monitor Interrupt                 */
  PendSV_IRQn                 = -2,     /*!< 14 Cortex-M3 Pend SV Interrupt                       */
  SysTick_IRQn                = -1,     /*!< 15 Cortex-M3 System Tick Interrupt                   */

 /***  CNSOC CM3 BLE Interrupt Numbers  ****/
 
	TIMER_IRQn			= 0,
	UART0_IRQn			= 5,
	
	//GPIO_IRQn			=	0,
	//UART0_IRQn		=	1,
} IRQn_Type ;

#include "core_cm3.h"
#include <stdint.h>
//#include <stdio.h>


typedef int32_t  s32;
typedef int16_t s16;
typedef int8_t  s8;

typedef const int32_t sc32;  /*!< Read Only */
typedef const int16_t sc16;  /*!< Read Only */
typedef const int8_t sc8;   /*!< Read Only */

typedef __IO int32_t  vs32;
typedef __IO int16_t  vs16;
typedef __IO int8_t   vs8;

typedef __I int32_t vsc32;  /*!< Read Only */
typedef __I int16_t vsc16;  /*!< Read Only */
typedef __I int8_t vsc8;   /*!< Read Only */

typedef uint32_t  u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef const uint32_t uc32;  /*!< Read Only */
typedef const uint16_t uc16;  /*!< Read Only */
typedef const uint8_t uc8;   /*!< Read Only */

typedef __IO uint32_t  vu32;
typedef __IO uint16_t vu16;
typedef __IO uint8_t  vu8;

typedef __I uint32_t vuc32;  /*!< Read Only */
typedef __I uint16_t vuc16;  /*!< Read Only */
typedef __I uint8_t vuc8;   /*!< Read Only */

typedef enum {RESET = 0, SET = !RESET} FlagStatus, ITStatus;

typedef enum {DISABLE = 0, ENABLE = !DISABLE} FunctionalState;
#define IS_FUNCTIONAL_STATE(STATE) (((STATE) == DISABLE) || ((STATE) == ENABLE))

typedef enum {ERROR = 0, SUCCESS = !ERROR} ErrorStatus;


#include "nvic.h"

/* ================================================================================ */
/* ================              Peripheral memory map             ================ */
/* ================================================================================ */
/* --------------------------  CHIP memory map  ------------------------------- */

#define SRAM_BASE             	(0x20000000UL)
#define FLASH_BASE           	(0x30000000UL)


#define DMAC_BASE            	(0x40001000UL)
#define SFC_BASE              	(0x40003000UL)

#define TIMER_BASE				(0x40011000UL)
#define RTC_BASE             	(0x40012000UL)
#define UART0_BASE            	(0x40013000UL)
#define UART1_BASE            	(0x40014000UL)
#define I2C0_BASE             	(0x40015000UL)
#define I2C1_BASE				(0x40016000UL)
#define SPI_BASE             	(0x40017000UL)
#define I2S_BASE              	(0x40018000UL)
#define GPIO_BASE            	(0x40019000UL)
#define BLE_BASE             	(0x41000000UL)


// standard bits
#define BIT_0	(0x00000001u)
#define BIT_1	(0x00000002u)
#define BIT_2	(0x00000004u)
#define BIT_3	(0x00000008u)
#define BIT_4	(0x00000010u)
#define BIT_5	(0x00000020u)
#define BIT_6	(0x00000040u)
#define BIT_7	(0x00000080u)
#define BIT_8	(0x00000100u)
#define BIT_9	(0x00000200u)
#define BIT_10	(0x00000400u)
#define BIT_11	(0x00000800u)
#define BIT_12	(0x00001000u)
#define BIT_13	(0x00002000u)
#define BIT_14	(0x00004000u)
#define BIT_15	(0x00008000u)
#define BIT_16	(0x00010000u)
#define BIT_17	(0x00020000u)
#define BIT_18	(0x00040000u)
#define BIT_19	(0x00080000u)
#define BIT_20	(0x00100000u)
#define BIT_21	(0x00200000u)
#define BIT_22	(0x00400000u)
#define BIT_23	(0x00800000u)
#define BIT_24	(0x01000000u)
#define BIT_25	(0x02000000u)
#define BIT_26	(0x04000000u)
#define BIT_27	(0x08000000u)
#define BIT_28	(0x10000000u)
#define BIT_29	(0x20000000u)
#define BIT_30	(0x40000000u)
#define BIT_31	(0x80008000u)




#ifdef __cplusplus
}
#endif

#endif  /* SOC_H */
