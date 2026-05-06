#define _USART_C_

#include <stdio.h>
#include <stdint.h>

#include "drv_usart.h"
#include "usart.h"

extern uint8_t tx_data[20];


//static usart_handle_t g_usart_handle;
static volatile uint8_t rx_async_flag = 0;
static volatile uint8_t tx_async_flag = 0;
static volatile uint8_t rx_trigger_flag = 0;


//////////////////////////////////////////////////////////////////
//加入以下代码,支持printf函数,而不需要选择use MicroLIB
#if 0
//标准库需要的支持函数
struct __FILE
{
	int handle;
};

//FILE __stdout;
//定义_sys_exit()以避免使用半主机模式
void _sys_exit(int x)
{
	x = x;
}
//重定义fputc函数
int putc(int ch)
{
	dw_usart_reg_t *addr = ((dw_usart_reg_t *) UART0);

	while((addr->USR & 0X04)==0);//循环发送,直到发送完毕
	addr->THR = (u8) ch;
	return ch;
}
#endif


struct {
    uint32_t base;
    uint32_t irq;
}
const sg_usart_config[CONFIG_USART_NUM] = {
    {UART0_BASE, UART0_IRQn},
//    {CSKY_UART1_BASE, UART1_IRQn},
//    {CSKY_UART2_BASE, UART2_IRQn},
};

#define ERR_USART(errno) (CSI_DRV_ERRNO_USART_BASE | errno)

/*
 * setting config may be accessed when the USART is not
 * busy(USR[0]=0) and the DLAB bit(LCR[7]) is set.
 */
#define WAIT_USART_IDLE(addr)   \
    do {                        \
        int32_t timecount = 0;  \
        while ((addr->USR & USR_UART_BUSY) && (timecount < UART_BUSY_TIMEOUT)) {  \
            timecount++;   \
        }   \
        if (timecount >= UART_BUSY_TIMEOUT) {  \
            return ERR_USART(EDRV_TIMEOUT);    \
        }                                      \
    } while(0)

#define USART_NULL_PARAM_CHK(para)              \
    do {                                        \
        if (para == NULL) {                     \
            return ERR_USART(EDRV_PARAMETER);   \
        }                                       \
    } while (0)




typedef struct {
    uint32_t base;
    uint32_t irq;
    usart_event_cb_t cb_event;           ///< Event callback
    void *cb_arg;
    uint32_t rx_total_num;
    uint32_t tx_total_num;
    uint8_t *rx_buf;
    uint8_t *tx_buf;
    volatile uint32_t rx_cnt;
    volatile uint32_t tx_cnt;
    volatile uint32_t tx_busy;
    volatile uint32_t rx_busy;
    //for get data count
    uint32_t last_tx_num;
    uint32_t last_rx_num;
} dw_usart_priv_t;

static dw_usart_priv_t uart_instance[1];

//波特率
int32_t usart_config_baudrate(usart_handle_t handle, uint32_t baudrate, uint32_t apbfreq)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

//	uint8_t data[16];
//	uart_receive_query(handle, data, 16);

	WAIT_USART_IDLE(addr);

	uint32_t divisor = ((apbfreq  * 10) / baudrate) >> 4;

    if ((divisor % 10) >= 5) {
        divisor = (divisor / 10) + 1;
    } else {
        divisor = divisor / 10;
    }

	addr->LCR |= LCR_SET_DLAB;

	addr->DLL = (divisor & 0xff);
	addr->DLH = (divisor >> 8) & 0xff;

	addr->LCR &= (~LCR_SET_DLAB);

	return 0;
}

//模式
int32_t uart_config_mode(usart_handle_t handle, usart_mode_e mode)
{
	if(mode == USART_MODE_ASYNCHRONOUS){
		return 0;
	}

	return ERR_USART(EDRV_USART_MODE);
}

//奇偶校验
int32_t uart_config_parity(usart_handle_t handle, usart_parity_e parity)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

	WAIT_USART_IDLE(addr);

    WAIT_USART_IDLE(addr);

    switch (parity) {
        case USART_PARITY_NONE:
            /*CLear the PEN bit(LCR[3]) to disable parity.*/
            addr->LCR &= (~LCR_PARITY_ENABLE);
            break;

        case USART_PARITY_ODD:
            /* Set PEN and clear EPS(LCR[4]) to set the ODD parity. */
            addr->LCR |= LCR_PARITY_ENABLE;
            addr->LCR &= LCR_PARITY_ODD;
            break;

        case USART_PARITY_EVEN:
            /* Set PEN and EPS(LCR[4]) to set the EVEN parity.*/
            addr->LCR |= LCR_PARITY_ENABLE;
            addr->LCR |= LCR_PARITY_EVEN;
            break;

        default:
            return ERR_USART(EDRV_USART_PARITY);
    }

    return 0;

}

