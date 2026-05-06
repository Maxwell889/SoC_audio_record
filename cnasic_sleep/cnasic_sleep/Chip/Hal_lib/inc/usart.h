#ifndef __USART_H
#define __USART_H

#include <stdint.h>
#include "soc.h"

#define CONFIG_USART_NUM 1

#define UART0     UART0_BASE

#define CSI_DRV_ERRNO_USART_BASE    0x81020000


#define UART_REC_LEN		   200		//最大接收字节数
#define BAUDRATE_DEFAULT       19200	//默认波特率19200
#define UART_BUSY_TIMEOUT      1000000	//繁忙超时
#define UART_RECEIVE_TIMEOUT   1000		//接收超时
#define UART_TRANSMIT_TIMEOUT  1000		//发送超时
#define UART_MAX_FIFO		   0x10		//最大FIFO
/* UART register bit definitions */

#define USR_UART_BUSY           0x01
#define USR_UART_TFE            0x04
#define USR_UART_RFNE           0x08
#define LSR_DATA_READY          0x01
#define LSR_THR_EMPTY           0x20
#define IER_RDA_INT_ENABLE      0x01
#define IER_THRE_INT_ENABLE     0x02
#define IIR_NO_ISQ_PEND         0x01
#define IIR_RECV_LINE_ENABLE    0x04

#define LCR_SET_DLAB            0x80   /* enable r/w DLR to set the baud rate */
#define LCR_PARITY_ENABLE       0x08   /* parity enabled */
#define LCR_PARITY_EVEN         0x10   /* Even parity enabled */
#define LCR_PARITY_ODD          0xef   /* Odd parity enabled */
#define LCR_WORD_SIZE_5         0xfc   /* the data length is 5 bits */
#define LCR_WORD_SIZE_6         0x01   /* the data length is 6 bits */
#define LCR_WORD_SIZE_7         0x02   /* the data length is 7 bits */
#define LCR_WORD_SIZE_8         0x03   /* the data length is 8 bits */
#define LCR_STOP_BIT1           0xfb   /* 1 stop bit */
#define LCR_STOP_BIT2           0x04   /* 1.5 stop bit */

#define DW_LSR_PFE              0x80
#define DW_LSR_TEMT             0x40
#define DW_LSR_THRE             0x40
#define	DW_LSR_BI               0x10
#define	DW_LSR_FE               0x08
#define	DW_LSR_PE               0x04
#define	DW_LSR_OE               0x02
#define	DW_LSR_DR               0x01
#define DW_LSR_TRANS_EMPTY      0x20

#define DW_FCR_FIFOE            0x01
#define DW_FCR_RFIFOR           0x02
#define DW_FCR_XFIFOR           0x04
#define DW_FCR_RT_FIFO_SINGLE   (0x0 << 6)    /* rcvr trigger 1 character in the FIFO */
#define DW_FCR_RT_FIFO_QUARTER  (0x1 << 6)     /* rcvr trigger FIFO 1/4 full */
#define DW_FCR_RT_FIFO_HALF     (0x2 << 6)     /* rcvr trigger FIFO 1/2 full */
#define DW_FCR_RT_FIFO_LESSTWO  (0x3 << 6)     /* rcvr trigger FIFO 2 less than full */
#define DW_FCR_TET_FIFO_EMPTY   (0x0 << 4)     /* tx empty trigger FIFO empty */
#define DW_FCR_TET_FIFO_TWO     (0x1 << 4)   /* tx empty trigger 2 characters in the FIFO */
#define DW_FCR_TET_FIFO_QUARTER (0x2 << 4)   /* tx empty trigger FIFO 1/4 full */
#define DW_FCR_TET_FIFO_HALF    (0x3 << 4)  /* tx empty trigger FIFO 1/2 full*/

#define DW_IIR_THR_EMPTY        0x02    /* threshold empty */
#define DW_IIR_RECV_DATA        0x04    /* received data available */
#define DW_IIR_RECV_LINE        0x06    /* receiver line status */
#define DW_IIR_CHAR_TIMEOUT     0x0c    /* character timeout */

