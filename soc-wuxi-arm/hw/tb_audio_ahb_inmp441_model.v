`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Testbench: tb_audio_ahb_inmp441_model
// Function:
//   Connect the behavioral INMP441 model to the current I2S receiver, FIFO, and
//   AHB audio register block. This checks whether the microphone model's I2S
//   timing can be decoded by i2s_rx_inmp441 and read back through DATA.
// -----------------------------------------------------------------------------
module tb_audio_ahb_inmp441_model;

    localparam integer NUM_SAMPLES  = 4;
    localparam [31:0]  AUDIO_BASE   = 32'h2001_0000;
    localparam [31:0]  ADDR_CONTROL = AUDIO_BASE + 32'h0;
    localparam [31:0]  ADDR_STATUS  = AUDIO_BASE + 32'h20;
    localparam [31:0]  ADDR_DATA    = AUDIO_BASE + 32'h40;

    reg         clk_50m;
    reg         rst_n;

    reg         ahb_hsel;
    reg  [31:0] ahb_haddr;
    reg  [1:0]  ahb_htrans;
    reg         ahb_hwrite;
    reg  [2:0]  ahb_hsize;
    reg  [31:0] ahb_hwdata;

    wire        mic_sd;
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
    integer    i;
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

    inmp441_model #(
        .MAX_SAMPLES (NUM_SAMPLES),
        .HEX_FILE    ("sample_hex_small.txt")
    ) u_inmp441_model (
        .sck (mic_sck),
        .ws  (mic_ws),
        .sd  (mic_sd)
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

    always #10 clk_50m = ~clk_50m;

    always @(posedge clk_50m) begin
        if (sample_valid) begin
            $display("[%0t] I2S sample_valid: sample_data=0x%04h", $time, sample_data);
        end
    end

    initial begin
        clk_50m             = 1'b0;
        rst_n               = 1'b0;
        expected_samples[0] = 16'h1234;
        expected_samples[1] = 16'hA5C3;
        expected_samples[2] = 16'h7E81;
        expected_samples[3] = 16'h4001;
        error_count         = 0;
        poll_count          = 0;
        status_reg          = 32'h0;
        data_reg            = 32'h0;
        ahb_idle;

        #200;
        rst_n = 1'b1;

        ahb_write(ADDR_CONTROL, 32'h0000_0001);

        while ((poll_count < 50000) && (status_reg[14:4] < NUM_SAMPLES)) begin
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

        ahb_write(ADDR_CONTROL, 32'h0000_0000);

        for (i = 0; i < NUM_SAMPLES; i = i + 1) begin
            ahb_read(ADDR_DATA, data_reg);
            if (data_reg[15:0] !== expected_samples[i]) begin
                error_count = error_count + 1;
                $display("[%0t] ERROR: sample %0d mismatch, got 0x%04h expected 0x%04h.",
                         $time, i, data_reg[15:0], expected_samples[i]);
            end else begin
                $display("[%0t] INFO: sample %0d matched 0x%04h.",
                         $time, i, data_reg[15:0]);
            end
        end

        if (error_count == 0) begin
            $display("TB PASS: INMP441 model data was received and read back correctly.");
        end else begin
            $display("TB FAIL: detected %0d error(s).", error_count);
        end

        $finish;
    end

    initial begin
        #10000000;
        $display("TB FAIL: timeout.");
        $finish;
    end

endmodule
