`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Testbench: tb_audio_ahb
// Function:
//   1. Generate 50 MHz system clock and reset.
//   2. Instantiate the I2S receiver, FIFO, and AHB audio slave together.
//   3. Emulate a simple I2S microphone on mic_sd.
//   4. Use AHB-Lite register reads/writes to enable capture, poll FIFO status,
//      and read back two known PCM samples.
// -----------------------------------------------------------------------------
module tb_audio_ahb;

    localparam integer NUM_SAMPLES  = 2;
    localparam [31:0]  AUDIO_BASE   = 32'h2001_0000;
    localparam [31:0]  ADDR_CONTROL = AUDIO_BASE + 32'h0;
    localparam [31:0]  ADDR_STATUS  = AUDIO_BASE + 32'h20;
    localparam [31:0]  ADDR_DATA    = AUDIO_BASE + 32'h40;

    reg         clk_50m;
    reg         rst_n;
    reg         mic_sd;

    reg         ahb_hsel;
    reg  [31:0] ahb_haddr;
    reg  [1:0]  ahb_htrans;
    reg         ahb_hwrite;
    reg  [2:0]  ahb_hsize;
    reg  [31:0] ahb_hwdata;

    wire        mic_sck;
    wire        mic_ws;
    wire        sample_valid;
    wire [15:0] sample_data;
    wire        fifo_wr_en;
    wire [15:0] fifo_wr_data;

    wire        i2s_fifo_full;
    wire        i2s_fifo_empty;
    wire [15:0] i2s_fifo_rd_data;
    wire [10:0] i2s_fifo_usedw;
    wire        i2s_fifo_rd_en;

    wire        audio_rx_enable;
    wire        audio_i2s_clear;
    wire        audio_fifo_clear;
    wire        audio_compress_enable;
    wire        audio_hreadyout;
    wire [31:0] audio_hrdata;
    wire        unused_adpcm_fifo_rd_en;

    reg [15:0] expected_samples [0:NUM_SAMPLES-1];
    integer    tx_frame_idx;
    integer    tx_bit_idx;
    integer    error_count;
    integer    poll_count;
    reg [31:0] status_reg;
    reg [31:0] data_reg;

    i2s_rx_inmp441 u_i2s_rx_inmp441 (
        .clk_50m      (clk_50m),
        .rst_n        (rst_n),
        .rx_enable    (audio_rx_enable),
        .clear        (audio_i2s_clear),
        .mic_sd       (mic_sd),
        .mic_sck      (mic_sck),
        .mic_ws       (mic_ws),
        .sample_valid (sample_valid),
        .sample_data  (sample_data),
        .fifo_wr_en   (fifo_wr_en),
        .fifo_wr_data (fifo_wr_data)
    );

    fifo_sync #(
        .DATA_WIDTH (16),
        .ADDR_WIDTH (10)
    ) u_i2s_fifo (
        .clk     (clk_50m),
        .rst_n   (rst_n),
        .clr     (audio_fifo_clear | audio_i2s_clear),
        .wr_en   (fifo_wr_en),
        .wr_data (fifo_wr_data),
        .rd_en   (i2s_fifo_rd_en),
        .rd_data (i2s_fifo_rd_data),
        .full    (i2s_fifo_full),
        .empty   (i2s_fifo_empty),
        .usedw   (i2s_fifo_usedw)
    );

    audio_ahb_if u_audio_ahb_if (
        .HSEL       (ahb_hsel),
        .HCLK       (clk_50m),
        .HRESETn    (rst_n),
        .HREADY     (1'b1),
        .HADDR      (ahb_haddr),
        .HTRANS     (ahb_htrans),
        .HWRITE     (ahb_hwrite),
        .HSIZE      (ahb_hsize),
        .HWDATA     (ahb_hwdata),
        .HREADYOUT  (audio_hreadyout),
        .HRDATA     (audio_hrdata),
        .rx_enable  (audio_rx_enable),
        .i2s_clear  (audio_i2s_clear),
        .fifo_clear (audio_fifo_clear),
        .compress_enable(audio_compress_enable),
        .sample_valid(sample_valid),
        .pcm_fifo_empty (i2s_fifo_empty),
        .pcm_fifo_full  (i2s_fifo_full),
        .pcm_fifo_usedw (i2s_fifo_usedw),
        .pcm_fifo_rd_data(i2s_fifo_rd_data),
        .pcm_fifo_rd_en (i2s_fifo_rd_en),
        .adpcm_fifo_empty (1'b1),
        .adpcm_fifo_full  (1'b0),
        .adpcm_fifo_usedw (11'd0),
        .adpcm_fifo_rd_data(32'd0),
        .adpcm_fifo_rd_en (unused_adpcm_fifo_rd_en)
    );

    function drive_bit;
        input [15:0] sample;
        input integer bit_idx;
        begin
            if ((bit_idx >= 1) && (bit_idx <= 16)) begin
                drive_bit = sample[16 - bit_idx];
            end else begin
                drive_bit = 1'b0;
            end
        end
    endfunction

    task ahb_idle;
        begin
            ahb_hsel   = 1'b0;
            ahb_haddr  = 32'h0;
            ahb_htrans = 2'b00;
            ahb_hwrite = 1'b0;
            ahb_hsize  = 3'b010;
            ahb_hwdata = 32'h0;
        end
    endtask

    task ahb_write;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(negedge clk_50m);
            ahb_hsel   = 1'b1;
            ahb_haddr  = addr;
            ahb_htrans = 2'b10;
            ahb_hwrite = 1'b1;
            ahb_hsize  = 3'b010;
            ahb_hwdata = data;
            @(posedge clk_50m);
            @(negedge clk_50m);
            ahb_hsel   = 1'b0;
            ahb_haddr  = 32'h0;
            ahb_htrans = 2'b00;
            ahb_hwrite = 1'b0;
            ahb_hsize  = 3'b010;
            @(posedge clk_50m);
            @(negedge clk_50m);
            ahb_hwdata = 32'h0;
        end
    endtask

    task ahb_read;
        input  [31:0] addr;
        output [31:0] data;
        begin
            @(negedge clk_50m);
            ahb_hsel   = 1'b1;
            ahb_haddr  = addr;
            ahb_htrans = 2'b10;
            ahb_hwrite = 1'b0;
            ahb_hsize  = 3'b010;
            ahb_hwdata = 32'h0;
            @(posedge clk_50m);
            #1;
            data = audio_hrdata;
            @(negedge clk_50m);
            ahb_idle;
        end
    endtask

    always #10 clk_50m = ~clk_50m; // 50 MHz

    // Simple microphone model:
    // update mic_sd on mic_sck falling edges so the next rising edge sees
    // stable data.
    initial begin
        mic_sd       = 1'b0;
        tx_frame_idx = 0;
        tx_bit_idx   = 1;

        wait (rst_n == 1'b1);

        forever begin
            @(negedge mic_sck);

            if (tx_frame_idx < NUM_SAMPLES) begin
                mic_sd <= drive_bit(expected_samples[tx_frame_idx], tx_bit_idx);
            end else begin
                mic_sd <= 1'b0;
            end

            if (tx_bit_idx == 63) begin
                tx_bit_idx = 0;

                if (tx_frame_idx < NUM_SAMPLES) begin
                    tx_frame_idx = tx_frame_idx + 1;
                end
            end else begin
                tx_bit_idx = tx_bit_idx + 1;
            end
        end
    end

    initial begin
        clk_50m             = 1'b0;
        rst_n               = 1'b0;
        expected_samples[0] = 16'h1234;
        expected_samples[1] = 16'hA5C3;
        error_count         = 0;
        poll_count          = 0;
        status_reg          = 32'h0;
        data_reg            = 32'h0;
        ahb_idle;

        #200;
        rst_n = 1'b1;

        // STATUS after reset: FIFO should be empty.
        ahb_read(ADDR_STATUS, status_reg);
        if (status_reg[0] !== 1'b1) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: fifo_empty should be 1 after reset.", $time);
        end

        // Enable capture.
        ahb_write(ADDR_CONTROL, 32'h0000_0001);

        // Poll until at least two samples are available in the FIFO.
        while ((poll_count < 20000) && (status_reg[14:4] < NUM_SAMPLES)) begin
            ahb_read(ADDR_STATUS, status_reg);
            poll_count = poll_count + 1;
        end

        if (status_reg[14:4] < NUM_SAMPLES) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: timeout waiting for FIFO fill, usedw=%0d.",
                     $time, status_reg[14:4]);
        end else begin
            $display("[%0t] INFO: FIFO filled to %0d samples.",
                     $time, status_reg[14:4]);
        end

        // Stop further capture before draining the FIFO.
        ahb_write(ADDR_CONTROL, 32'h0000_0000);

        ahb_read(ADDR_DATA, data_reg);
        if (data_reg[15:0] !== expected_samples[0]) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: first PCM mismatch, got 0x%04h expected 0x%04h.",
                     $time, data_reg[15:0], expected_samples[0]);
        end else begin
            $display("[%0t] INFO: first PCM matched 0x%04h.",
                     $time, data_reg[15:0]);
        end

        ahb_read(ADDR_DATA, data_reg);
        if (data_reg[15:0] !== expected_samples[1]) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: second PCM mismatch, got 0x%04h expected 0x%04h.",
                     $time, data_reg[15:0], expected_samples[1]);
        end else begin
            $display("[%0t] INFO: second PCM matched 0x%04h.",
                     $time, data_reg[15:0]);
        end

        ahb_read(ADDR_STATUS, status_reg);
        if (status_reg[14:4] !== 0) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: FIFO should be empty after two reads, usedw=%0d.",
                     $time, status_reg[14:4]);
        end

        if (error_count == 0) begin
            $display("TB PASS: audio AHB path read back expected PCM samples.");
        end else begin
            $display("TB FAIL: detected %0d error(s).", error_count);
        end

        $finish;
    end

    initial begin
        #5000000;
        $display("TB FAIL: timeout.");
        $finish;
    end

endmodule
