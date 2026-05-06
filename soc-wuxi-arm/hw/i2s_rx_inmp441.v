`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Module: i2s_rx_inmp441
// Function:
//   I2S master-side receiver for INMP441. The module generates mic_sck/mic_ws,
//   receives one 16-bit left-channel sample per frame, and exposes a simple
//   FIFO write interface.
//
// Notes:
//   1. Input clock is 50 MHz, internal mic_sck target is about 1 MHz.
//   2. mic_ws is 0 during the left slot and 1 during the right slot.
//   3. Standard I2S 1-bit delay is respected. The MSB arrives one bit clock
//      after the slot boundary.
//   4. sample_valid is asserted for one clk_50m cycle when one 16-bit sample
//      has been fully received.
// -----------------------------------------------------------------------------
module i2s_rx_inmp441 (
    input  wire        clk_50m,
    input  wire        rst_n,
    input  wire        rx_enable,
    input  wire        clear,
    input  wire        mic_sd,
    output wire        mic_sck,
    output wire        mic_ws,
    output reg         sample_valid,
    output reg [15:0]  sample_data,
    output wire        fifo_wr_en,
    output wire [15:0] fifo_wr_data
);

    // 50 MHz / (25 * 2) = 1 MHz.
    // No extra clock domain is created. The divider only creates toggle enables,
    // while all sequential logic still runs in the clk_50m domain.
    localparam integer SCK_HALF_DIV      = 25;
    localparam integer SCK_DIV_CNT_WIDTH = 5;

    // One I2S frame contains 64 SCK periods:
    //   bit  0..31 : left slot,  WS = 0
    //   bit 32..63 : right slot, WS = 1
    //
    // In standard I2S, the MSB appears one bit clock after the slot starts.
    // Therefore left-channel valid 16-bit data is captured at bit 1..16.
    localparam [5:0] FRAME_LAST_BIT      = 6'd63;
    localparam [5:0] LEFT_SLOT_LAST_BIT  = 6'd31;
    localparam [5:0] LEFT_DATA_START_BIT = 6'd1;
    localparam [5:0] LEFT_DATA_END_BIT   = 6'd16;

    reg [SCK_DIV_CNT_WIDTH-1:0] sck_div_cnt;
    reg                         mic_sck_reg;
    reg                         mic_ws_reg;
    reg                         sck_rise_ce;
    reg                         sck_fall_ce;
    reg [5:0]                   frame_bit_cnt;
    reg [15:0]                  shift_reg;

    assign mic_sck     = mic_sck_reg;
    assign mic_ws      = mic_ws_reg;
    assign fifo_wr_en  = sample_valid;
    assign fifo_wr_data = sample_data;

    // -------------------------------------------------------------------------
    // 1) SCK generation
    // -------------------------------------------------------------------------
    // Toggle mic_sck every 25 clk_50m cycles, which gives about 1 MHz.
    //
    // sck_rise_ce / sck_fall_ce are one-cycle clock-enable pulses:
    //   - sck_rise_ce means "this clk_50m edge corresponds to an SCK rise"
    //   - sck_fall_ce means "this clk_50m edge corresponds to an SCK fall"
    // Frame counting and sample capture use these enables.
    always @(posedge clk_50m or negedge rst_n) begin
        if (!rst_n) begin
            sck_div_cnt <= {SCK_DIV_CNT_WIDTH{1'b0}};
            mic_sck_reg <= 1'b0;
            sck_rise_ce <= 1'b0;
            sck_fall_ce <= 1'b0;
        end else begin
            if (clear || !rx_enable) begin
                sck_div_cnt <= {SCK_DIV_CNT_WIDTH{1'b0}};
                mic_sck_reg <= 1'b0;
                sck_rise_ce <= 1'b0;
                sck_fall_ce <= 1'b0;
            end else begin
                sck_rise_ce <= 1'b0;
                sck_fall_ce <= 1'b0;

                if (sck_div_cnt == SCK_HALF_DIV - 1) begin
                    sck_div_cnt <= {SCK_DIV_CNT_WIDTH{1'b0}};
                    mic_sck_reg <= ~mic_sck_reg;

                    if (mic_sck_reg == 1'b0) begin
                        sck_rise_ce <= 1'b1;
                    end else begin
                        sck_fall_ce <= 1'b1;
                    end
                end else begin
                    sck_div_cnt <= sck_div_cnt + 1'b1;
                end
            end
        end
    end

    // -------------------------------------------------------------------------
    // 2) Frame and slot tracking
    // -------------------------------------------------------------------------
    // Advance frame_bit_cnt on every SCK rising edge.
    // Update mic_ws on the following SCK falling edge so the channel indicator
    // is already stable before the next sampling edge.
    always @(posedge clk_50m or negedge rst_n) begin
        if (!rst_n) begin
            frame_bit_cnt <= 6'd0;
            mic_ws_reg    <= 1'b0;
        end else if (clear || !rx_enable) begin
            frame_bit_cnt <= 6'd0;
            mic_ws_reg    <= 1'b0;
        end else begin
            if (sck_rise_ce) begin
                if (frame_bit_cnt == FRAME_LAST_BIT) begin
                    frame_bit_cnt <= 6'd0;
                end else begin
                    frame_bit_cnt <= frame_bit_cnt + 1'b1;
                end
            end

            if (sck_fall_ce) begin
                mic_ws_reg <= (frame_bit_cnt > LEFT_SLOT_LAST_BIT);
            end
        end
    end

    // -------------------------------------------------------------------------
    // 3) Serial-to-parallel conversion and sample output
    // -------------------------------------------------------------------------
    // Only the left slot is received, so shifting only happens at bit 1..16.
    //
    // Sampling is done on SCK rising edges. In the testbench, mic_sd changes on
    // SCK falling edges, so the next rising edge sees stable data.
    //
    // frame_bit_cnt = 0 is the mandatory I2S 1-bit delay and carries no valid
    // audio data. At frame_bit_cnt = 16, one full 16-bit sample has been
    // collected, so sample_data is updated and sample_valid pulses for one cycle.
    always @(posedge clk_50m or negedge rst_n) begin
        if (!rst_n) begin
            shift_reg     <= 16'd0;
            sample_valid  <= 1'b0;
            sample_data   <= 16'd0;
        end else if (clear || !rx_enable) begin
            shift_reg     <= 16'd0;
            sample_valid  <= 1'b0;
            sample_data   <= 16'd0;
        end else begin
            sample_valid <= 1'b0;

            if (sck_rise_ce) begin
                // Bit 0 of the left slot is the mandatory I2S delay bit.
                // Clear the shift register here before receiving a new sample.
                if (frame_bit_cnt == 6'd0) begin
                    shift_reg <= 16'd0;
                end

                if ((frame_bit_cnt >= LEFT_DATA_START_BIT) &&
                    (frame_bit_cnt <= LEFT_DATA_END_BIT)) begin
                    shift_reg <= {shift_reg[14:0], mic_sd};

                    if (frame_bit_cnt == LEFT_DATA_END_BIT) begin
                        sample_data  <= {shift_reg[14:0], mic_sd};
                        sample_valid <= 1'b1;
                    end
                end
            end
        end
    end

endmodule
