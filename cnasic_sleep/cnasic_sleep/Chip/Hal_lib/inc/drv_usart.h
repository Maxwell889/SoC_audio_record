#ifndef __DRV_USART_H
#define __DRV_USART_H

#ifdef __cplusplus
extern "C" {
#endif 


/// definition for usart handle.
typedef void *usart_handle_t;
	
#define ERRNO_DRV_START 0X80

/* drvier General return codes */
typedef enum {
    EDRV = ERRNO_DRV_START,   ///< Unspecified error
    EDRV_BUSY,                ///< Driver is busy
    EDRV_TIMEOUT,             ///< Timeout occurred
    EDRV_UNSUPPORTED,         ///< Operation not supported
    EDRV_PARAMETER,           ///< Parameter error
    EDRV_SPECIFIC             ///< Start of driver specific errors
} drv_common_err_e;	

/****** USART specific error codes *****/
typedef enum {
    EDRV_USART_MODE  = (EDRV_SPECIFIC + 1),      ///< Specified Mode not supported
    EDRV_USART_BAUDRATE,                         ///< Specified baudrate not supported
    EDRV_USART_DATA_BITS,                        ///< Specified number of Data bits not supported
    EDRV_USART_PARITY,                           ///< Specified Parity not supported
    EDRV_USART_STOP_BITS,                        ///< Specified number of Stop bits not supported
    EDRV_USART_FLOW_CONTROL,                     ///< Specified Flow Control not supported
    EDRV_USART_CPOL,                             ///< Specified Clock Polarity not supported
    EDRV_USART_CPHA                              ///< Specified Clock Phase not supported
} drv_usart_error_e;

/*----- USART Control Codes: Mode -----*/
typedef enum {
    USART_MODE_ASYNCHRONOUS         = 0,   ///< USART (Asynchronous)
    USART_MODE_SYNCHRONOUS_MASTER      ,   ///< Synchronous Master
    USART_MODE_SYNCHRONOUS_SLAVE       ,   ///< Synchronous Slave (external clock signal)
    USART_MODE_SINGLE_WIRE             ,    ///< USART Single-wire (half-duplex)
    USART_MODE_SINGLE_IRDA             ,    ///< UART IrDA
    USART_MODE_SINGLE_SMART_CARD       ,    ///< UART Smart Card
} usart_mode_e;

/*----- USART Control Codes: Mode Parameters: Data Bits -----*/
typedef enum {
    USART_DATA_BITS_5             = 0,    ///< 5 Data bits
    USART_DATA_BITS_6                ,    ///< 6 Data bit
    USART_DATA_BITS_7                ,    ///< 7 Data bits
    USART_DATA_BITS_8                ,    ///< 8 Data bits (default)
    USART_DATA_BITS_9                     ///< 9 Data bits
} usart_data_bits_e;

/*----- USART Control Codes: Mode Parameters: Parity -----*/
typedef enum {
    USART_PARITY_NONE            = 0,       ///< No Parity (default)
    USART_PARITY_EVEN               ,       ///< Even Parity
    USART_PARITY_ODD                ,       ///< Odd Parity
    USART_PARITY_1                  ,       ///< Parity forced to 1
    USART_PARITY_0                          ///< Parity forced to 0
} usart_parity_e;

/*----- USART Control Codes: Mode Parameters: Stop Bits -----*/
typedef enum {
    USART_STOP_BITS_1            = 0,    ///< 1 Stop bit (default)
    USART_STOP_BITS_2               ,    ///< 2 Stop bits
    USART_STOP_BITS_1_5             ,    ///< 1.5 Stop bits
    USART_STOP_BITS_0_5                  ///< 0.5 Stop bits
} usart_stop_bits_e;

/*----- USART Control Codes: Mode Parameters: Clock Polarity (Synchronous mode) -----*/
typedef enum {
    USART_CPOL0                  = 0,    ///< CPOL = 0 (default). data are captured on rising edge (low->high transition)
    USART_CPOL1                          ///< CPOL = 1. data are captured on falling edge (high->lowh transition)
} usart_cpol_e;

/*----- USART Control Codes: Mode Parameters: Clock Phase (Synchronous mode) -----*/
typedef enum {
    USART_CPHA0                  = 0,   ///< CPHA = 0 (default). sample on first (leading) edge
    USART_CPHA1                         ///< CPHA = 1. sample on second (trailing) edge
} usart_cpha_e;

/*----- USART Control Codes: flush data type-----*/
typedef enum {
    USART_FLUSH_WRITE,
    USART_FLUSH_READ
} usart_flush_type_e;

/*----- USART Control Codes: flow control type-----*/
typedef enum {
    USART_FLOWCTRL_NONE,
    USART_FLOWCTRL_CTS,
    USART_FLOWCTRL_RTS,
    USART_FLOWCTRL_CTS_RTS
} usart_flowctrl_type_e;

/*----- USART Modem Control -----*/
typedef enum {
    USART_RTS_CLEAR,                  ///< Deactivate RTS
    USART_RTS_SET,                    ///< Activate RTS
    USART_DTR_CLEAR,                  ///< Deactivate DTR
    USART_DTR_SET                     ///< Activate DTR
} usart_modem_ctrl_e;

/*----- USART Modem Status -----*/
typedef struct {
  uint32_t cts : 1;                     ///< CTS state: 1=Active, 0=Inactive
  uint32_t dsr : 1;                     ///< DSR state: 1=Active, 0=Inactive
  uint32_t dcd : 1;                     ///< DCD state: 1=Active, 0=Inactive
  uint32_t ri  : 1;                     ///< RI  state: 1=Active, 0=Inactive
} usart_modem_stat_t;

/*----- USART Control Codes: on-off intrrupte type-----*/
typedef enum {
    USART_INTR_WRITE,
    USART_INTR_READ
} usart_intr_type_e;

/**
\brief USART Status
*/
typedef struct  {
    uint32_t tx_busy          : 1;        ///< Transmitter busy flag
    uint32_t rx_busy          : 1;        ///< Receiver busy flag
    uint32_t tx_underflow     : 1;        ///< Transmit data underflow detected (cleared on start of next send operation)(Synchronous Slave)
    uint32_t rx_overflow      : 1;        ///< Receive data overflow detected (cleared on start of next receive operation)
    uint32_t rx_break         : 1;        ///< Break detected on receive (cleared on start of next receive operation)
    uint32_t rx_framing_error : 1;        ///< Framing error detected on receive (cleared on start of next receive operation)
    uint32_t rx_parity_error  : 1;        ///< Parity error detected on receive (cleared on start of next receive operation)
} usart_status_t;

/****** USART Event *****/
typedef enum {
    USART_EVENT_SEND_COMPLETE       = 0,  ///< Send completed; however USART may still transmit data
    USART_EVENT_RECEIVE_COMPLETE    = 1,  ///< Receive completed
    USART_EVENT_TRANSFER_COMPLETE   = 2,  ///< Transfer completed
    USART_EVENT_TX_COMPLETE         = 3,  ///< Transmit completed (optional)
    USART_EVENT_TX_UNDERFLOW        = 4,  ///< Transmit data not available (Synchronous Slave)
    USART_EVENT_RX_OVERFLOW         = 5,  ///< Receive data overflow
    USART_EVENT_RX_TIMEOUT          = 6,  ///< Receive character timeout (optional)
    USART_EVENT_RX_BREAK            = 7,  ///< Break detected on receive
    USART_EVENT_RX_FRAMING_ERROR    = 8,  ///< Framing error detected on receive
    USART_EVENT_RX_PARITY_ERROR     = 9,  ///< Parity error detected on receive
    USART_EVENT_CTS                 = 10, ///< CTS state changed (optional)
    USART_EVENT_DSR                 = 11, ///< DSR state changed (optional)
    USART_EVENT_DCD                 = 12, ///< DCD state changed (optional)
    USART_EVENT_RI                  = 13, ///< RI  state changed (optional)
    USART_EVENT_RECEIVED            = 14,  ///< Received data, but no send()/receive()/transfer() called
} usart_event_e;

typedef void (*usart_event_cb_t)(usart_event_e event, void *cb_arg);   ///< Pointer to \ref usart_event_cb_t : USART Event call back.

/**
\brief USART Driver Capabilities.
*/
typedef struct  {
    uint32_t asynchronous       : 1;      ///< supports UART (Asynchronous) mode
    uint32_t synchronous_master : 1;      ///< supports Synchronous Master mode
    uint32_t synchronous_slave  : 1;      ///< supports Synchronous Slave mode
    uint32_t single_wire        : 1;      ///< supports UART Single-wire mode
    uint32_t irda               : 1;      ///< supports UART IrDA mode
    uint32_t smart_card         : 1;      ///< supports UART Smart Card mode
    uint32_t smart_card_clock   : 1;      ///< Smart Card Clock generator available
    uint32_t flow_control_rts   : 1;      ///< RTS Flow Control available
    uint32_t flow_control_cts   : 1;      ///< CTS Flow Control available
    uint32_t event_tx_complete  : 1;      ///< Transmit completed event: \ref ARM_USART_EVENT_TX_COMPLETE
    uint32_t event_rx_timeout   : 1;      ///< Signal receive character timeout event: \ref ARM_USART_EVENT_RX_TIMEOUT
    uint32_t rts                : 1;      ///< RTS Line: 0=not available, 1=available
    uint32_t cts                : 1;      ///< CTS Line: 0=not available, 1=available
    uint32_t dtr                : 1;      ///< DTR Line: 0=not available, 1=available
    uint32_t dsr                : 1;      ///< DSR Line: 0=not available, 1=available
    uint32_t dcd                : 1;      ///< DCD Line: 0=not available, 1=available
    uint32_t ri                 : 1;      ///< RI Line: 0=not available, 1=available
    uint32_t event_cts          : 1;      ///< Signal CTS change event: \ref ARM_USART_EVENT_CTS
    uint32_t event_dsr          : 1;      ///< Signal DSR change event: \ref ARM_USART_EVENT_DSR
    uint32_t event_dcd          : 1;      ///< Signal DCD change event: \ref ARM_USART_EVENT_DCD
    uint32_t event_ri           : 1;      ///< Signal RI change event: \ref ARM_USART_EVENT_RI
} usart_capabilities_t;



	
	
	
	
	
	


#ifdef __cplusplus
}
#endif


#endif 

