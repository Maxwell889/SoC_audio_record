`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Module: inmp441_model
// Function:
//   Behavioral simulation model of an INMP441-like I2S microphone.
//
// Timing model:
//   - FPGA drives SCK and WS.
//   - WS = 0 means left slot, WS = 1 means right slot.
//   - The model only outputs data in the left slot.
//   - SD changes on SCK falling edges; the receiver should sample on rising edges.
//   - One I2S delay bit is inserted before the sample MSB.
//   - The sample payload here is 16-bit PCM, MSB first.
// -----------------------------------------------------------------------------
module inmp441_model #(
    parameter integer MAX_SAMPLES = 282624,
    parameter [1024*8-1:0] HEX_FILE = "sample_hex.txt"
) (
    input  wire sck,
    input  wire ws,
    output reg  sd
);

    reg [15:0] sample_data [0:MAX_SAMPLES-1];
    integer    num_samples;

    reg [5:0]  bit_pos;
    reg        ws_prev;
    reg [18:0] sample_idx;
    reg [15:0] current_sample;

    wire ws_fall_detected = (ws_prev == 1'b1 && ws == 1'b0);
    wire ws_rise_detected = (ws_prev == 1'b0 && ws == 1'b1);

    initial begin : load_samples
        integer i;
        integer fd;
        integer rc;

        for (i = 0; i < MAX_SAMPLES; i = i + 1) begin
            sample_data[i] = 16'h0000;
        end

        fd = $fopen(HEX_FILE, "r");
        if (fd == 0) begin
            $display("[inmp441_model] WARNING: could not open '%0s', using zero samples.", HEX_FILE);
            num_samples = 0;
        end else begin
            i = 0;
            while (i < MAX_SAMPLES && !$feof(fd)) begin
                rc = $fscanf(fd, "%x\n", sample_data[i]);
                if (rc == 1) begin
                    i = i + 1;
                end
            end
            num_samples = i;
            $fclose(fd);
            $display("[inmp441_model] loaded %0d PCM samples from '%0s'.", num_samples, HEX_FILE);
        end

        bit_pos        = 6'd0;
        ws_prev        = 1'b0;
        sample_idx     = 19'd0;
        current_sample = sample_data[0];
        sd             = 1'b0;
    end

    always @(negedge sck) begin
        ws_prev <= ws;

        if (ws_fall_detected) begin
            // Start of left slot. Output the mandatory I2S delay bit now.
            bit_pos        <= 6'd2;
            current_sample <= sample_data[sample_idx];
            sd             <= sample_data[sample_idx][15];
        end else if (ws_rise_detected) begin
            // Start of right slot. INMP441 left-channel data is not driven here.
            bit_pos <= 6'd32;
            sd      <= 1'b0;

            if (num_samples == 0) begin
                sample_idx <= 19'd0;
            end else if (sample_idx == num_samples - 1) begin
                sample_idx <= 19'd0;
            end else begin
                sample_idx <= sample_idx + 1'b1;
            end
        end else begin
            if (ws == 1'b0 && bit_pos >= 6'd1 && bit_pos <= 6'd16) begin
                sd <= current_sample[16 - bit_pos];
            end else begin
                sd <= 1'b0;
            end

            if (bit_pos == 6'd63) begin
                bit_pos <= 6'd0;
            end else begin
                bit_pos <= bit_pos + 1'b1;
            end
        end
    end

endmodule
