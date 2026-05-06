`timescale 1ns/1ns
`define PERIOD 20


module test;

reg clk;
reg resetn;
reg tdi, tck;
wire tms, tdo;
wire [7:0] b_pad_gpio_porta;
wire uart1_rxd, uart2_rxd;
wire uart1_txd, uart2_txd;
wire mic_sd;
wire mic_sck, mic_ws;
wire spi_flash_miso;
wire spi_flash_mosi, spi_flash_sck, spi_flash_cs_n;
wire timer0_extin, timer1_extin;
reg  last_audio_rx_enable;
reg  audio_write_pending;
reg [31:0] audio_write_addr;

// Behavioral model interconnect
wire flash_so;     // raw MISO from flash model (tri-state)
wire mic_model_sd; // raw SD from mic model

// Simulation controls
integer sim_cycle;
integer timeout_cycles;

 top  u_soc(
    .CLK            (clk),
    .RESETn         (resetn),
    .TDI            (tdi),
    .TCK            (tck),
    .TMS            (tms),
    .TDO            (tdo),
    .b_pad_gpio_porta(b_pad_gpio_porta[7:0]),
    .uart1_rxd      (uart1_rxd),
    .uart2_rxd      (uart2_rxd),
    .uart1_txd      (uart1_txd),
    .uart2_txd      (uart2_txd),
    .mic_sd         (mic_sd),
    .mic_sck        (mic_sck),
    .mic_ws         (mic_ws),
    .spi_flash_miso (spi_flash_miso),
    .spi_flash_mosi (spi_flash_mosi),
    .spi_flash_sck  (spi_flash_sck),
    .spi_flash_cs_n (spi_flash_cs_n),
    .timer0_extin   (timer0_extin),
    .timer1_extin   (timer1_extin));

// Idle input defaults
assign uart1_rxd   = 1'b1;
assign uart2_rxd   = 1'b1;
assign timer0_extin = 1'b0;
assign timer1_extin = 1'b0;

// MISO: pass through with weak pull-up when flash is deselected (so = high-Z)
assign spi_flash_miso = (flash_so === 1'bz) ? 1'b1 : flash_so;

// Microphone SD: driven by INMP441 behavioral model
assign mic_sd = mic_model_sd;


// =========================================================================
// Behavioral model: W25Q64 SPI Flash (64 Mbit)
// Connected to the PL022 SSP controller via top-level SPI flash ports.
// =========================================================================
w25q64_model #(
    .MEM_KB   (128),       // 128 KB internal memory for simulation
    .BUSY_SCK (50)         // 50 SCK cycles of BUSY after program/erase
) u_w25q64 (
    .sck  (spi_flash_sck),
    .cs_n (spi_flash_cs_n),
    .si   (spi_flash_mosi),
    .so   (flash_so)
);

// =========================================================================
// Behavioral model: INMP441 I2S MEMS Microphone
// Connected to the I2S receiver via top-level mic ports.
// HEX_FILE path is relative to the vsim launch directory (modelsim/).
// =========================================================================
inmp441_model #(
    .MAX_SAMPLES (282624),
    .HEX_FILE    ("../soc-wuxi-arm/hw/sample_hex.txt")
) u_inmp441 (
    .sck (mic_sck),
    .ws  (mic_ws),
    .sd  (mic_model_sd)
);


// Clock generation
always #(`PERIOD/2) clk = ~clk;

// Simulation cycle counter (for timeout)
always @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        sim_cycle <= 0;
    end else begin
        sim_cycle <= sim_cycle + 1;
    end
end

// Lockup detection — if CPU locks up, report it
always @(posedge clk) begin
    if (u_soc.lockup) begin
        $display("[%0t] ** WARNING: CPU lockup asserted !", $time);
    end
end

