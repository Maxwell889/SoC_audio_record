
`timescale 1ns/1ns

module top(
   input  wire          CLK,                  // Oscillator
   input  wire          RESETn,
   // Debug
   input  wire          TDI,                  // JTAG TDI
   input  wire          TCK,                  // SWD Clk / JTAG TCK
   inout  wire          TMS,                  // SWD I/O / JTAG TMS
   output wire          TDO,                   // SWV     / JTAG TDO


   //peripherals ports

  inout  wire  [7:0]    b_pad_gpio_porta,

  input  wire           uart1_rxd,
  output wire           uart1_txd,

   input  wire           uart2_rxd,
   output wire           uart2_txd,

   // I2S microphone
   input  wire           mic_sd,
   output wire           mic_sck,
   output wire           mic_ws,

   // SPI flash, driven by ARM PL022 SSP on APB extension port 12
   input  wire           spi_flash_miso,
   output wire           spi_flash_mosi,
   output wire           spi_flash_sck,
   output wire           spi_flash_cs_n,

     // Timer
   input  wire           timer0_extin,
   input  wire           timer1_extin

);

   wire [31:0]apb_int;
   wire  [239:0] irq = {208'b0000_0000_0000_0000, apb_int};
   wire         i2s_sample_valid;
   wire [15:0]  i2s_sample_data;
   wire         i2s_fifo_wr_en;
   wire [15:0]  i2s_fifo_wr_data;
   wire         i2s_fifo_full;
   wire         i2s_fifo_empty;
   wire [15:0]  i2s_fifo_rd_data;
   wire [10:0]  i2s_fifo_usedw;
   wire         i2s_fifo_ahb_rd_en;
   wire         i2s_fifo_adpcm_rd_en;
   wire         i2s_fifo_rd_en;
   wire         audio_compress_enable;
   wire         adpcm_word_valid;
   wire [31:0]  adpcm_word_data;
   wire         adpcm_fifo_full;
   wire         adpcm_fifo_empty;
   wire [31:0]  adpcm_fifo_rd_data;
   wire [10:0]  adpcm_fifo_usedw;
   wire         adpcm_fifo_rd_en;
   wire         audio_rx_enable;
   wire         audio_i2s_clear;
   wire         audio_fifo_clear;
   wire [31:0]  audio_hrdata;
   wire         audio_hreadyout;
   wire [11:0]  apb_paddr;
   wire         apb_pwrite;
   wire [31:0]  apb_pwdata;
   wire         apb_penable;
   wire         ext12_psel;
   wire [31:0]  ext12_prdata;
   wire         ext12_pready;
   wire         ext12_pslverr;
   wire         ext13_psel;
   wire [31:0]  ext13_prdata;
   wire         ext13_pready;
   wire         ext13_pslverr;
   wire [15:0]  ssp_prdata;
   wire         ssp_intr;
   wire         ssp_rx_intr;
   wire         ssp_tx_intr;
   wire         ssp_rx_overrun_intr;
   wire         ssp_rx_timeout_intr;
   wire         ssp_fssout;
   wire         ssp_clkout;
   wire         ssp_txd;
   wire         ssp_tx_dma_sreq;
   wire         ssp_tx_dma_breq;
   wire         ssp_rx_dma_sreq;
   wire         ssp_rx_dma_breq;
   wire         ssp_scanout_pclk;
   wire         ssp_scanout_sspclk;
   wire         ssp_n_oe;
   wire         ssp_n_ctl_oe;
   reg          spi_flash_cs_n_reg;

   


    //Internal wires
    //
    wire         lockup;      // Processor lockup status
    wire         uart1_txen;  // UART1 TX enable
    wire         uart2_txen;  // UART2 TX enable
    //
   /////////////////////////////////////////////////////////////////////////////
   // Connect Code Bus to ROM
   /////////////////////////////////////////////////////////////////////////////

   // CPU I-Code bus
   wire   [31:0] haddri;
   wire    [1:0] htransi;
   wire    [2:0] hsizei;
   wire    [2:0] hbursti;
   wire    [3:0] hproti;
   wire    [1:0] memattri;
   wire   [31:0] hrdatai;
   wire          hreadyi;


   // CPU D-Code bus
   wire   [31:0] haddrd;
   wire    [1:0] htransd;
   wire    [1:0] hmasterd;
   wire    [2:0] hsized;
   wire    [2:0] hburstd;
   wire    [3:0] hprotd;
   wire    [1:0] memattrd;
   wire   [31:0] hwdatad;
   wire          hwrited;
   wire          exreqd;
   wire   [31:0] hrdatad;
   wire          hreadyd;
 
   wire          exrespd = 1'b0;

  
   /////////////////////////////////////////////////////////////////////////////
   // Connect System Bus to RAM and Peripherals
   /////////////////////////////////////////////////////////////////////////////

   // CPU System bus
   wire   [31:0] haddrs; 
   wire    [2:0] hbursts; 
   wire          hmastlocks; 
   wire    [3:0] hprots; 
   wire    [2:0] hsizes; 
   wire    [1:0] htranss; 
   wire   [31:0] hwdatas; 
   wire          hwrites; 
   wire   [31:0] hrdatas; 
   wire          hreadys; 

   wire          exresps = 1'b0;


   /////////////////////////////////////////////////////////////////////////////
   // Debug Signals
   /////////////////////////////////////////////////////////////////////////////

   // Debug signals (TDO pin is used for SWV unless JTAG mode is active)
   wire          dbg_tdo;                    // SWV / JTAG TDO
   wire          dbg_tdo_nen;                // SWV / JTAG TDO tristate enable (active low)
   wire          dbg_swdo;                   // SWD I/O 3-state output
   wire          dbg_swdo_en;                // SWD I/O 3-state enable
   wire          dbg_jtag_nsw;               // SWD in JTAG state (HIGH)
   wire          dbg_swo;                    // Serial wire viewer/output
   wire          tdo_enable     = !dbg_tdo_nen | !dbg_jtag_nsw;
   wire          tdo_tms        = dbg_jtag_nsw         ? dbg_tdo    : dbg_swo;
   assign        TMS            = dbg_swdo_en          ? dbg_swdo   : 1'bz;
   assign        TDO            = tdo_enable           ? tdo_tms    : 1'bz;

   // CoreSight requires a loopback from REQ to ACK for a minimal
   // debug power control implementation
   wire          cpu0cdbgpwrupreq;          // Debug Power Domain up request
   wire          cpu0cdbgpwrupack;          // Debug Power Domain up acknowledge
   assign        cpu0cdbgpwrupack = cpu0cdbgpwrupreq;

     //BusMatrix
  
    // output port mi0
    wire         hselmi0;          // slave select
    wire  [31:0] haddrmi0;         // address bus
    wire   [1:0] htransmi0;        // transfer type
    wire         hwritemi0;        // transfer direction
    wire   [2:0] hsizemi0;         // transfer size
    wire   [2:0] hburstmi0;        // burst type
    wire   [3:0] hprotmi0;         // protection control
    wire   [3:0] hmastermi0;       // master select
    wire  [31:0] hwdatami0;        // write data
    wire         hmastlockmi0;     // locked sequence
    wire         hreadymuxmi0;     // transfer done

    wire  [31:0] hrdatami0;        // read data bus
    wire         hreadyoutmi0;     // hready feedback
    wire   [1:0] hrespmi0;         // transfer response
    wire  [31:0] hausermi0;        // address user signals
    wire  [31:0] hwusermi0;        // write-data usER signals
    wire  [31:0] hrusermi0;        // read-data useR signals

    // output port mi1
    wire         hselmi1;          // slave select
    wire  [31:0] haddrmi1;         // address bus
    wire   [1:0] htransmi1;        // transfer type
    wire         hwritemi1;        // transfer direction
    wire   [2:0] hsizemi1;         // transfer size
    wire   [2:0] hburstmi1;        // burst type
    wire   [3:0] hprotmi1;         // protection control
    wire   [3:0] hmastermi1;       // master select
    wire  [31:0] hwdatami1;        // write data
    wire         hmastlockmi1;     // locked sequence
    wire         hreadymuxmi1;     // transfer done

    wire  [31:0] hrdatami1;        // read data bus
    wire         hreadyoutmi1;     // hready feedback
    wire   [1:0] hrespmi1;         // transfer response
    wire  [31:0] hausermi1;        // address user signals
    wire  [31:0] hwusermi1;        // write-data usER signals
    wire  [31:0] hrusermi1;        // read-data useR signals

    // output port mi2
    wire         hselmi2;          // slave select
    wire  [31:0] haddrmi2;         // address bus
    wire   [1:0] htransmi2;        // transfer type
    wire         hwritemi2;        // transfer direction
    wire   [2:0] hsizemi2;         // transfer size
    wire   [2:0] hburstmi2;        // burst type
    wire   [3:0] hprotmi2;         // protection control
    wire   [3:0] hmastermi2;       // master select
    wire  [31:0] hwdatami2;        // write data
    wire         hmastlockmi2;     // locked sequence
    wire         hreadymuxmi2;     // transfer done

    wire  [31:0] hrdatami2;        // read data bus
    wire         hreadyoutmi2;     // hready feedback
    wire   [1:0] hrespmi2;         // transfer response
    wire  [31:0] hausermi2;        // address user signals
    wire  [31:0] hwusermi2;        // write-data usER signal
    wire  [31:0] hrusermi2;        // read-data useR signals



   /////////////////////////////////////////////////////////////////////////////
   // Cortex-M0 Core
   /////////////////////////////////////////////////////////////////////////////

   // DesignStart simplified integration level
   CORTEXM3INTEGRATIONDS u_CORTEXM3INTEGRATION (
      // Inputs
      .ISOLATEn       (1'b1),               // Active low to isolate core power domain
      .RETAINn        (1'b1),               // Active low to retain core state during power-down

      // Resets
      .PORESETn       (RESETn),            // Power on reset - reset processor and debugSynchronous to FCLK and HCLK
      .SYSRESETn      (RESETn),      // System reset   - reset processor onlySynchronous to FCLK and HCLK
      .RSTBYPASS      (1'b0),               // Reset bypass - active high to disable internal generated reset for testing (e.gATPG)
      .CGBYPASS       (1'b0),               // Clock gating bypass - active high to disable internal clock gating for testing
      .SE             (1'b0),               // DFT is tied off in this example

      // Clocks
      .FCLK           (CLK),               // Free running clock - NVIC, SysTick, debug
      .HCLK           (CLK),               // System clock - AHB, processor
                                            // it is separated so that it can be gated off when no debugger is attached
      .TRACECLKIN     (1'B0),               // Trace clock input.  REVISIT, does it want its own named signal as an input?
      // SysTick
      .STCLK          (CLK),               // External reference clock for SysTick (Not really a clock, it is sampled by DFF)
                                            // Must be synchronous to FCLK or tied when no alternative clock source
      .STCALIB        ({1'b1,               // No alternative clock source
                        1'b0,               // Exact multiple of 10ms from FCLK
                        24'h08F}),      // Calibration value for SysTick for 25 MHz source

      .AUXFAULT       ({32{1'b0}}),         // Auxiliary Fault Status Register inputs: Connect to fault status generating logic
                                            // if required. Result appears in the Auxiliary Fault Status Register at address
                                            // 0xE000ED3C. A one-cycle pulse of information results in the information being stored
                                            // in the corresponding bit until a write-clear occurs.

      // Configuration - system
      .BIGEND         (1'b0),               // Select when exiting system reset - Peripherals in this system do not support BIGEND
      .DNOTITRANS     (1'b1),               // I-CODE & D-CODE merging configuration.
                                            // This disable I-CODE from generating a transfer when D-CODE bus need a transfer
                                            // Must be HIGH when using the Designstart system

      // SWJDAP signal for single processor mode
      .nTRST          (1'b1),               // JTAG TAP Reset
      .SWCLKTCK       (TCK),                // SW/JTAG Clock
      .SWDITMS        (TMS),                // SW Debug Data In / JTAG Test Mode Select
      .TDI            (TDI),                // JTAG TAP Data In / Alternative input function
      .CDBGPWRUPACK   (cpu0cdbgpwrupack),   // Debug Power Domain up acknowledge.

      // IRQs
      .INTISR         (irq[239:0]),         // Interrupts
      .INTNMI         (1'b0),               // Non-maskable Interrupt

      // I-CODE Bus
      .HREADYI        (hreadyi),            // I-CODE bus ready
      .HRDATAI        (hrdatai),            // I-CODE bus read data
      .HRESPI         (2'b00),             // I-CODE bus response
      .IFLUSH         (1'b0),               // Prefetch flush - fixed when using the Designstart system

      // D-CODE Bus
      .HREADYD        (hreadyd),            // D-CODE bus ready
      .HRDATAD        (hrdatad),            // D-CODE bus read data
      .HRESPD         (2'b00),             // D-CODE bus response
      .EXRESPD        (exrespd),            // D-CODE bus exclusive response

      // System Bus
      .HREADYS        (hreadys),            // System bus ready
      .HRDATAS        (hrdatas),            // System bus read data
      .HRESPS         (2'b00),             // System bus response
      .EXRESPS        (exresps),            // System bus exclusive response

      // Sleep
      .RXEV           (1'b0),               // Receive Event input
      .SLEEPHOLDREQn  (1'b1),               // Extend Sleep request

      // External Debug Request
      .EDBGRQ         (1'b0),               // External Debug request to CPU
      .DBGRESTART     (1'b0),               // Debug Restart request - Not needed in a single CPU system

      // DAP HMASTER override
      .FIXMASTERTYPE  (1'b0),               // Tie High to override HMASTER for AHB-AP accesses

      // WIC
      .WICENREQ       (1'b0),               // Active HIGH request for deep sleep to be WIC-based deep sleep
                                            // This should be driven from a PMU

      // Timestamp interface
      .TSVALUEB       ({48{1'b0}}),         // Binary coded timestamp value for trace - Trace is not used in this course
      // Timestamp clock ratio change is rarely used

      // Configuration - debug
      .DBGEN          (1'b1),               // Halting Debug Enable
      .NIDEN          (1'b1),               // Non-invasive debug enable for ETM
      .MPUDISABLE     (1'b0),               // Tie high to emulate processor with no MPU

      // SWJDAP signal for single processor mode
      .TDO            (dbg_tdo),            // JTAG TAP Data Out // REVISIT needs mux for SWV
      .nTDOEN         (dbg_tdo_nen),        // TDO enable
      .CDBGPWRUPREQ   (cpu0cdbgpwrupreq),   // Debug Power Domain up request
      .SWDO           (dbg_swdo),           // SW Data Out
      .SWDOEN         (dbg_swdo_en),        // SW Data Out Enable
      .JTAGNSW        (dbg_jtag_nsw),       // JTAG/not Serial Wire Mode

      // Single Wire Viewer
      .SWV            (dbg_swo),            // SingleWire Viewer Data

      // TPIU signals for single processor mode
      .TRACECLK       (),                   // TRACECLK output
      .TRACEDATA      (),                   // Trace Data

      // CoreSight AHB Trace Macrocell (HTM) bus capture interface
      // Connected here for visibility but usually not used in SoC.
      .HTMDHADDR      (),                   // HTM data HADDR
      .HTMDHTRANS     (),                   // HTM data HTRANS
      .HTMDHSIZE      (),                   // HTM data HSIZE
      .HTMDHBURST     (),                   // HTM data HBURST
      .HTMDHPROT      (),                   // HTM data HPROT
      .HTMDHWDATA     (),                   // HTM data HWDATA
      .HTMDHWRITE     (),                   // HTM data HWRITE
      .HTMDHRDATA     (),                   // HTM data HRDATA
      .HTMDHREADY     (),                   // HTM data HREADY
      .HTMDHRESP      (),                   // HTM data HRESP

      // AHB I-Code bus
      .HADDRI         (haddri),             // I-CODE bus address
      .HTRANSI        (htransi),            // I-CODE bus transfer type
      .HSIZEI         (hsizei),             // I-CODE bus transfer size
      .HBURSTI        (hbursti),            // I-CODE bus burst length
      .HPROTI         (hproti),             // i-code bus protection
      .MEMATTRI       (memattri),           // I-CODE bus memory attributes

      // AHB D-Code bus
      .HADDRD         (haddrd),             // D-CODE bus address
      .HTRANSD        (htransd),            // D-CODE bus transfer type
      .HSIZED         (hsized),             // D-CODE bus transfer size
      .HWRITED        (hwrited),            // D-CODE bus write not read
      .HBURSTD        (hburstd),            // D-CODE bus burst length
      .HPROTD         (hprotd),             // D-CODE bus protection
      .MEMATTRD       (memattrd),           // D-CODE bus memory attributes
      .HMASTERD       (hmasterd),           // D-CODE bus master
      .HWDATAD        (hwdatad),            // D-CODE bus write data
      .EXREQD         (exreqd),             // D-CODE bus exclusive request

      // AHB System bus
      .HADDRS         (haddrs),             // System bus address
      .HTRANSS        (htranss),            // System bus transfer type
      .HSIZES         (hsizes),             // System bus transfer size
      .HWRITES        (hwrites),            // System bus write not read
      .HBURSTS        (hbursts),            // System bus burst length
      .HPROTS         (hprots),             // System bus protection
      .HMASTLOCKS     (hmastlocks),         // System bus lock
      .MEMATTRS       (),                   // System bus memory attributes
      .HMASTERS       (),                   // System bus master
      .HWDATAS        (hwdatas),            // System bus write data
      .EXREQS         (),                   // System bus exclusive request

      // Status
      .BRCHSTAT       (),                   // Branch State
      .HALTED         (),                   // The processor is halted
      .DBGRESTARTED   (),                   // Debug Restart interface handshaking
      .LOCKUP         (lockup),             // The processor is locked up
      .SLEEPING       (),                   // The processor is in sleep mdoe (sleep/deep sleep)
      .SLEEPDEEP      (),                   // The processor is in deep sleep mode
      .SLEEPHOLDACKn  (),                   // Acknowledge for SLEEPHOLDREQn
      .ETMINTNUM      (),                   // Current exception number
      .ETMINTSTAT     (),                   // Exception/Interrupt activation status
      .CURRPRI        (),                   // Current exception priority
      .TRCENA         (),                   // Trace Enable

      // Reset Request
      .SYSRESETREQ    (),      // System Reset Request

      // Events
      .TXEV           (),                   // Transmit Event

      // Clock gating control
      .GATEHCLK       (),                   // when high, HCLK can be turned off

      .WAKEUP         (),                   // Active HIGH signal from WIC to the PMU that indicates a wake-up event has
                                            // occurred and the system requires clocks and power
      .WICENACK       ()                    // Acknowledge for WICENREQ - WIC operation deep sleep mode
   );


   //BusMatrix instantiation

   BusMatrix3x3 u_BusMatrix3x3 (

    // Common AHB signals
    .HCLK		(CLK),
    .HRESETn		(RESETn),

    // System address remapping control
    .REMAP		({4{1'b0}}),

    // Input port SI0 (inputs from master 0)
    .HSELSI0			(htranss[1]),
    .HADDRSI0			(haddrs),
    .HTRANSSI0		(htranss),
    .HWRITESI0		(hwrites),
    .HSIZESI0			(hsizes),
    .HBURSTSI0		(hbursts),
    .HPROTSI0			(hprots),
    .HMASTERSI0		(4'b0000),
    .HWDATASI0		(hwdatas),
    .HMASTLOCKSI0	(1'b0),
    .HREADYSI0		(hreadys),
    .HAUSERSI0		({32{1'b0}}),
    .HWUSERSI0		({32{1'b0}}),

    
    // Input port SI1 (inputs from master 1)
    .HSELSI1			(htransd[1]),
    .HADDRSI1			(haddrd),
    .HTRANSSI1		(htransd),
    .HWRITESI1		(hwrited),
    .HSIZESI1			(hsized),
    .HBURSTSI1		(hburstd),
    .HPROTSI1			(hprotd),
    .HMASTERSI1		(4'b0001),
    .HWDATASI1		(hwdatad),
    .HMASTLOCKSI1	(1'b0),
    .HREADYSI1		(hreadyd),
    .HAUSERSI1		({32{1'b0}}),
    .HWUSERSI1		({32{1'b0}}),

    // Input port SI2 (inputs from master 2)
    .HSELSI2			(htransi[1]),
    .HADDRSI2			(haddri),
    .HTRANSSI2		(htransi),
    .HWRITESI2		(1'b0),
    .HSIZESI2			(hsizei),
    .HBURSTSI2		(hbursti),
    .HPROTSI2			(hproti),
    .HMASTERSI2		(4'b0010),
    .HWDATASI2		({32{1'b0}}),
    .HMASTLOCKSI2	(1'b0),
    .HREADYSI2		(hreadyi),
    .HAUSERSI2		({32{1'b0}}),
    .HWUSERSI2		({32{1'b0}}),


    // Output port MI0 (inputs from slave 0)
    .HRDATAMI0		(hrdatami0),
    .HREADYOUTMI0	(hreadyoutmi0),
    .HRESPMI0			(2'b00),
    .HRUSERMI0		(32'b0),

    // Output port MI1 (inputs from slave 1)
    .HRDATAMI1		(hrdatami1),
    .HREADYOUTMI1	(hreadyoutmi1),
    .HRESPMI1			(2'b00),
    .HRUSERMI1		(32'b0),

    // Output port MI2 (inputs from slave 2)
    .HRDATAMI2		(audio_hrdata),
    .HREADYOUTMI2	(audio_hreadyout),
    .HRESPMI2			(2'b00),
    .HRUSERMI2		(32'b0),

    // Scan test dummy signals; not connected until scan insertion
    .SCANENABLE		(1'b0),   // Scan Test Mode Enable
    .SCANINHCLK		(1'b0),   // Scan Chain Input


    // Output port MI0 (outputs to slave 0)
    .HSELMI0			(hselmi0		),
    .HADDRMI0			(haddrmi0		),
    .HTRANSMI0		(htransmi0	),
    .HWRITEMI0		(hwritemi0	),
    .HSIZEMI0			(hsizemi0		),
    .HBURSTMI0		(hburstmi0	),
    .HPROTMI0			(hprotmi0		),
    .HMASTERMI0		(hmastermi0	),
    .HWDATAMI0		(hwdatami0	),
    .HMASTLOCKMI0	(hmastlockmi0),
    .HREADYMUXMI0	(hreadymi0	),
    .HAUSERMI0		(hausermi0	),
    .HWUSERMI0		(hwusermi0	),

    // Output port MI1 (outputs to slave 1)
    .HSELMI1			(hselmi1		),
    .HADDRMI1			(haddrmi1		),
    .HTRANSMI1		(htransmi1	),
    .HWRITEMI1		(hwritemi1	),
    .HSIZEMI1			(hsizemi1		),
    .HBURSTMI1		(hburstmi1	),
    .HPROTMI1			(hprotmi1		),
    .HMASTERMI1		(hmastermi1	),
    .HWDATAMI1		(hwdatami1	),
    .HMASTLOCKMI1	(hmastlockmi1),
    .HREADYMUXMI1	(hreadymi1	),
    .HAUSERMI1		(hausermi1	),
    .HWUSERMI1		(hwusermi1	),

    // Output port MI2 (outputs to slave 2)
    .HSELMI2			(hselmi2		),
    .HADDRMI2			(haddrmi2		),
    .HTRANSMI2		(htransmi2	),
    .HWRITEMI2		(hwritemi2	),
    .HSIZEMI2			(hsizemi2		),
    .HBURSTMI2		(hburstmi2	),
    .HPROTMI2			(hprotmi2		),
    .HMASTERMI2		(hmastermi2	),
    .HWDATAMI2		(hwdatami2	),
    .HMASTLOCKMI2	(hmastlockmi2),
    .HREADYMUXMI2	(hreadymi2	),
    .HAUSERMI2		(hausermi2	),
    .HWUSERMI2		(hwusermi2	),

    // Input port SI0 (outputs to master 0)
    .HRDATASI0		(hrdatas		),
    .HREADYOUTSI0	(hreadys		),
    .HRESPSI0			(),
    .HRUSERSI0		(),

    // Input port SI1 (outputs to master 1)
    .HRDATASI1		(hrdatad		),
    .HREADYOUTSI1	(hreadyd		),
    .HRESPSI1			(),
    .HRUSERSI1		(),

     // Input port SI2 (outputs to master 2)
    .HRDATASI2		(hrdatai		),
    .HREADYOUTSI2	(hreadyi		),
    .HRESPSI2			(),
    .HRUSERSI2		(),

    // Scan test dummy signals; not connected until scan insertion
    .SCANOUTHCLK	()  
);

//SRAM instantiation

AHB2MEM
   #(16)  U_SRAM 
   (
   .HSEL			(hselmi0		),
   .HCLK			(CLK				),
   .HRESETn		(RESETn			),
   .HREADY		(hreadymi0	),
   .HADDR			(haddrmi0		),
   .HTRANS		(htransmi0	),
   .HWRITE		(hwritemi0	),
   .HSIZE			(hsizemi0		),
   .HWDATA		(hwdatami0	),
   .HREADYOUT	(hreadyoutmi0),
   .HRDATA		(hrdatami0	)
   );
   
assign hrespmi0[1:0]=2'b0;

//apb_subsystem instantiation
cmsdk_apb_subsystem #(
	.APB_EXT_PORT12_ENABLE (1),
	.APB_EXT_PORT13_ENABLE (1)
) u_apb_subsystem (
	.HCLK						(CLK				),
	.HRESETn				(RESETn			),
              		             
	.HSEL						(hselmi1		),
	.HADDR					(haddrmi1[15:0]),
	.HTRANS					(htransmi1	),
	.HWRITE					(hwritemi1	),
	.HSIZE					(hsizemi1		),
	.HPROT					(hprotmi1		),
	.HREADY					(hreadymi1	),
	.HWDATA					(hwdatami1	),
              		                   
	.HREADYOUT			(hreadyoutmi1),
	.HRDATA					(hrdatami1	),
	.HRESP					(hrespmi1[1]),
                                                
	.PCLK						(CLK				),    
	.PCLKG					(CLK				),  
	.PCLKEN					(1'b1				),  
	.PRESETn				(RESETn			), 

	.PADDR					(apb_paddr		),
	.PWRITE					(apb_pwrite		),
	.PWDATA					(apb_pwdata		),
	.PENABLE				(apb_penable	),
                                     
	.ext12_psel			(ext12_psel		),
	.ext13_psel			(ext13_psel		),
	.ext14_psel			(),
	.ext15_psel			(),
                                     
	.ext12_prdata		(ext12_prdata	),
	.ext12_pready		(ext12_pready	),
	.ext12_pslverr	(ext12_pslverr),
                                    
	.ext13_prdata		(ext13_prdata	),
	.ext13_pready		(ext13_pready	),
	.ext13_pslverr	(ext13_pslverr),
                                       
	.ext14_prdata		(),
	.ext14_pready		(),
	.ext14_pslverr	(),
	                             
	.ext15_prdata		(),
	.ext15_pready		(),
	.ext15_pslverr	(),

	.b_pad_gpio_porta	(b_pad_gpio_porta[7:0]),
	                                       
	.uart1_rxd			(uart1_rxd		),
	.uart1_txd			(uart1_txd		),
	.uart1_txen			(uart1_txen		),
                	                                
	.uart2_rxd			(uart2_rxd		),
	.uart2_txd			(uart2_txd		),
	.uart2_txen			(uart2_txen		),
               
	.timer0_extin		(timer0_extin	),
	.timer1_extin		(timer1_extin	),
	.apbsubsys_interrupt (apb_int	)
	);

assign hrespmi1[0]=1'b0;

// APB extension port 12: ARM PL022 SSP controller for external SPI flash.
// APB address decode uses apb_paddr[15:12], so ext12 occupies the APB slot
// selected by nibble 0xC. In the current system this is used as the SPI
// controller register window for software polling.
assign ext12_prdata = {16'b0, ssp_prdata};
assign ext12_pready = 1'b1;
assign ext12_pslverr = 1'b0;

// APB extension port 13: one-bit manual chip select for SPI flash.
// The PL022 FSS output can pulse between characters depending on FIFO timing;
// flash erase/program commands require CS to stay low for the whole command.
assign ext13_prdata = {31'b0, spi_flash_cs_n_reg};
assign ext13_pready = 1'b1;
assign ext13_pslverr = 1'b0;

always @(posedge CLK or negedge RESETn) begin
	if (!RESETn) begin
		spi_flash_cs_n_reg <= 1'b1;
	end else if (ext13_psel && apb_penable && apb_pwrite && (apb_paddr[11:2] == 10'h000)) begin
		spi_flash_cs_n_reg <= apb_pwdata[0];
	end
end

assign spi_flash_mosi = ssp_txd;
assign spi_flash_sck  = ssp_clkout;
assign spi_flash_cs_n = spi_flash_cs_n_reg;

Ssp u_spi_flash_ssp (
	.PCLK          (CLK                 ),
	.SSPCLK        (CLK                 ),
	.PRESETn       (RESETn              ),
	.nSSPRST       (RESETn              ),
	.PSEL          (ext12_psel          ),
	.PENABLE       (apb_penable         ),
	.PWRITE        (apb_pwrite          ),
	.SSPRXD        (spi_flash_miso      ),
	.SSPFSSIN      (1'b1                ),
	.SSPCLKIN      (1'b0                ),
	.SCANENABLE    (1'b0                ),
	.SCANINPCLK    (1'b0                ),
	.SCANINSSPCLK  (1'b0                ),
	.PADDR         (apb_paddr[11:2]     ),
	.PWDATA        (apb_pwdata[15:0]    ),
	.SSPTXDMACLR   (1'b0                ),
	.SSPRXDMACLR   (1'b0                ),
	.SSPINTR       (ssp_intr            ),
	.SSPRXINTR     (ssp_rx_intr         ),
	.SSPTXINTR     (ssp_tx_intr         ),
	.SSPRORINTR    (ssp_rx_overrun_intr ),
	.SSPRTINTR     (ssp_rx_timeout_intr ),
	.SSPFSSOUT     (ssp_fssout          ),
	.SSPCLKOUT     (ssp_clkout          ),
	.SCANOUTPCLK   (ssp_scanout_pclk    ),
	.SCANOUTSSPCLK (ssp_scanout_sspclk  ),
	.SSPTXD        (ssp_txd             ),
	.nSSPOE        (ssp_n_oe            ),
	.nSSPCTLOE     (ssp_n_ctl_oe        ),
	.PRDATA        (ssp_prdata          ),
	.SSPTXDMASREQ  (ssp_tx_dma_sreq     ),
	.SSPTXDMABREQ  (ssp_tx_dma_breq     ),
	.SSPRXDMASREQ  (ssp_rx_dma_sreq     ),
	.SSPRXDMABREQ  (ssp_rx_dma_breq     )
);

// I2S receiver integration
// For the current coursework stage, the I2S block is connected directly at top
// level and writes incoming samples into a local synchronous FIFO.
i2s_rx_inmp441 u_i2s_rx_inmp441 (
	.clk_50m      (CLK              ),
	.rst_n        (RESETn           ),
	.rx_enable    (audio_rx_enable  ),
	.clear        (audio_i2s_clear  ),
	.mic_sd       (mic_sd           ),
	.mic_sck      (mic_sck          ),
	.mic_ws       (mic_ws           ),
	.sample_valid (i2s_sample_valid ),
	.sample_data  (i2s_sample_data  ),
	.fifo_wr_en   (i2s_fifo_wr_en   ),
	.fifo_wr_data (i2s_fifo_wr_data )
);

// I2S sample FIFO
// The write side is driven by the I2S receiver. The read side is selected
// between direct AHB PCM reads and the ADPCM compression path.
assign i2s_fifo_adpcm_rd_en = audio_compress_enable & !i2s_fifo_empty & !adpcm_fifo_full;
assign i2s_fifo_rd_en = audio_compress_enable ? i2s_fifo_adpcm_rd_en : i2s_fifo_ahb_rd_en;

fifo_sync #(
	.DATA_WIDTH (16),
	.ADDR_WIDTH (10)
) u_i2s_fifo (
	.clk     (CLK             ),
	.rst_n   (RESETn          ),
	.clr     (audio_fifo_clear | audio_i2s_clear),
	.wr_en   (i2s_fifo_wr_en  ),
	.wr_data (i2s_fifo_wr_data),
	.rd_en   (i2s_fifo_rd_en  ),
	.rd_data (i2s_fifo_rd_data),
	.full    (i2s_fifo_full   ),
	.empty   (i2s_fifo_empty  ),
	.usedw   (i2s_fifo_usedw  )
);

// IMA ADPCM encoder with built-in 4-bit-to-32-bit packing.
ima_adpcm_encoder u_ima_adpcm_encoder (
	.clk              (CLK                    ),
	.rst_n            (RESETn                 ),
	.enable           (audio_compress_enable  ),
	.clear            (audio_fifo_clear | audio_i2s_clear),
	.pcm_valid        (i2s_fifo_adpcm_rd_en   ),
	.pcm_sample       (i2s_fifo_rd_data       ),
	.adpcm_word_valid (adpcm_word_valid       ),
	.adpcm_word       (adpcm_word_data        )
);

// Packed ADPCM FIFO. Each word contains eight 4-bit IMA ADPCM codes.
fifo_sync #(
	.DATA_WIDTH (32),
	.ADDR_WIDTH (10)
) u_adpcm_fifo (
	.clk     (CLK                             ),
	.rst_n   (RESETn                          ),
	.clr     (audio_fifo_clear | audio_i2s_clear),
	.wr_en   (adpcm_word_valid                ),
	.wr_data (adpcm_word_data                 ),
	.rd_en   (adpcm_fifo_rd_en                ),
	.rd_data (adpcm_fifo_rd_data              ),
	.full    (adpcm_fifo_full                 ),
	.empty   (adpcm_fifo_empty                ),
	.usedw   (adpcm_fifo_usedw                )
);

// Audio AHB slave
// This block is connected to BusMatrix MI2, which is decoded to
// 0x2001_0000 - 0x2001_FFFF in the current system.
audio_ahb_if u_audio_ahb_if (
	.HSEL       (hselmi2         ),
	.HCLK       (CLK             ),
	.HRESETn    (RESETn          ),
	.HREADY     (hreadymi2       ),
	.HADDR      (haddrmi2        ),
	.HTRANS     (htransmi2       ),
	.HWRITE     (hwritemi2       ),
	.HSIZE      (hsizemi2        ),
	.HWDATA     (hwdatami2       ),
	.HREADYOUT  (audio_hreadyout ),
	.HRDATA     (audio_hrdata    ),
	.rx_enable  (audio_rx_enable ),
	.i2s_clear  (audio_i2s_clear ),
	.fifo_clear (audio_fifo_clear),
	.compress_enable(audio_compress_enable),
	.sample_valid(i2s_sample_valid),
	.pcm_fifo_empty (i2s_fifo_empty  ),
	.pcm_fifo_full  (i2s_fifo_full   ),
	.pcm_fifo_usedw (i2s_fifo_usedw  ),
	.pcm_fifo_rd_data(i2s_fifo_rd_data),
	.pcm_fifo_rd_en (i2s_fifo_ahb_rd_en),
	.adpcm_fifo_empty (adpcm_fifo_empty),
	.adpcm_fifo_full  (adpcm_fifo_full ),
	.adpcm_fifo_usedw (adpcm_fifo_usedw),
	.adpcm_fifo_rd_data(adpcm_fifo_rd_data),
	.adpcm_fifo_rd_en (adpcm_fifo_rd_en)
);

endmodule
