`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Testbench: tb_audio_ahb_adpcm
// Function:
//   Verifies the compressed audio path:
//     I2S -> PCM FIFO -> IMA ADPCM encoder -> ADPCM FIFO -> audio_ahb_if.
//   Eight zero PCM samples should produce one packed ADPCM word of 0x00000000.
// -----------------------------------------------------------------------------
module tb_audio_ahb_adpcm;

    localparam integer NUM_SAMPLES  = 8;
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

    wire        pcm_fifo_full;
    wire        pcm_fifo_empty;
    wire [15:0] pcm_fifo_rd_data;
    wire [10:0] pcm_fifo_usedw;
    wire        pcm_fifo_ahb_rd_en;
    wire        pcm_fifo_adpcm_rd_en;
    wire        pcm_fifo_rd_en;

    wire        adpcm_word_valid;
    wire [31:0] adpcm_word_data;
    wire        adpcm_fifo_full;
    wire        adpcm_fifo_empty;
    wire [31:0] adpcm_fifo_rd_data;
    wire [10:0] adpcm_fifo_usedw;
    wire        adpcm_fifo_rd_en;

    wire        audio_rx_enable;
    wire        audio_i2s_clear;
    wire        audio_fifo_clear;
    wire        audio_compress_enable;
    wire        audio_hreadyout;
    wire [31:0] audio_hrdata;

    integer tx_frame_idx;
    integer tx_bit_idx;
    integer error_count;
    integer poll_count;
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

    assign pcm_fifo_adpcm_rd_en = audio_compress_enable & !pcm_fifo_empty & !adpcm_fifo_full;
    assign pcm_fifo_rd_en = audio_compress_enable ? pcm_fifo_adpcm_rd_en : pcm_fifo_ahb_rd_en;

    fifo_sync #(
        .DATA_WIDTH (16),
        .ADDR_WIDTH (10)
    ) u_pcm_fifo (
        .clk     (clk_50m),
        .rst_n   (rst_n),
        .clr     (audio_fifo_clear | audio_i2s_clear),
        .wr_en   (fifo_wr_en),
        .wr_data (fifo_wr_data),
        .rd_en   (pcm_fifo_rd_en),
        .rd_data (pcm_fifo_rd_data),
        .full    (pcm_fifo_full),
        .empty   (pcm_fifo_empty),
        .usedw   (pcm_fifo_usedw)
    );

    ima_adpcm_encoder u_ima_adpcm_encoder (
        .clk              (clk_50m),
        .rst_n            (rst_n),
        .enable           (audio_compress_enable),
        .clear            (audio_fifo_clear | audio_i2s_clear),
        .pcm_valid        (pcm_fifo_adpcm_rd_en),
        .pcm_sample       (pcm_fifo_rd_data),
        .adpcm_word_valid (adpcm_word_valid),
        .adpcm_word       (adpcm_word_data)
    );

    fifo_sync #(
        .DATA_WIDTH (32),
        .ADDR_WIDTH (10)
    ) u_adpcm_fifo (
        .clk     (clk_50m),
        .rst_n   (rst_n),
        .clr     (audio_fifo_clear | audio_i2s_clear),
        .wr_en   (adpcm_word_valid),
        .wr_data (adpcm_word_data),
        .rd_en   (adpcm_fifo_rd_en),
        .rd_data (adpcm_fifo_rd_data),
        .full    (adpcm_fifo_full),
        .empty   (adpcm_fifo_empty),
        .usedw   (adpcm_fifo_usedw)
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
        .pcm_fifo_empty (pcm_fifo_empty),
        .pcm_fifo_full  (pcm_fifo_full),
        .pcm_fifo_usedw (pcm_fifo_usedw),
        .pcm_fifo_rd_data(pcm_fifo_rd_data),
        .pcm_fifo_rd_en (pcm_fifo_ahb_rd_en),
        .adpcm_fifo_empty (adpcm_fifo_empty),
        .adpcm_fifo_full  (adpcm_fifo_full),
        .adpcm_fifo_usedw (adpcm_fifo_usedw),
        .adpcm_fifo_rd_data(adpcm_fifo_rd_data),
        .adpcm_fifo_rd_en (adpcm_fifo_rd_en)
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

    initial begin
        mic_sd       = 1'b0;
        tx_frame_idx = 0;
        tx_bit_idx   = 1;

        wait (rst_n == 1'b1);

        forever begin
            @(negedge mic_sck);

            // Drive eight zero PCM samples. All remaining frames stay zero too.
            mic_sd <= 1'b0;

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
        clk_50m     = 1'b0;
        rst_n       = 1'b0;
        error_count = 0;
        poll_count  = 0;
        status_reg  = 32'h0;
        data_reg    = 32'h0;
        ahb_idle;

        #200;
        rst_n = 1'b1;

        // rx_enable + compress_enable
        ahb_write(ADDR_CONTROL, 32'h0000_0009);

        while ((poll_count < 50000) && (status_reg[14:4] < 1)) begin
            ahb_read(ADDR_STATUS, status_reg);
            poll_count = poll_count + 1;
        end

        if (status_reg[15] !== 1'b1) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: compress_enable status bit should be 1.", $time);
        end

        if (status_reg[14:4] < 1) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: timeout waiting for ADPCM FIFO fill, usedw=%0d.",
                     $time, status_reg[14:4]);
        end else begin
            $display("[%0t] INFO: ADPCM FIFO filled to %0d word(s).",
                     $time, status_reg[14:4]);
        end

        // Stop I2S capture but keep compressed read mode selected.
        ahb_write(ADDR_CONTROL, 32'h0000_0008);

        ahb_read(ADDR_DATA, data_reg);
        if (data_reg !== 32'h0000_0000) begin
            error_count = error_count + 1;
            $display("[%0t] ERROR: ADPCM word mismatch, got 0x%08h expected 0x00000000.",
                     $time, data_reg);
        end else begin
            $display("[%0t] INFO: ADPCM word matched 0x%08h.", $time, data_reg);
        end

        if (error_count == 0) begin
            $display("TB PASS: compressed audio AHB path read back expected ADPCM word.");
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