// Audio bus monitor. If the firmware reaches AUDIO_BASE=0x2001_0000,
// these lines make the transaction visible in the transcript.
always @(posedge clk or negedge resetn) begin
    if (!resetn) begin
        last_audio_rx_enable <= 1'b0;
        audio_write_pending <= 1'b0;
        audio_write_addr <= 32'b0;
    end else begin
        if (audio_write_pending) begin
            $display("[%0t] AUDIO AHB WRITE addr=0x%08h data=0x%08h",
                     $time, audio_write_addr, u_soc.hwdatami2);
            audio_write_pending <= 1'b0;
        end

        if (u_soc.hselmi2 && u_soc.htransmi2[1]) begin
            if (u_soc.hwritemi2) begin
                audio_write_pending <= 1'b1;
                audio_write_addr <= u_soc.haddrmi2;
            end else begin
                $display("[%0t] AUDIO AHB READ  addr=0x%08h data=0x%08h",
                         $time, u_soc.haddrmi2, u_soc.audio_hrdata);
            end
        end

        if (u_soc.audio_rx_enable !== last_audio_rx_enable) begin
            $display("[%0t] audio_rx_enable -> %0b", $time, u_soc.audio_rx_enable);
            last_audio_rx_enable <= u_soc.audio_rx_enable;
        end
    end
end

initial begin
    clk      = 1;
    resetn   = 0;
    tdi      = 1'b0;
    tck      = 1'b0;
    timeout_cycles = 0;

    // Try to load firmware. The path is relative to the directory where
    // vsim is launched. The default path assumes launching from the
    // modelsim/ directory (where arm_soc.mpf lives).
    // If running from a different directory, use the FIRMWARE_HEX define:
    //   vlog +define+FIRMWARE_HEX=\"../cnasic_sleep/.../outfile.bin\" test.v
`ifdef FIRMWARE_HEX
    $readmemh(`FIRMWARE_HEX, u_soc.U_SRAM.memory);
`else
    $readmemh("../cnasic_sleep/cnasic_sleep/prj/keil/output/outfile.bin", u_soc.U_SRAM.memory);
`endif
    $display("[%0t] *  RAM loaded successfully !", $time);

    // Hold reset for 20 clock cycles
    #(`PERIOD*20) resetn = 1;
    $display("[%0t] *  Reset de-asserted, CPU starting...", $time);
end

// UART TX monitor — print any character sent by the CPU via UART1
// (useful if firmware uses printf-style debugging)
`ifdef MONITOR_UART
// Simple UART monitor at 115200 baud (8.68 us per bit)
// This is a basic capture — for detailed analysis use waveform viewer.
reg [7:0] uart_rx_byte;
reg [3:0] uart_rx_bit;
reg       uart_rx_busy;

always @(negedge uart1_txd) begin
    if (!uart_rx_busy) begin
        uart_rx_busy <= 1;
        uart_rx_bit  <= 0;
        // Wait half bit time to sample in the middle
        #(4340);
        for (uart_rx_bit = 0; uart_rx_bit < 8; uart_rx_bit = uart_rx_bit + 1) begin
            #(8680);
            uart_rx_byte[uart_rx_bit] = uart1_txd;
        end
        #(8680);  // Stop bit
        $write("%c", uart_rx_byte);
        uart_rx_busy <= 0;
    end
end
`endif

// Simulation timeout — stops simulation after a configurable number of cycles.
// Default is 10 million cycles = 200 ms at 50 MHz, which is enough for
// complex firmware to finish a meaningful task.
initial begin
    timeout_cycles = 10000000;
`ifdef SIM_TIMEOUT_CYCLES
    timeout_cycles = `SIM_TIMEOUT_CYCLES;
`endif
    #(`PERIOD * timeout_cycles);
    $display("\n[%0t] *  Simulation timeout (%0d cycles), stopping.", $time, timeout_cycles);
    $display("[%0t] *  Total simulation cycles: %0d", $time, sim_cycle);
    $finish;
end

// Optional waveform dump (uncomment to enable FSDB output)
//initial begin
//    $fsdbDumpfile("output.fsdb");
//    $fsdbDumpvars;
//end

endmodule
