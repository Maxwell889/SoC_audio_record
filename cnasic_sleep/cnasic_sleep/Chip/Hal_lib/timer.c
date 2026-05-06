/*
 * timer.c
 *
 *  Created on: 2018年4月25日
 *      Author: dell
 */

#include<stdint.h>
#include "drv_timer.h"
#include "timer.h"
#include "stdio.h"
#include "soc.h"
//#include "gpio.h"

#define test_timer_num   0
#define TEST_MODE_FREE_RUNING   0
#define TEST_MODE_USER_DEFINED  1



static volatile uint8_t time_free_runing_flag = 0;
static volatile uint8_t time_user_defined_flag = 0;
static volatile uint8_t test_mode;

struct {                      //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    uint32_t base;
    uint32_t irq;
}

const sg_timer_config[CONFIG_TIMER_NUM] = {
    {TIMER_BASE, TIMER_IRQn},

};
static dw_timer_priv_t timer_instance[CONFIG_TIMER_NUM];

static void timer_event_cb_fun(timer_event_e event, void *arg)
{
    switch (test_mode) {
        case TEST_MODE_FREE_RUNING:
            time_free_runing_flag = 1;
            break;

        case TEST_MODE_USER_DEFINED:
            time_user_defined_flag = 1;
            break;
    }

}
/**************************中断******************************************/
void csi_vic_enable_irq(int32_t IRQn)
{
    VIC->ISER[0U] = (uint32_t)(1UL << (((uint32_t)(int32_t)IRQn) & 0x1FUL));
#ifdef CONFIG_SYSTEM_SECURE
    VIC->ISSR[0U] = (uint32_t)(1UL << (((uint32_t)(int32_t)IRQn) & 0x1FUL));
#endif
}
void dw_timer_irqhandler(int idx)
{
    dw_timer_priv_t *timer_priv = &timer_instance[idx];
    timer_priv->timeout_flag = 1;

    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);

    addr->TxEOI;

    if (timer_priv->cb_event) {
                 timer_priv->cb_event(TIMER_EVENT_TIMEOUT, timer_priv->arg);
		}
}
void TIM0_IRQHandler ()
{
    dw_timer_irqhandler(0);
}
void csi_vic_disable_irq(int32_t IRQn)
{
    VIC->ICER[0U] = (uint32_t)(1UL << (((uint32_t)(int32_t)IRQn) & 0x1FUL));
}

/**************************************************************************/
/**************************配置********************************************/
int32_t csi_timer_config(timer_handle_t handle, timer_mode_e mode)
{
    dw_timer_priv_t *timer_priv = handle;
    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);

    switch (mode) {         //0
        case TIMER_MODE_FREE_RUNNING:
            addr->TxControl &= ~DW_TIMER_TXCONTROL_MODE;        //选择free-running   MODE
            break;

        case TIMER_MODE_RELOAD:
            addr->TxControl |= DW_TIMER_TXCONTROL_MODE;         //选择user-defined running   MODE
            break;
				}
    return 0;
}
int32_t csi_timer_set_timeout(timer_handle_t handle, uint32_t timeout)
{
    dw_timer_priv_t *timer_priv = handle;
    timer_priv->timeout = timeout;
    return 0;
}
int32_t csi_timer_get_current_value(timer_handle_t handle, uint32_t *value)
{
    dw_timer_priv_t *timer_priv = handle;
    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);
    *value = addr->TxCurrentValue;
    return 0;
}
int32_t csi_timer_stop(timer_handle_t handle)
{
    //TIMER_NULL_PARAM_CHK(handle);  //参数错误判断

    dw_timer_priv_t *timer_priv = handle;
    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);
    addr->TxControl |= DW_TIMER_TXCONTROL_INTMASK;      /* disenable interrupt */     //定时器中断屏蔽
    addr->TxControl &= ~DW_TIMER_TXCONTROL_ENABLE;      /* disable the timer */    //禁用定时器
    return 0;
}
int32_t csi_timer_start(timer_handle_t handle, uint32_t apbfreq)   //(handle,24000000)
{
    dw_timer_priv_t *timer_priv = handle;
    timer_priv->timeout_flag = 0;
    uint32_t min_us = apbfreq / 1000000;
    uint32_t load;
    if (timer_priv->timeout > 0xffffffff / min_us) {
        return   -1;
    }
    if (min_us) {
        load = (uint32_t)(timer_priv->timeout * min_us);
    } else {
        load = (uint32_t)(((timer_priv->timeout) * apbfreq) / 1000000);
    }
    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);
    if (timer_priv->timeout == 0) {
        addr->TxLoadCount = 0xffffffff;                           /* load time(us) */
    } else {
        addr->TxLoadCount = load;                           /* load time(us) */
    }
    addr->TxControl &= ~DW_TIMER_TXCONTROL_ENABLE;      /* disable the timer */                //关闭定时器
    addr->TxControl |= DW_TIMER_TXCONTROL_ENABLE;       /* enable the corresponding timer */   //启用相应的定时器
    addr->TxControl &= ~DW_TIMER_TXCONTROL_INTMASK;     /* enable interrupt */                  //中断位打开使能

    return 0;
}
static void timer_deactive_control(dw_timer_reg_t *addr)
{
    /* stop the corresponding timer */
    addr->TxControl &= ~DW_TIMER_TXCONTROL_ENABLE;             //停止相应的定时器
    /* Disable interrupt. */
    addr->TxControl |= DW_TIMER_TXCONTROL_INTMASK;             //禁止中断
}
int32_t csi_timer_uninitialize(timer_handle_t handle)
{
    dw_timer_priv_t *timer_priv = (dw_timer_priv_t *)handle;
    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);
    timer_deactive_control(addr);
    timer_priv->cb_event = NULL;
    csi_vic_disable_irq(timer_priv->irq);   //禁用外部中断
    return 0;
}
int32_t target_get_timer(int32_t idx, uint32_t *base, uint32_t *irq)       //(0,*(随机分配地址),*(随机分配地址))
{
    if (idx >= CONFIG_TIMER_NUM) {        //0>=4
        return 0;
    }
    *base = sg_timer_config[idx].base;
    *irq = sg_timer_config[idx].irq;
    return idx;
}
timer_handle_t csi_timer_initialize(int32_t idx, timer_event_cb_t cb_event, void *arg)   //(0,定时器事件回调的指针(NULL),NULL)
{
    if (idx < 0 || idx >= CONFIG_TIMER_NUM) {       //idx<0 || idx>=4
        return NULL;
    }

    uint32_t base = 0u;
    uint32_t irq = 0u;

    int32_t real_idx = target_get_timer(idx, &base, &irq);  //取址定时器（idx）的base、irq、arg

    if (real_idx != idx) {
        return NULL;
    }

    dw_timer_priv_t *timer_priv = &timer_instance[idx];         //将上述取址写入
    timer_priv->base = base;
    timer_priv->irq  = irq;
    timer_priv->arg  = arg;

    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);   //定时器寄存器地址
    timer_priv->timeout = DW_TIMER_INIT_DEFAULT_VALUE;             //写定时器初始化的默认值  0x7ffffff

    timer_deactive_control(addr);           //停止相应的定时器、禁用中断
    timer_priv->cb_event = cb_event;

    if (cb_event != NULL) {
        csi_vic_enable_irq(timer_priv->irq);
    }
		   //开启外部中断

    return (timer_handle_t)timer_priv;
}

