`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Testbench: tb_spi_pl022
// Function:
//   1. Configure the ARM PL022 SSP through its APB register interface.
//   2. Write one 8-bit byte into SSPDR.
//   3. Loop MOSI back to MISO to emulate a simple SPI slave returning the same
//      byte.
//   4. Check that SCK toggles, MOSI transmits the expected byte, and APB can
//      read the received byte back from SSPDR.
//
// This testbench does not model the full W25Q64 command set. It verifies the
// currently integrated SPI controller path at register/pin level.
// -----------------------------------------------------------------------------
module tb_spi_pl022;

    localparam [9:0] ADDR_CR0  = 10'h000; // 0x00 >> 2
    localparam [9:0] ADDR_CR1  = 10'h001; // 0x04 >> 2
    localparam [9:0] ADDR_DR   = 10'h002; // 0x08 >> 2
    localparam [9:0] ADDR_SR   = 10'h003; // 0x0C >> 2
    localparam [9:0] ADDR_CPSR = 10'h004; // 0x10 >> 2

    localparam [7:0] TX_BYTE   = 8'hA5;

    reg         pclk;
    reg         reset_n;
    reg         psel;
    reg         penable;
    reg         pwrite;
    reg  [11:2] paddr;
    reg  [15:0] pwdata;

    wire [15:0] prdata;
    wire        spi_flash_cs_n;
    wire        spi_flash_sck;
    wire        spi_flash_mosi;
    wire        spi_flash_miso;

    wire        ssp_intr;
    wire        ssp_rx_intr;
    wire        ssp_tx_intr;
    wire        ssp_ror_intr;
    wire        ssp_rt_intr;
    wire        scanout_pclk;
    wire        scanout_sspclk;
    wire        n_ssp_oe;
    wire        n_ssp_ctl_oe;
    wire        ssp_tx_dma_sreq;
    wire        ssp_tx_dma_breq;
    wire        ssp_rx_dma_sreq;
    wire        ssp_rx_dma_breq;

    integer     error_count;
    integer     poll_count;
    integer     sck_edge_count;
    reg [15:0]  read_data;
    reg [7:0]   mosi_shift;
    integer     mosi_bit_count;

    // Loopback model: the transmitted MOSI bit is sampled back through MISO.
    assign spi_flash_miso = spi_flash_mosi;

    Ssp u_spi_flash_ssp (
        .PCLK          (pclk),
        .SSPCLK        (pclk),
        .PRESETn       (reset_n),
        .nSSPRST       (reset_n),
        .PSEL          (psel),
        .PENABLE       (penable),
        .PWRITE        (pwrite),
        .SSPRXD        (spi_flash_miso),
        .SSPFSSIN      (1'b1),
        .SSPCLKIN      (1'b0),
        .SCANENABLE    (1'b0),
        .SCANINPCLK    (1'b0),
        .SCANINSSPCLK  (1'b0),
        .PADDR         (paddr),
        .PWDATA        (pwdata),
        .SSPTXDMACLR   (1'b0),
        .SSPRXDMACLR   (1'b0),
        .SSPINTR       (ssp_intr),
        .SSPRXINTR     (ssp_rx_intr),
        .SSPTXINTR     (ssp_tx_intr),
        .SSPRORINTR    (ssp_ror_intr),
        .SSPRTINTR     (ssp_rt_intr),
        .SSPFSSOUT     (spi_flash_cs_n),
        .SSPCLKOUT     (spi_flash_sck),
        .SCANOUTPCLK   (scanout_pclk),
        .SCANOUTSSPCLK (scanout_sspclk),
        .SSPTXD        (spi_flash_mosi),
        .nSSPOE        (n_ssp_oe),
        .nSSPCTLOE     (n_ssp_ctl_oe),
        .PRDATA        (prdata),
        .SSPTXDMASREQ  (ssp_tx_dma_sreq),
        .SSPTXDMABREQ  (ssp_tx_dma_breq),
        .SSPRXDMASREQ  (ssp_rx_dma_sreq),
        .SSPRXDMABREQ  (ssp_rx_dma_breq)
    );

    always #10 pclk = ~pclk; // 50 MHz

    always @(posedge spi_flash_sck or negedge reset_n) begin
        if (!reset_n) begin
            sck_edge_count <= 0;
            mosi_shift     <= 8'h00;
            mosi_bit_count <= 0;
        end else begin
            sck_edge_count <= sck_edge_count + 1;

            if (mosi_bit_count < 8) begin
                mosi_shift     <= {mosi_shift[6:0], spi_flash_mosi};
                mosi_bit_count <= mosi_bit_count + 1;
            end
        end
    end

    task apb_idle;
        begin
            psel    = 1'b0;
            penable = 1'b0;
            pwrite  = 1'b0;
            paddr   = 10'h000;
            pwdata  = 16'h0000;
        end
    endtask

    task apb_write;
        input [9:0]  addr_word;
        input [15:0] data;
        begin
            @(negedge pclk);
            psel    = 1'b1;
            penable = 1'b0;
            pwrite  = 1'b1;
            paddr   = addr_word;
            pwdata  = data;

            @(negedge pclk);
            penable = 1'b1;

            @(negedge pclk);
            apb_idle;
        end
    endtask

    task apb_read;
        input  [9:0]  addr_word;
        output [15:0] data;
        begin
            @(negedge pclk);
            psel    = 1'b1;
            penable = 1'b0;
            pwrite  = 1'b0;
            paddr   = addr_word;
            pwdata  = 16'h0000;

            @(negedge pclk);
            penable = 1'b1;

            @(posedge pclk);
            data = prdata;

            @(negedge pclk);
            apb_idle;
        end
    endtask

    initial begin
        pclk           = 1'b0;
        reset_n        = 1'b0;
        error_count    = 0;
        poll_count     = 0;
        read_data      = 16'h0000;
        apb_idle;

        #200;
        reset_n = 1'b1;

        // SPI mode 0, 8-bit data. SCR=0.
        apb_write(ADDR_CR0, 16'h0007);

        // Fast simulation divider: CPSDVSR=2, so SSPCLKOUT is easy to observe.
        apb_write(ADDR_CPSR, 16'h0002);

        // Enable SSP in master mode.
        apb_write(ADDR_CR1, 16'h0002);

        apb_write(ADDR_DR, {8'h00, TX_BYTE});

        // Poll SSPSR.RNE until one received byte is available.
        while (poll_count < 2000) begin
            apb_read(ADDR_SR, read_data);
            if (read_data[2] == 1'b1) begin
                poll_count = 2000;
            end else begin
                poll_count = poll_count + 1;
            end
        end

        if (read_data[2] !== 1'b1) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: timeout waiting for SSPSR.RNE.", $time);
        end

        apb_read(ADDR_DR, read_data);
        if (read_data[7:0] !== TX_BYTE) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: SPI loopback read mismatch, got 0x%02h expected 0x%02h.",
                     $time, read_data[7:0], TX_BYTE);
        end else begin
            $display("[%0t] INFO: SPI loopback read matched 0x%02h.",
                     $time, read_data[7:0]);
        end

        if (sck_edge_count < 8) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: expected at least 8 SCK rising edges, got %0d.",
                     $time, sck_edge_count);
        end else begin
            $display("[%0t] INFO: observed %0d SCK rising edges.", $time, sck_edge_count);
        end

        if (mosi_shift !== TX_BYTE) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: MOSI bit stream mismatch, got 0x%02h expected 0x%02h.",
                     $time, mosi_shift, TX_BYTE);
        end else begin
            $display("[%0t] INFO: MOSI bit stream matched 0x%02h.", $time, mosi_shift);
        end

        if (error_count == 0) begin
            $display("TB PASS: PL022 APB-to-SPI path transmitted and received expected byte.");
        end else begin
            $display("TB FAIL: %0d error(s).", error_count);
        end

        $finish;
    end

endmodule