//停止位
int32_t uart_config_stopbits(usart_handle_t handle, usart_stop_bits_e stopbit)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

	WAIT_USART_IDLE(addr);

    switch (stopbit) {
        case USART_STOP_BITS_1:
            /* Clear the STOP bit to set 1 stop bit*/
            addr->LCR &= LCR_STOP_BIT1;
            break;

        case USART_STOP_BITS_2:
            /*
            * If the STOP bit is set "1",we'd gotten 1.5 stop
            * bits when DLS(LCR[1:0]) is zero, else 2 stop bits.
            */
            addr->LCR |= LCR_STOP_BIT2;
            break;

        default:
            return ERR_USART(EDRV_USART_STOP_BITS);
    }

    return 0;

}

//数据位
int32_t uart_config_databits(usart_handle_t handle, usart_data_bits_e databits)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

	uint8_t data[16];
	uart_receive_query(handle, data, 16);

	WAIT_USART_IDLE(addr);

   /* The word size decides by the DLS bits(LCR[1:0]), and the
    * corresponding relationship between them is:
    *   DLS   word size
    *       00 -- 5 bits
    *       01 -- 6 bits
    *       10 -- 7 bits
    *       11 -- 8 bits
    */

    switch (databits) {
        case USART_DATA_BITS_5:
            addr->LCR &= LCR_WORD_SIZE_5;
            break;

        case USART_DATA_BITS_6:
            addr->LCR &= 0xfd;
            addr->LCR |= LCR_WORD_SIZE_6;
            break;

        case USART_DATA_BITS_7:
            addr->LCR &= 0xfe;
            addr->LCR |= LCR_WORD_SIZE_7;
            break;

        case USART_DATA_BITS_8:
            addr->LCR |= LCR_WORD_SIZE_8;
            break;

        default:
            return ERR_USART(EDRV_USART_DATA_BITS);
    }

    return 0;

}

/*
static void usart_event_cb(uint32_t event, void *cb_arg)
{
    switch (event) {
        case USART_EVENT_SEND_COMPLETE:
            tx_async_flag = 1;
            break;

        case USART_EVENT_RECEIVE_COMPLETE:
            rx_async_flag = 1;
            break;

        default:
            break;
    }
}
*/

//void _uart_init(uint32_t *base){
//	*base = UART0;
//}

int32_t target_uart_init(int32_t idx, uint32_t *base, uint32_t *irq)
{
//	if(idx >= 0 && idx < 3)

	*base = sg_usart_config[idx].base;
	*irq = sg_usart_config[idx].irq;

	return idx;
}


//初始化
usart_handle_t uart_init(uint32_t UARTx, usart_event_cb_t cb_event, void *cb_arg)
{
	uint32_t base = 0u;
	uint32_t irq = 0u;

//	_uart_init( &base);
	target_uart_init(0, &base, &irq);

	dw_usart_priv_t *uart_priv = &uart_instance[1];
	uart_priv->base = base;
	uart_priv->irq = irq;
	uart_priv->cb_event = cb_event;
	uart_priv->cb_arg = cb_arg;

	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);
	//FIFO enable
	addr->FCR = DW_FCR_FIFOE | DW_FCR_RT_FIFO_HALF;

	//enable received data available
	addr->IER = IER_RDA_INT_ENABLE | IIR_RECV_LINE_ENABLE;

	return uart_priv;
}


//uart 配置
int32_t uart_config(usart_handle_t handle,
					uint32_t sysclk,
					uint32_t baud,
					usart_mode_e mode,
					usart_parity_e parity,
					usart_stop_bits_e stopbits,
					usart_data_bits_e bits)
{
	//uart 波特率
	usart_config_baudrate(handle, baud, sysclk);

	//uart 模式
	uart_config_mode(handle, mode);

	//uart 奇偶校验
	uart_config_parity(handle, parity);

	//uart 停止位
	uart_config_stopbits(handle, stopbits);

	//数据位
	uart_config_databits(handle, bits);

	return 0;
}

//
int32_t uart_receive_query(usart_handle_t handle, void *data, uint32_t num)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);
	int32_t recv_num = 0;
	uint8_t *dest = (uint8_t *)data;

	while(addr->LSR & 0x1){
		*dest ++= addr->RBR;
		recv_num ++;

		if(recv_num >= num){
			break;
		}
	}

	return recv_num;
}

//uart_receive
int32_t uart_receive(usart_handle_t handle, void *data, uint32_t num)
{
	dw_usart_priv_t *uart_priv = handle;

	uart_priv->rx_buf = (uint8_t *)data;
	uart_priv->rx_total_num = num;
	uart_priv->rx_cnt = 0;
	uart_priv->rx_busy = 1;
	uart_priv->last_rx_num = 0;

	return 0;
}


//接收字符
int32_t uart_getchar(usart_handle_t handle, uint8_t *ch)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

	while(!(addr->LSR & LSR_DATA_READY));

	*ch = addr->RBR;

	return 0;
}

void UART_SendData(uint32_t UARTx, uint16_t Data)
{
	dw_usart_reg_t *addr = ((dw_usart_reg_t *) UARTx);

	addr->THR = (Data & (uint16_t)0x01FF);
}

