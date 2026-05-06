`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Module: ima_adpcm_encoder
// Function:
//   IMA ADPCM encoder with built-in 4-bit-to-32-bit packing.
//
// Notes:
//   1. Each accepted 16-bit PCM sample produces one 4-bit IMA ADPCM code.
//   2. Eight 4-bit codes are packed into one 32-bit word.
//   3. Code order is little-nibble first:
//        code0 -> [3:0], code1 -> [7:4], ..., code7 -> [31:28].
//   4. The module has no output backpressure. When adpcm_word_valid pulses, the
//      consumer should capture adpcm_word in that clk cycle.
// -----------------------------------------------------------------------------
module ima_adpcm_encoder (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        enable,
    input  wire        clear,
    input  wire        pcm_valid,
    input  wire [15:0] pcm_sample,
    output reg         adpcm_word_valid,
    output reg  [31:0] adpcm_word
);

    reg signed [15:0] predictor;
    reg        [6:0]  step_index;
    reg        [2:0]  pack_count;
    reg        [31:0] pack_shift;
    reg               pcm_valid_d;
    reg signed [15:0] pcm_sample_d;

    reg signed [15:0] predictor_next_comb;
    reg        [6:0]  step_index_next_comb;
    reg        [31:0] packed_next_comb;

    function [15:0] ima_step;
        input [6:0] index;
        begin
            case (index)
                7'd0: ima_step = 16'd7;
                7'd1: ima_step = 16'd8;
                7'd2: ima_step = 16'd9;
                7'd3: ima_step = 16'd10;
                7'd4: ima_step = 16'd11;
                7'd5: ima_step = 16'd12;
                7'd6: ima_step = 16'd13;
                7'd7: ima_step = 16'd14;
                7'd8: ima_step = 16'd16;
                7'd9: ima_step = 16'd17;
                7'd10: ima_step = 16'd19;
                7'd11: ima_step = 16'd21;
                7'd12: ima_step = 16'd23;
                7'd13: ima_step = 16'd25;
                7'd14: ima_step = 16'd28;
                7'd15: ima_step = 16'd31;
                7'd16: ima_step = 16'd34;
                7'd17: ima_step = 16'd37;
                7'd18: ima_step = 16'd41;
                7'd19: ima_step = 16'd45;
                7'd20: ima_step = 16'd50;
                7'd21: ima_step = 16'd55;
                7'd22: ima_step = 16'd60;
                7'd23: ima_step = 16'd66;
                7'd24: ima_step = 16'd73;
                7'd25: ima_step = 16'd80;
                7'd26: ima_step = 16'd88;
                7'd27: ima_step = 16'd97;
                7'd28: ima_step = 16'd107;
                7'd29: ima_step = 16'd118;
                7'd30: ima_step = 16'd130;
                7'd31: ima_step = 16'd143;
                7'd32: ima_step = 16'd157;
                7'd33: ima_step = 16'd173;
                7'd34: ima_step = 16'd190;
                7'd35: ima_step = 16'd209;
                7'd36: ima_step = 16'd230;
                7'd37: ima_step = 16'd253;
                7'd38: ima_step = 16'd279;
                7'd39: ima_step = 16'd307;
                7'd40: ima_step = 16'd337;
                7'd41: ima_step = 16'd371;
                7'd42: ima_step = 16'd408;
                7'd43: ima_step = 16'd449;
                7'd44: ima_step = 16'd494;
                7'd45: ima_step = 16'd544;
                7'd46: ima_step = 16'd598;
                7'd47: ima_step = 16'd658;
                7'd48: ima_step = 16'd724;
                7'd49: ima_step = 16'd796;
                7'd50: ima_step = 16'd876;
                7'd51: ima_step = 16'd963;
                7'd52: ima_step = 16'd1060;
                7'd53: ima_step = 16'd1166;
                7'd54: ima_step = 16'd1282;
                7'd55: ima_step = 16'd1411;
                7'd56: ima_step = 16'd1552;
                7'd57: ima_step = 16'd1707;
                7'd58: ima_step = 16'd1878;
                7'd59: ima_step = 16'd2066;
                7'd60: ima_step = 16'd2272;
                7'd61: ima_step = 16'd2499;
                7'd62: ima_step = 16'd2749;
                7'd63: ima_step = 16'd3024;
                7'd64: ima_step = 16'd3327;
                7'd65: ima_step = 16'd3660;
                7'd66: ima_step = 16'd4026;
                7'd67: ima_step = 16'd4428;
                7'd68: ima_step = 16'd4871;
                7'd69: ima_step = 16'd5358;
                7'd70: ima_step = 16'd5894;
                7'd71: ima_step = 16'd6484;
                7'd72: ima_step = 16'd7132;
                7'd73: ima_step = 16'd7845;
                7'd74: ima_step = 16'd8630;
                7'd75: ima_step = 16'd9493;
                7'd76: ima_step = 16'd10442;
                7'd77: ima_step = 16'd11487;
                7'd78: ima_step = 16'd12635;
                7'd79: ima_step = 16'd13899;
                7'd80: ima_step = 16'd15289;
                7'd81: ima_step = 16'd16818;
                7'd82: ima_step = 16'd18500;
                7'd83: ima_step = 16'd20350;
                7'd84: ima_step = 16'd22385;
                7'd85: ima_step = 16'd24623;
                7'd86: ima_step = 16'd27086;
                7'd87: ima_step = 16'd29794;
                default: ima_step = 16'd32767;
            endcase
        end
    endfunction

    function signed [4:0] ima_index_delta;
        input [3:0] code;
        begin
            case (code[2:0])
                3'd0: ima_index_delta = -5'sd1;
                3'd1: ima_index_delta = -5'sd1;
                3'd2: ima_index_delta = -5'sd1;
                3'd3: ima_index_delta = -5'sd1;
                3'd4: ima_index_delta = 5'sd2;
                3'd5: ima_index_delta = 5'sd4;
                3'd6: ima_index_delta = 5'sd6;
                default: ima_index_delta = 5'sd8;
            endcase
        end
    endfunction

    always @(*) begin : p_encode_comb
        integer sample_i;
        integer predictor_i;
        integer diff_i;
        integer step_i;
        integer vpdiff_i;
        integer code_i;
        integer new_predictor_i;
        integer new_index_i;

        sample_i = pcm_sample_d;
        predictor_i = predictor;
        diff_i = sample_i - predictor_i;
        step_i = ima_step(step_index);
        code_i = 0;
        vpdiff_i = step_i >>> 3;

        if (diff_i < 0) begin
            code_i = 8;
            diff_i = -diff_i;
        end

        if (diff_i >= step_i) begin
            code_i = code_i | 4;
            diff_i = diff_i - step_i;
            vpdiff_i = vpdiff_i + step_i;
        end

        if (diff_i >= (step_i >>> 1)) begin
            code_i = code_i | 2;
            diff_i = diff_i - (step_i >>> 1);
            vpdiff_i = vpdiff_i + (step_i >>> 1);
        end

        if (diff_i >= (step_i >>> 2)) begin
            code_i = code_i | 1;
            vpdiff_i = vpdiff_i + (step_i >>> 2);
        end

        if (code_i[3]) begin
            new_predictor_i = predictor_i - vpdiff_i;
        end else begin
            new_predictor_i = predictor_i + vpdiff_i;
        end

        if (new_predictor_i > 32767) begin
            new_predictor_i = 32767;
        end else if (new_predictor_i < -32768) begin
            new_predictor_i = -32768;
        end

        // Convert step_index to an integer before adding a signed delta.
        // Otherwise unsigned arithmetic can turn 0 + (-1) into a large value.
        new_index_i = step_index;
        new_index_i = new_index_i + ima_index_delta(code_i[3:0]);
        if (new_index_i < 0) begin
            new_index_i = 0;
        end else if (new_index_i > 88) begin
            new_index_i = 88;
        end

        predictor_next_comb = new_predictor_i[15:0];
        step_index_next_comb = new_index_i[6:0];
        packed_next_comb = pack_shift | ({28'b0, code_i[3:0]} << (pack_count * 4));
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            predictor <= 16'sd0;
            step_index <= 7'd0;
            pack_count <= 3'd0;
            pack_shift <= 32'd0;
            pcm_valid_d <= 1'b0;
            pcm_sample_d <= 16'sd0;
            adpcm_word_valid <= 1'b0;
            adpcm_word <= 32'd0;
        end else if (clear) begin
            predictor <= 16'sd0;
            step_index <= 7'd0;
            pack_count <= 3'd0;
            pack_shift <= 32'd0;
            pcm_valid_d <= 1'b0;
            pcm_sample_d <= 16'sd0;
            adpcm_word_valid <= 1'b0;
            adpcm_word <= 32'd0;
        end else begin
            adpcm_word_valid <= 1'b0;
            pcm_valid_d <= enable && pcm_valid;
            if (enable && pcm_valid) begin
                pcm_sample_d <= $signed(pcm_sample);
            end

            if (enable && pcm_valid_d) begin
                predictor <= predictor_next_comb;
                step_index <= step_index_next_comb;

                if (pack_count == 3'd7) begin
                    adpcm_word <= packed_next_comb;
                    adpcm_word_valid <= 1'b1;
                    pack_count <= 3'd0;
                    pack_shift <= 32'd0;
                end else begin
                    pack_count <= pack_count + 1'b1;
                    pack_shift <= packed_next_comb;
                end
            end
        end
    end

endmodule
