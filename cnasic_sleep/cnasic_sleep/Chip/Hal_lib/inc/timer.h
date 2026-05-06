#include "soc.h"

#include "dw_timer.h"

#define TCIP_BASE           (0xE000E000UL)                            //!< Titly Coupled IP Base Address
#define CORET_BASE          (TCIP_BASE +  0x0010UL)                   //!< CORET Base Address
#define VIC_BASE           (TCIP_BASE +  0x0100UL)                   //!< VIC Base Address
#define DCC_BASE            (0xE0011000UL)                            //!< DCC Base Address
#define VIC  ((VIC_Type*)VIC_BASE)       //!< VIC configuration struct //
#define CONFIG_TIMER_NUM 4

typedef void *timer_hand_t;

//typedef void (*timer_event_cb_t)(timer_event_e event, void *arg);   //重复定义 drv_timer.h

typedef struct {
    uint32_t base;
    uint32_t irq;
    timer_event_cb_t cb_event;
    uint32_t timeout;                  ///< the set time (us)
    uint32_t timeout_flag;
    void *arg;
} dw_timer_priv_t;

static dw_timer_priv_t timer_instance[CONFIG_TIMER_NUM];

/**
      \brief 访问矢量中断控制器的结构体
     */
    typedef struct {
        __IOM uint32_t ISER[1U];               /*!< Offset: 0x000 (R/W)  中断使能设置寄存器 */
        uint32_t RESERVED0[15U];
        __IOM uint32_t IWER[1U];               /*!< Offset: 0x040 (R/W)  中断低功耗唤醒设置寄存器 */
        uint32_t RESERVED1[15U];
        __IOM uint32_t ICER[1U];               /*!< Offset: 0x080 (R/W)  中断使能清除寄存器*/
        uint32_t RESERVED2[15U];
        __IOM uint32_t IWDR[1U];               /*!< Offset: 0x0c0 (R/W)  中断低功耗唤醒清除寄存器 */
        uint32_t RESERVED3[15U];
        __IOM uint32_t ISPR[1U];               /*!< Offset: 0x100 (R/W)  中断等待设置寄存器*/
        uint32_t RESERVED4[15U];
        __IOM uint32_t ISSR[1U];               /*!< Offset: 0x140 (R/W)  安全中断使能设置寄存器 */
        uint32_t RESERVED5[15U];
        __IOM uint32_t ICPR[1U];               /*!< Offset: 0x180 (R/W)  中断等待清除寄存器*/
        uint32_t RESERVED6[31U];
        __IOM uint32_t IABR[1U];               /*!< Offset: 0x200 (R/W)  中断响应状态寄存器*/
        uint32_t RESERVED7[63U];
        __IOM uint32_t IPR[8U];                /*!< Offset: 0x300 (R/W)  中断优先级设置寄存器*/
        uint32_t RESERVED8[504U];
        __IM  uint32_t ISR;                    /*!< Offset: 0xB00 (R/ )  中断状态寄存器 */
        __IOM uint32_t IPTR;                   /*!< Offset: 0xB04 (R/W)  中断优先级阈值寄存器 */
    } VIC_Type;
/*
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
struct {                                      //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    uint32_t base;
    uint32_t irq;
}

const sg_timer_config[CONFIG_TIMER_NUM] = {
    {TIMER_BASE, TIMER_IRQn},

};
static dw_timer_priv_t timer_instance[CONFIG_TIMER_NUM];

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
    addr->TxControl |= DW_TIMER_TXCONTROL_INTMASK;      // disenable interrupt      //定时器中断屏蔽
    addr->TxControl &= ~DW_TIMER_TXCONTROL_ENABLE;      // disable the timer     //禁用定时器

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
        addr->TxLoadCount = 0xffffffff;                           // load time(us)
    } else {
        addr->TxLoadCount = load;                           // load time(us)
    }
    addr->TxControl &= ~DW_TIMER_TXCONTROL_ENABLE;      // disable the timer               //关闭定时器
    addr->TxControl |= DW_TIMER_TXCONTROL_ENABLE;       // enable the corresponding timer    //启用相应的定时器
    addr->TxControl &= ~DW_TIMER_TXCONTROL_INTMASK;     // enable interrupt                   //中断位打开使能

    return 0;
}

static void timer_deactive_control(dw_timer_reg_t *addr)
{
    // stop the corresponding timer
    addr->TxControl &= ~DW_TIMER_TXCONTROL_ENABLE;             //停止相应的定时器
    // Disable interrupt.
    addr->TxControl |= DW_TIMER_TXCONTROL_INTMASK;             //禁止中断
}

void csi_vic_disable_irq(int32_t IRQn)
{
    VIC->ICER[0U] = (uint32_t)(1UL << (((uint32_t)(int32_t)IRQn) & 0x1FUL));
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
int32_t target_get_timer(int32_t idx, uint32_t *base, uint32_t *irq)       //(0,*(随机分配地址ַ),*(随机分配地址))
{
    if (idx >= CONFIG_TIMER_NUM) {        //0>=4
        return NULL;
    }
    *base = sg_timer_config[idx].base;
    *irq = sg_timer_config[idx].irq;
    return idx;
}
timer_handle_t csi_timer_initialize(int32_t idx, timer_event_cb_t cb_event, void *arg)   //(0,定时器时间回调的指针(NULL),NULL)
{
    if (idx < 0 || idx >= CONFIG_TIMER_NUM) {       //idx<0 || idx>=4
        return NULL;
    }

    uint32_t base = 0u;
    uint32_t irq = 0u;

    int32_t real_idx = target_get_timer(idx, &base, &irq);  //取址定时器 （idx）的base、irq、arg

    if (real_idx != idx) {
        return NULL;
    }

    dw_timer_priv_t *timer_priv = &timer_instance[idx];         //将上述取址写入
    timer_priv->base = base;
    timer_priv->irq  = irq;
    timer_priv->arg  = arg;

    dw_timer_reg_t *addr = (dw_timer_reg_t *)(timer_priv->base);   //定时器寄存器地址ַ
    timer_priv->timeout = DW_TIMER_INIT_DEFAULT_VALUE;             //写定时器初始化的默认值     0x7ffffff

    timer_deactive_control(addr);           //停止相应的定时器、禁止中断
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

*/










