`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Module: fifo_sync
// Function:
//   Simple single-clock synchronous FIFO for student projects.
//
// Notes:
//   1. Read and write share the same clock.
//   2. A successful read updates rd_data in the same clk cycle.
//   3. Simultaneous read/write is supported when the FIFO is neither empty nor
//      full in a conflicting way.
//   4. DEPTH is derived from ADDR_WIDTH as 2**ADDR_WIDTH.
// -----------------------------------------------------------------------------
module fifo_sync #(
    parameter integer DATA_WIDTH = 16,
    parameter integer ADDR_WIDTH = 8
) (
    input  wire                  clk,
    input  wire                  rst_n,
    input  wire                  clr,
    input  wire                  wr_en,
    input  wire [DATA_WIDTH-1:0] wr_data,
    input  wire                  rd_en,
    output wire [DATA_WIDTH-1:0] rd_data,
    output wire                  full,
    output wire                  empty,
    output reg  [ADDR_WIDTH:0]   usedw
);

    localparam integer DEPTH = (1 << ADDR_WIDTH);

    reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];
    reg [ADDR_WIDTH-1:0] wr_ptr;
    reg [ADDR_WIDTH-1:0] rd_ptr;

    wire do_write;
    wire do_read;

    assign empty    = (usedw == { (ADDR_WIDTH + 1) {1'b0} });
    assign full     = (usedw == DEPTH[ADDR_WIDTH:0]);
    assign do_write = wr_en && !full;
    assign do_read  = rd_en && !empty;
    assign rd_data  = empty ? {DATA_WIDTH{1'b0}} : mem[rd_ptr];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr  <= {ADDR_WIDTH{1'b0}};
            rd_ptr  <= {ADDR_WIDTH{1'b0}};
            usedw   <= {(ADDR_WIDTH + 1){1'b0}};
        end else if (clr) begin
            wr_ptr  <= {ADDR_WIDTH{1'b0}};
            rd_ptr  <= {ADDR_WIDTH{1'b0}};
            usedw   <= {(ADDR_WIDTH + 1){1'b0}};
        end else begin
            if (do_write) begin
                mem[wr_ptr] <= wr_data;
                wr_ptr      <= wr_ptr + 1'b1;
            end

            if (do_read) begin
                rd_ptr  <= rd_ptr + 1'b1;
            end

            case ({do_write, do_read})
                2'b10: usedw <= usedw + 1'b1;
                2'b01: usedw <= usedw - 1'b1;
                default: usedw <= usedw;
            endcase
        end
    end

endmodule
