`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Testbench: tb_ima_adpcm_encoder
// Function:
//   Basic self-check for the IMA ADPCM encoder and internal 4-bit-to-32-bit
//   packer. Eight zero PCM samples should produce eight zero ADPCM codes, packed
//   as one 32-bit zero word.
// -----------------------------------------------------------------------------
module tb_ima_adpcm_encoder;

    reg         clk;
    reg         rst_n;
    reg         enable;
    reg         clear;
    reg         pcm_valid;
    reg  [15:0] pcm_sample;
    wire        adpcm_word_valid;
    wire [31:0] adpcm_word;

    integer i;
    integer valid_count;
    integer error_count;

    ima_adpcm_encoder dut (
        .clk              (clk),
        .rst_n            (rst_n),
        .enable           (enable),
        .clear            (clear),
        .pcm_valid        (pcm_valid),
        .pcm_sample       (pcm_sample),
        .adpcm_word_valid (adpcm_word_valid),
        .adpcm_word       (adpcm_word)
    );

    always #10 clk = ~clk;

    always @(posedge clk) begin
        if (!rst_n) begin
            valid_count <= 0;
        end else if (adpcm_word_valid) begin
            valid_count <= valid_count + 1;

            if (adpcm_word !== 32'h0000_0000) begin
                error_count <= error_count + 1;
                $display("[%0t] ERROR: expected packed word 0x00000000, got 0x%08h.",
                         $time, adpcm_word);
            end else begin
                $display("[%0t] INFO: packed ADPCM word matched 0x%08h.",
                         $time, adpcm_word);
            end
        end
    end

    initial begin
        clk         = 1'b0;
        rst_n       = 1'b0;
        enable      = 1'b0;
        clear       = 1'b0;
        pcm_valid   = 1'b0;
        pcm_sample  = 16'h0000;
        valid_count = 0;
        error_count = 0;

        repeat (5) @(posedge clk);
        rst_n  = 1'b1;
        enable = 1'b1;

        for (i = 0; i < 8; i = i + 1) begin
            @(negedge clk);
            pcm_sample = 16'h0000;
            pcm_valid  = 1'b1;
            @(negedge clk);
            pcm_valid  = 1'b0;
        end

        repeat (10) @(posedge clk);

        if (valid_count != 1) begin
            error_count = error_count + 1;
            $display("ERROR: expected exactly one packed word, got %0d.", valid_count);
        end

        if (error_count == 0) begin
            $display("TB PASS: IMA ADPCM encoder basic packing test passed.");
        end else begin
            $display("TB FAIL: detected %0d error(s).", error_count);
        end

        $finish;
    end

    initial begin
        #500000;
        $display("TB FAIL: timeout.");
        $finish;
    end

endmodule
