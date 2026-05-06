`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// Module: w25q64_model
// Function:
//   Behavioral simulation model of W25Q64JV (64 Mbit) SPI Flash.
//   SPI Mode 0 (CPOL=0, CPHA=0).
//
// Supported commands: 0x9F, 0x05, 0x06, 0x04, 0x02, 0x03, 0x20, 0xD8
// -----------------------------------------------------------------------------
module w25q64_model #(
    parameter integer MEM_KB = 128,        // memory size in KB
    parameter integer BUSY_SCK = 50        // busy duration in SCK cycles
) (
    input  wire sck,
    input  wire cs_n,
    input  wire si,
    output wire so
);

    // Commands
    localparam CMD_WREN  = 8'h06;
    localparam CMD_WRDI  = 8'h04;
    localparam CMD_RDSR  = 8'h05;
    localparam CMD_READ  = 8'h03;
    localparam CMD_PP    = 8'h02;
    localparam CMD_SE    = 8'h20;
    localparam CMD_BE    = 8'hD8;  // Block Erase 64KB
    localparam CMD_CE    = 8'hC7;  // Chip Erase
    localparam CMD_JEDEC = 8'h9F;

    // Memory
    localparam integer MEM_BYTES = MEM_KB * 1024;
    reg [7:0] mem [0:MEM_BYTES-1];

    // Status register: [1] = WEL, [0] = BUSY
    reg [1:0] status;

    // State machine
    reg [7:0]  cmd;
    reg [23:0] addr;
    reg [8:0]  byte_cnt;      // counts bytes in/out (0..255 for page)
    reg [3:0]  bit_cnt;       // 0..7 within a byte
    reg [7:0]  shift_in;
    reg [7:0]  shift_out;
    reg        output_active;
    reg        state_busy;
    reg [15:0] busy_cnt;
    reg [1:0]  jedec_idx;     // 0=manuf, 1=type, 2=capacity
    reg        continuous_read;  // for RDSR and READ which can loop
    reg        first_bit;        // suppress shift on the first negedge after load

    // Phase tracking
    // 0=idle/wait_cmd, 1=addr_2, 2=addr_1, 3=addr_0, 4=data
    reg [2:0] phase;

    // JEDEC ID values
    wire [7:0] jedec_bytes [0:2];
    assign jedec_bytes[0] = 8'hEF;
    assign jedec_bytes[1] = 8'h40;
    assign jedec_bytes[2] = 8'h17;

    // Tri-state MISO
    assign so = output_active ? shift_out[7] : 1'bz;

    // -------------------------------------------------------------------------
    // CS# management
    // -------------------------------------------------------------------------
    always @(negedge cs_n) begin
        // Entering a transaction
        cmd            <= 8'h00;
        addr           <= 24'h0;
        byte_cnt       <= 9'd0;
        bit_cnt        <= 4'd0;
        phase          <= 3'd0;
        output_active  <= 1'b0;
        jedec_idx      <= 2'd0;
        continuous_read <= 1'b0;
        first_bit       <= 1'b0;
        // Note: do NOT reset status here — WEL and BUSY persist across CS# toggles
    end

    always @(posedge cs_n) begin
        // Leaving a transaction
        output_active  <= 1'b0;
        phase          <= 3'd0;
        continuous_read <= 1'b0;
    end

    // -------------------------------------------------------------------------
    // Sample SI on SCK rising edge (SPI Mode 0)
    // -------------------------------------------------------------------------
    always @(posedge sck) begin
        if (!cs_n) begin
            shift_in <= {shift_in[6:0], si};

            case (phase)
                3'd0: begin  // receiving command byte
                    if (bit_cnt == 4'd7) begin
                        cmd     <= {shift_in[6:0], si};
                        bit_cnt <= 4'd0;
                        // Decode command for next phase
                        case ({shift_in[6:0], si})
                            CMD_RDSR: begin
                                continuous_read <= 1'b1;
                                output_active   <= 1'b1;
                                shift_out       <= {6'h00, status};
                                first_bit       <= 1'b1;
                                phase           <= 3'd4;
                            end
                            CMD_WREN: begin
                                status[1] <= 1'b1;
                                phase     <= 3'd0;
                            end
                            CMD_WRDI: begin
                                status[1] <= 1'b0;
                                phase     <= 3'd0;
                            end
                            CMD_JEDEC: begin
                                jedec_idx      <= 2'd0;
                                output_active  <= 1'b1;
                                shift_out      <= jedec_bytes[0];
                                first_bit      <= 1'b1;
                                phase          <= 3'd4;
                            end
                            CMD_READ, CMD_PP, CMD_SE, CMD_BE: begin
                                addr    <= 24'h0;
                                phase   <= 3'd3;  // start receiving 3 addr bytes
                            end
                            CMD_CE: begin
                                if (status[1]) begin
                                    chip_erase;
                                end
                                phase <= 3'd0;
                            end
                            default: begin
                                phase <= 3'd0;
                            end
                        endcase
                    end else begin
                        bit_cnt <= bit_cnt + 1'b1;
                    end
                end

                3'd3: begin  // receiving address byte 2 (MSB)
                    if (bit_cnt == 4'd7) begin
                        addr[23:16] <= {shift_in[6:0], si};
                        bit_cnt     <= 4'd0;
                        phase       <= 3'd2;
                    end else begin
                        bit_cnt <= bit_cnt + 1'b1;
                    end
                end

                3'd2: begin  // receiving address byte 1
                    if (bit_cnt == 4'd7) begin
                        addr[15:8] <= {shift_in[6:0], si};
                        bit_cnt    <= 4'd0;
                        phase      <= 3'd1;
                    end else begin
                        bit_cnt <= bit_cnt + 1'b1;
                    end
                end

                3'd1: begin  // receiving address byte 0 (LSB)
                    if (bit_cnt == 4'd7) begin
                        addr[7:0] <= {shift_in[6:0], si};
                        bit_cnt   <= 4'd0;
                        // All 3 address bytes received — execute command
                        case (cmd)
                            CMD_READ: begin
                                output_active   <= 1'b1;
                                continuous_read <= 1'b1;
                                shift_out       <= mem[addr % MEM_BYTES];
                                first_bit       <= 1'b1;
                                phase           <= 3'd4;
                            end
                            CMD_PP: begin
                                byte_cnt <= 9'd0;
                                phase    <= 3'd4;
                            end
                            CMD_SE: begin
                                if (status[1]) begin
                                    sector_erase(addr);
                                end
                                phase <= 3'd0;
                            end
                            CMD_BE: begin
                                if (status[1]) begin
                                    block_erase(addr);
                                end
                                phase <= 3'd0;
                            end
                        endcase
                    end else begin
                        bit_cnt <= bit_cnt + 1'b1;
                    end
                end

                3'd4: begin  // data phase — depends on command
                    if (cmd == CMD_PP) begin
                        // Page Program: receive data bytes
                        if (bit_cnt == 4'd7) begin
                            if (status[1] && !status[0]) begin
                                mem[(addr + byte_cnt) % MEM_BYTES] <= {shift_in[6:0], si};
                            end
                            byte_cnt <= byte_cnt + 1'b1;
                            bit_cnt  <= 4'd0;
                            // End of page program when CS# goes high
                            // (we stay in this phase until CS# rises)
                        end else begin
                            bit_cnt <= bit_cnt + 1'b1;
                        end
                    end
                    // For READ/RDSR/JEDEC: output is handled on negedge sck
                end

                default: ;
            endcase
        end
    end

    // -------------------------------------------------------------------------
    // Update MISO on SCK falling edge (SPI Mode 0)
    // -------------------------------------------------------------------------
    always @(negedge sck) begin
        if (!cs_n && output_active) begin
            // first_bit guards against an immediate shift on the negedge right
            // after shift_out was loaded at posedge.  Skipping that shift keeps
            // the MSB stable until the SSP samples it at the next posedge.
            if (first_bit) begin
                first_bit <= 1'b0;
            end else if (phase == 3'd4 && cmd == CMD_JEDEC) begin
                // JEDEC ID output: rotate through 3 bytes
                if (bit_cnt == 4'd7) begin
                    bit_cnt <= 4'd0;
                    if (jedec_idx == 2'd2) begin
                        // End of JEDEC ID
                        output_active <= 1'b0;
                    end else begin
                        jedec_idx  <= jedec_idx + 1'b1;
                        shift_out  <= jedec_bytes[jedec_idx + 1'b1];
                        first_bit  <= 1'b1;
                    end
                end else begin
                    shift_out <= {shift_out[6:0], 1'b0};
                    bit_cnt   <= bit_cnt + 1'b1;
                end
            end else if (phase == 3'd4 && cmd == CMD_RDSR) begin
                // Status register: continuously output
                if (bit_cnt == 4'd7) begin
                    bit_cnt   <= 4'd0;
                    shift_out <= {6'h00, status};
                    first_bit <= 1'b1;
                end else begin
                    shift_out <= {shift_out[6:0], 1'b0};
                    bit_cnt   <= bit_cnt + 1'b1;
                end
            end else if (phase == 3'd4 && cmd == CMD_READ) begin
                // Read data: auto-increment address
                if (bit_cnt == 4'd7) begin
                    bit_cnt <= 4'd0;
                    addr    <= addr + 1'b1;
                    shift_out <= mem[(addr + 1'b1) % MEM_BYTES];
                    first_bit <= 1'b1;
                end else begin
                    shift_out <= {shift_out[6:0], 1'b0};
                    bit_cnt   <= bit_cnt + 1'b1;
                end
            end
        end
    end

    // -------------------------------------------------------------------------
    // Page Program completion — triggered by CS# rising
    // -------------------------------------------------------------------------
    always @(posedge cs_n) begin
        if (cmd == CMD_PP && byte_cnt > 0 && status[1] && !status[0]) begin
            status[0] <= 1'b1;   // BUSY
            status[1] <= 1'b0;   // clear WEL
            state_busy <= 1'b1;
            busy_cnt   <= BUSY_SCK;
        end
    end

    // -------------------------------------------------------------------------
    // BUSY timer — counts SCK cycles while CS# is high
    // -------------------------------------------------------------------------
    always @(posedge sck) begin
        if (state_busy) begin
            if (busy_cnt > 0) begin
                busy_cnt <= busy_cnt - 1'b1;
            end else begin
                state_busy <= 1'b0;
                status[0]  <= 1'b0;  // clear BUSY
            end
        end
    end

    // -------------------------------------------------------------------------
    // Sector Erase  (0x20) — 4 KB
    // -------------------------------------------------------------------------
    task sector_erase;
        input [23:0] sect_addr;
        integer i;
        reg [23:0] base;
        begin
            base = {sect_addr[23:12], 12'h000};
            for (i = 0; i < 4096; i = i + 1) begin
                if ((base + i) < MEM_BYTES) begin
                    mem[base + i] = 8'hFF;
                end
            end
            status[0]  <= 1'b1;   // BUSY
            status[1]  <= 1'b0;   // clear WEL
            state_busy <= 1'b1;
            busy_cnt   <= BUSY_SCK * 10;
        end
    endtask

    // -------------------------------------------------------------------------
    // Block Erase  (0xD8) — 64 KB
    // -------------------------------------------------------------------------
    task block_erase;
        input [23:0] block_addr;
        integer i;
        reg [23:0] base;
        begin
            base = {block_addr[23:16], 16'h0000};
            for (i = 0; i < 65536; i = i + 1) begin
                if ((base + i) < MEM_BYTES) begin
                    mem[base + i] = 8'hFF;
                end
            end
            status[0]  <= 1'b1;
            status[1]  <= 1'b0;
            state_busy <= 1'b1;
            busy_cnt   <= BUSY_SCK * 20;   // block erase takes longer
        end
    endtask

    // -------------------------------------------------------------------------
    // Chip Erase  (0xC7 / 0x60) — entire memory
    // -------------------------------------------------------------------------
    task chip_erase;
        integer i;
        begin
            for (i = 0; i < MEM_BYTES; i = i + 1) begin
                mem[i] = 8'hFF;
            end
            status[0]  <= 1'b1;
            status[1]  <= 1'b0;
            state_busy <= 1'b1;
            busy_cnt   <= BUSY_SCK * 50;   // chip erase takes much longer
        end
    endtask

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------
    integer init_i;
    initial begin
        // Erase all memory (set to 0xFF)
        for (init_i = 0; init_i < MEM_BYTES; init_i = init_i + 1) begin
            mem[init_i] = 8'hFF;
        end

        // Preload test patterns
        // Sector 0 (0x000000): ASCII "HELLO" + padding
        mem[0] = 8'h48;  // H
        mem[1] = 8'h45;  // E
        mem[2] = 8'h4C;  // L
        mem[3] = 8'h4C;  // L
        mem[4] = 8'h4F;  // O
        mem[5] = 8'h0A;  // \n

        // Sector 1 (0x001000): counter pattern 0..255
        for (init_i = 0; init_i < 256; init_i = init_i + 1) begin
            mem[24'h001000 + init_i] = init_i[7:0];
        end

        // State
        cmd            = 8'h00;
        addr           = 24'h0;
        byte_cnt       = 9'd0;
        bit_cnt        = 4'd0;
        phase          = 3'd0;
        output_active  = 1'b0;
        status         = 2'b00;
        state_busy     = 1'b0;
        busy_cnt       = 16'd0;
        jedec_idx      = 2'd0;
        continuous_read = 1'b0;
        first_bit       = 1'b0;
    end

endmodule