int32_t csi_timer_get_instance_count(void)
{
    return CONFIG_TIMER_NUM;    //CONFIG_TIMER_NUM = 4
}


/***************************模式****************************************/
/****************************************************************************/
static int32_t test_user_defined_fun(timer_handle_t timer_handle)
{
    uint32_t timeout = 0x3fffffff;
    int32_t ret;
    time_user_defined_flag = 0;

    test_mode = TEST_MODE_USER_DEFINED;

	ret = csi_timer_config(timer_handle, TIMER_MODE_RELOAD);   //user-defined running  MODE
   if (ret < 0) {
        return -1;
    }

		ret = csi_timer_set_timeout(timer_handle, 1000000);   //set  timeout  划重点  设置延时时间（15S）
		if (ret < 0) {
        return -1;
    }
    //gpio_init( );
    //GPIO_OUT = LED_ON;                                   	//点灯
    ret = csi_timer_start(timer_handle, SYSTEM_CLOCK);
    if (ret < 0) {
        return -1;
    }

    while (timeout) {
        timeout--;
        if (time_user_defined_flag == 1) {
            break;
        }
    }
    if (time_user_defined_flag == 0) {
        return -1;
    }
    //GPIO_OUT = LED_OFF;                                  //计时结束关灯
    ret = csi_timer_stop(timer_handle);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

static int32_t test_free_running_fun(timer_handle_t timer_handle)
{
    uint32_t timeout = 0x3ffffff;
    uint32_t value;
    int32_t ret;
    time_free_runing_flag = 0;
    test_mode = TEST_MODE_FREE_RUNING;
    ret = csi_timer_config(timer_handle, TIMER_MODE_FREE_RUNNING);
    if (ret < 0) {
        return -1;
    }
    ret = csi_timer_set_timeout(timer_handle, 1000000);   //set  timeout
    if (ret < 0) {
        return -1;
    }
    ret = csi_timer_start(timer_handle, SYSTEM_CLOCK);
    if (ret < 0) {
        return -1;
    }

    while (timeout) {
        timeout--;
        csi_timer_get_current_value (timer_handle, &value);   //value = TxCurrentValue;
        if (time_free_runing_flag == 1) {
                    break;
                }

        /*if (value < 0xffffff00) {
            break;
        }*/
    }
    if (timeout == 0) {
        return -1;
    }

    ret = csi_timer_stop(timer_handle);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/**************************************************************************/
/**************************测试************************************************/
static int32_t test_timer(uint8_t timer_num)  //0
{
    int32_t ret;
    timer_handle_t timer_handle;
    if (timer_num >= csi_timer_get_instance_count()) {       //if(0>=4)
        return -1;
    }

		timer_handle = csi_timer_initialize(timer_num, timer_event_cb_fun, (void *)NULL);   //返回一个定时器0示例的指针
		/*typedef struct {
			 uint32_t base;
			 uint32_t irq;
			 timer_event_cb_t cb_event;
			 uint32_t timeout;                  ///< the set time (us)
			 uint32_t timeout_flag;
			 void *arg;
			 } dw_timer_priv_t;     返回的指针类型
				 */

    if (timer_handle == NULL) {
      //  printf("csi_timer_initialize error\n");
        return -1;
    }

    ret = test_free_running_fun(timer_handle);          //  free_running   MODE
    if (ret < 0) {
        return -1;
    }

    ret = test_user_defined_fun(timer_handle);          //user_defined  MODE  15s延时
    if (ret < 0) {
        return -1;
    }

		ret = csi_timer_uninitialize(timer_handle);     //初始化定时器接口，停止操作并释放所使用的软件资源
    if (ret < 0) {
        return -1;
    }
    return 0;
}

int example_timer(uint8_t timer_num)
{
    int ret;
    ret = test_timer(timer_num);
    if (ret < 0) {
        return -1;
    }
		return 0;
}



