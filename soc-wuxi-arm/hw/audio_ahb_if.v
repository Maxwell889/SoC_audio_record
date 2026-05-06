`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Module: audio_ahb_if
// Function:
//   Minimal AHB-Lite slave for the coursework audio path.
//
// Address map within the MI2 region (base 0x2001_0000 in the current top):
//   0x00 CONTROL
//        [0] rx_enable     - 1 enables the I2S receiver
//        [1] i2s_clear     - write 1 to generate a one-cycle clear pulse
//        [2] fifo_clear    - write 1 to generate a one-cycle clear pulse
//        [3] compress_enable - 1 selects the ADPCM data path for STATUS/DATA
//   0x20 STATUS
//        [0] fifo_empty
//        [1] fifo_full
//        [2] sample_valid  - pulse mirrored into the status readback
//        [3] rx_enable
//        [14:4] fifo_usedw
//        [15] compress_enable
//   0x40 DATA
//        compress_enable=0: [15:0] PCM sample, read pops one PCM FIFO word.
//        compress_enable=1: [31:0] packed ADPCM word, read pops one ADPCM FIFO word.
//
// Notes:
//   1. This slave is always ready and does not insert wait states.
//   2. FIFO data is read using a "peek then pop" scheme. rd_en is pulsed on
//      a valid DATA register read while rd_data reflects the current FIFO front.
// -----------------------------------------------------------------------------
module audio_ahb_if (
    input  wire        HSEL,
    input  wire        HCLK,
    input  wire        HRESETn,
    input  wire        HREADY,
    input  wire [31:0] HADDR,
    input  wire [1:0]  HTRANS,
    input  wire        HWRITE,
    input  wire [2:0]  HSIZE,
    input  wire [31:0] HWDATA,
    output wire        HREADYOUT,
    output reg  [31:0] HRDATA,

    output reg         rx_enable,
    output reg         i2s_clear,
    output reg         fifo_clear,
    output reg         compress_enable,
    input  wire        sample_valid,
    input  wire        pcm_fifo_empty,
    input  wire        pcm_fifo_full,
    input  wire [10:0] pcm_fifo_usedw,
    input  wire [15:0] pcm_fifo_rd_data,
    output reg         pcm_fifo_rd_en,
    input  wire        adpcm_fifo_empty,
    input  wire        adpcm_fifo_full,
    input  wire [10:0] adpcm_fifo_usedw,
    input  wire [31:0] adpcm_fifo_rd_data,
    output reg         adpcm_fifo_rd_en
);

    // Register address map (2026-05-04 remap):
    // Early board reads made HADDR[4:2] look stuck-at-0, so this mapping
    // distinguishes registers using HADDR[7:5] (word_addr[5:3]).
    //
    // Old mapping (broken by stuck bits):
    //   CONTROL = 6'h00 (byte +0x00), STATUS = 6'h01 (+0x04), DATA = 6'h02 (+0x08)
    // New mapping (uses HADDR[7:5]):
    //   CONTROL = 6'h00 (byte +0x00), STATUS = 6'h08 (+0x20), DATA = 6'h10 (+0x40)
    localparam [5:0] ADDR_CONTROL = 6'h00;
    localparam [5:0] ADDR_STATUS  = 6'h08;
    localparam [5:0] ADDR_DATA    = 6'h10;

    wire       trans_valid;
    wire       write_valid;
    wire       read_valid;
    wire [5:0] word_addr;
    wire       data_read_hit;
    wire       selected_fifo_empty_raw;
    wire       selected_fifo_empty;
    wire       selected_fifo_full;
    wire [10:0] selected_fifo_usedw;

    // AHB-Lite write pipeline: HWDATA is valid during the DATA phase,
    // which is one HCLK cycle after the ADDRESS phase where HTRANS[1]=1.
    // We register the address-phase control signals and apply the write
    // in the data phase to sample HWDATA at the correct time.
    reg         write_pending;
    reg [5:0]   pending_word_addr;
    reg         pending_hsize_32;

    assign HREADYOUT  = 1'b1;
    assign trans_valid = HSEL && HREADY && HTRANS[1];
    assign write_valid = trans_valid && HWRITE;
    assign read_valid  = trans_valid && !HWRITE;
    assign word_addr   = HADDR[7:2];
    assign selected_fifo_empty_raw = compress_enable ? adpcm_fifo_empty : pcm_fifo_empty;
    assign selected_fifo_full  = compress_enable ? adpcm_fifo_full  : pcm_fifo_full;
    assign selected_fifo_usedw = compress_enable ? adpcm_fifo_usedw : pcm_fifo_usedw;
    assign selected_fifo_empty = selected_fifo_empty_raw || (selected_fifo_usedw == 11'd0);
    assign data_read_hit = read_valid && (word_addr == ADDR_DATA) && !selected_fifo_empty;

    always @(posedge HCLK or negedge HRESETn) begin
        if (!HRESETn) begin
            rx_enable <= 1'b0;
            i2s_clear <= 1'b0;
            fifo_clear <= 1'b0;
            compress_enable <= 1'b0;
            pcm_fifo_rd_en <= 1'b0;
            adpcm_fifo_rd_en <= 1'b0;
            write_pending <= 1'b0;
            pending_word_addr <= 6'b0;
            pending_hsize_32 <= 1'b0;
            HRDATA <= 32'b0;
        end else begin
            // Clear pulses are one HCLK cycle wide.
            i2s_clear <= 1'b0;
            fifo_clear <= 1'b0;
            pcm_fifo_rd_en <= 1'b0;
            adpcm_fifo_rd_en <= 1'b0;

            // Capture write intent during the ADDRESS phase.
            // The write will be applied next cycle when HWDATA is valid.
            write_pending <= write_valid;
            pending_word_addr <= word_addr;
            pending_hsize_32 <= (HSIZE == 3'b010);

            // Apply the write during the DATA phase (one cycle after address).
            if (write_pending && pending_hsize_32) begin
                case (pending_word_addr)
                    ADDR_CONTROL: begin
                        rx_enable <= HWDATA[0];
                        i2s_clear <= HWDATA[1];
                        fifo_clear <= HWDATA[2];
                        compress_enable <= HWDATA[3];
                    end
                    default: begin
                        rx_enable <= rx_enable;
                    end
                endcase
            end

            // AHB-Lite read data is sampled in the DATA phase, one cycle
            // after the address phase. Snapshot the addressed register here
            // so HRDATA stays stable even when HADDR moves to the next access.
            if (read_valid) begin
                case (word_addr)
                    ADDR_CONTROL: begin
                        HRDATA <= {28'b0, compress_enable, fifo_clear, i2s_clear, rx_enable};
                    end
                    ADDR_STATUS: begin
                        HRDATA <= {17'b0,
                                   compress_enable,
                                   selected_fifo_usedw,
                                   rx_enable,
                                   sample_valid,
                                   selected_fifo_full,
                                   selected_fifo_empty};
                    end
                    ADDR_DATA: begin
                        HRDATA <= compress_enable ? adpcm_fifo_rd_data : {16'b0, pcm_fifo_rd_data};
                    end
                    default: begin
                        HRDATA <= 32'b0;
                    end
                endcase
            end

            if (data_read_hit) begin
                if (compress_enable) begin
                    adpcm_fifo_rd_en <= 1'b1;
                end else begin
                    pcm_fifo_rd_en <= 1'b1;
                end
            end
        end
    end

endmodule