#define DW_MCR_AFCE             0x20    /* Auto Flow Control Enable */
#define DW_MCR_RTS              0x02

#pragma anon_unions
typedef struct {
    union {
        __IM uint32_t RBR;           /* Offset: 0x000 (R/ )  Receive buffer register */
        __OM uint32_t THR;           /* Offset: 0x000 ( /W)  Transmission hold register */
        __IOM uint32_t DLL;          /* Offset: 0x000 (R/W)  Clock frequency division low section register */
    };
    union {
        __IOM uint32_t DLH;          /* Offset: 0x004 (R/W)  Clock frequency division high section register */
        __IOM uint32_t IER;          /* Offset: 0x004 (R/W)  Interrupt enable register */
    };
    union {
        __IM uint32_t IIR;             /* Offset: 0x008 (R/ )  Interrupt indicia register */
        __OM uint32_t FCR;             /* Offset: 0x008 ( /W)  FIFO control register */
    };
    __IOM uint32_t LCR;            /* Offset: 0x00C (R/W)  Transmission control register */
    __IOM uint32_t MCR;            /* Offset: 0x010 (R/W)  Modem control register */
    __IM uint32_t LSR;             /* Offset: 0x014 (R/ )  Transmission state register */
    __IM uint32_t MSR;             /* Offset: 0x018 (R/ )  Modem state register */
    uint32_t RESERVED1[21];
    __IOM uint32_t FAR;            /* Offset: 0x070 (R/W)  FIFO accesss register */
    __IM uint32_t TFR;             /* Offset: 0x074 (R/ )  transmit FIFO read */
    __OM uint32_t RFW;             /* Offset: 0x078 ( /W)  receive FIFO write */
    __IM uint32_t USR;             /* Offset: 0x07c (R/ )  UART state register */
    __IM uint32_t TFL;             /* Offset: 0x080 (R/ )  transmit FIFO level */
    __IM uint32_t RFL;             /* Offset: 0x084 (R/ )  receive FIFO level */

} dw_usart_reg_t;




#ifdef	_USART_C_

#define GLOBAL
#else
#define	GLOBAL extern
#endif

GLOBAL uint16_t UART_RX_STA ;
GLOBAL u8 UART_RX_BUF[UART_REC_LEN];


#undef GLOBAL

static void usart_event_cb(uint32_t event, void *cb_arg);

int32_t uart_receive_query(usart_handle_t handle, void *data, uint32_t num);
int32_t usart_config_baudrate(usart_handle_t handle, uint32_t baudrate, uint32_t apbfreq);
int32_t uart_config_mode(usart_handle_t handle, usart_mode_e mode);
int32_t uart_config_parity(usart_handle_t handle, usart_parity_e parity);
int32_t uart_config_stopbits(usart_handle_t handle, usart_stop_bits_e stopbit);
int32_t uart_config_databits(usart_handle_t handle, usart_data_bits_e databits);

void uart_nvic_config(void);

uint8_t UART_GetITStatus(uint32_t UARTx);
void UART_SendData(uint32_t UARTx,  uint16_t Data);
uint16_t UART_ReceiveData(uint32_t UARTx);

int32_t uart_send_byte(usart_handle_t handle, uint8_t ch);
int32_t uart_getchar(usart_handle_t handle, uint8_t *ch);
int32_t uart_read_byte(usart_handle_t handle);
int32_t uart_receive(usart_handle_t handle, void *data, uint32_t num);

int32_t uart_read(usart_handle_t uart, unsigned char *data, uint32_t num);
int32_t uart_send(usart_handle_t uart, unsigned char *data, uint32_t num);

usart_handle_t uart_init(uint32_t UARTx, usart_event_cb_t cb_event, void *cb_arg);

int32_t uart_config(usart_handle_t handle,
					uint32_t sysclk,
					uint32_t baud,
					usart_mode_e mode,
					usart_parity_e parity,
					usart_stop_bits_e stopbits,
					usart_data_bits_e bits);

//int test_uart();




#endif