uint16_t UART_ReceiveData(uint32_t UARTx)
{
	dw_usart_reg_t *addr = ((dw_usart_reg_t *) UARTx);

	return (uint16_t)(addr->RBR & (uint16_t)0x01FF);

}

uint8_t UART_GetITStatus(uint32_t UARTx)
{
	dw_usart_reg_t *addr = ((dw_usart_reg_t *) UARTx);

	return (uint8_t)(addr->IIR & 0xf);

//	switch(it_state){
//		case DW_IIR_THR_EMPTY:
//			return
//	}
}

int32_t uart_read_byte(usart_handle_t handle)
{
	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

	while(!(addr->LSR & LSR_DATA_READY));

	return (uint16_t)(addr->RBR & (uint16_t)0x01FF);

}


//发送字符
int32_t uart_send_byte(usart_handle_t handle, uint8_t ch)
{

	dw_usart_priv_t *uart_priv = handle;
	dw_usart_reg_t *addr = (dw_usart_reg_t *)(uart_priv->base);

	while((!(addr->LSR & DW_LSR_TRANS_EMPTY)));

	addr->THR = ch;

	return 0;
}

//
int32_t uart_read(usart_handle_t uart, unsigned char *data, uint32_t num)
{
	uint8_t Res;

	uint32_t i = 0;
	int time_out = 0x7fffff;
	rx_async_flag = 0;

	for(i = 0; i < num; i ++){
		Res = uart_read_byte(uart);
		data[i] = Res;
	}

//	for(i = 0; i < num; i ++){
//		uart_getchar(uart, data);
//	}

//	uart_receive(uart, data, num);

	while(time_out){
		time_out --;
		if(rx_async_flag == 1){
			break;
		}
	}
	if(0 == time_out){
		return -1;
	}

	rx_async_flag = 0;
	return 0;
}

//
int32_t uart_send(usart_handle_t uart, unsigned char *data, uint32_t num)
{
	uint32_t i;
	int  time_out = 0x7ffff;
	tx_async_flag = 0;

	for(i = 0; i < num; i ++){
		uart_send_byte(uart, data[i]);
	}

	//uart_send_byte(uart, '\n');

	while(time_out){
		time_out --;

		if(tx_async_flag == 1){
			break;
		}
	}

	if(0 == time_out){
		return -1;
	}

	tx_async_flag = 0;
	return 0;
}

//void uart_test_async_mode(usart_handle_t usart)
//{
//	uint32_t get;
//
//	//usart = uart_init(UART0, usart_event_cb, NULL);
//	uart_config(usart, SYSTEM_CLOCK, 115200, USART_MODE_ASYNCHRONOUS, USART_PARITY_NONE, USART_STOP_BITS_1, USART_DATA_BITS_8);
//
//	uart_send(usart, tx_data, sizeof(tx_data));
//}

void uart_nvic_config(void)
{
	NVIC_InitTypeDef NVIC_InitStructure;

	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_3);				//设置优先级配置模式，第3组：抢占优先级

	//Enable the Uart Interrupt
	NVIC_InitStructure.NVIC_IRQChannel = UART0_IRQn;			//串口中断号
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

}

void UART0_IRQHandler()
{
	u8 res;
	//UART_RX_STA = 0;

	u8 it_state = UART_GetITStatus(UART0);

	if(UART_GetITStatus(UART0) != 0){
		res = UART_ReceiveData(UART0);		//读取接收到的数据 UART->RBR

		if((UART_RX_STA & 0x8000) == 0){	//接收未完成
			if(UART_RX_STA & 0x4000){		//接收到了0x0d
				if(res != 0x0a)
					UART_RX_STA = 0;		//接收错误，重新开始
				else
					UART_RX_STA |= 0x8000;	//接收完成
			}
			else{							//还未收到0x0d
				if(res == 0x0d)
					UART_RX_STA |= 0x4000;
				else{
					UART_RX_BUF[UART_RX_STA & 0x3FFF] = res;
					UART_RX_STA ++;
					if(UART_RX_STA > (UART_REC_LEN - 1))
						UART_RX_STA = 0;	//接收数据错误，重新开始
				}
			}
		}
	}
}

//int _uart_test(usart_handle_t uart)
//{
//	uint32_t get;
//	int32_t ret;
//
//	while(1){
//		get = uart_wait[_reply(uart);
//
//		if((get == 1)){
//			break;
//		}
//		else{
//
//		}
//	}
//}

//测试uart
//int test_uart()
//{
//	usart_handle_t p_uart;
//	int32_t ret;
//
//	//初始化UART
//	p_uart = uart_init(UART0, NULL, NULL);
//
//	//配置UART
//	ret = uart_config(p_uart, SYSTEM_CLOCK, 115200, USART_MODE_ASYNCHRONOUS, USART_PARITY_NONE, USART_STOP_BITS_1,USART_DATA_BITS_8);
//
//	//ret =_uart_test(p_uart);
//
//	return 0;
//}


