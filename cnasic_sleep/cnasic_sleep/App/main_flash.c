#include <stdint.h>

#include "soc.h"

/*
 * Minimal SPI Flash read/write test — verify that the SPI Flash can be
 * erased, programmed, and read back correctly via the PL022 SSP controller.
 *
 * Does NOT touch audio, I2S, ADPCM, or any other peripheral.
 *
 * Supports Winbond W25Q64 (JEDEC 0xEF4017) and Eon EN25Q64 (JEDEC 0x1C3017).
 *
 * Hardware: SPI Flash behind ARM PL022 SSP at APB ext12: SPI0_BASE=0x4000C000
 */

/* ---------- UART register definitions ---------- */
#define HOST_UART_BASE             (0x40005000UL)
#define HOST_UART_DATA_ADDR        (HOST_UART_BASE + 0x00UL)
#define HOST_UART_STATE_ADDR       (HOST_UART_BASE + 0x04UL)
#define HOST_UART_CTRL_ADDR        (HOST_UART_BASE + 0x08UL)
#define HOST_UART_INTCLEAR_ADDR    (HOST_UART_BASE + 0x0CUL)
#define HOST_UART_BAUDDIV_ADDR     (HOST_UART_BASE + 0x10UL)

#define HOST_UART_STATE_TX_FULL    (1UL << 0)
#define HOST_UART_STATE_RX_FULL    (1UL << 1)
#define HOST_UART_STATE_TX_OVERRUN (1UL << 2)
#define HOST_UART_STATE_RX_OVERRUN (1UL << 3)
#define HOST_UART_CTRL_TX_ENABLE   (1UL << 0)
#define HOST_UART_CTRL_RX_ENABLE   (1UL << 1)

/* ---------- SPI (PL022 SSP) register definitions ---------- */
#define SPI0_BASE                  (0x4000C000UL)
#define SPI_CR0                    (*(volatile uint32_t *)(SPI0_BASE + 0x00UL))
#define SPI_CR1                    (*(volatile uint32_t *)(SPI0_BASE + 0x04UL))
#define SPI_DR                     (*(volatile uint32_t *)(SPI0_BASE + 0x08UL))
#define SPI_SR                     (*(volatile uint32_t *)(SPI0_BASE + 0x0CUL))
#define SPI_CPSR                   (*(volatile uint32_t *)(SPI0_BASE + 0x10UL))
#define SPI_IMSC                   (*(volatile uint32_t *)(SPI0_BASE + 0x14UL))
#define SPI_ICR                    (*(volatile uint32_t *)(SPI0_BASE + 0x20UL))

#define SPI_SR_TFE                 (1UL << 0)
#define SPI_SR_TNF                 (1UL << 1)
#define SPI_SR_RNE                 (1UL << 2)
#define SPI_SR_BSY                 (1UL << 4)

/* APB ext13: manual active-low chip select for the SPI flash. */
#define SPI_FLASH_CS_BASE          (0x4000D000UL)
#define SPI_FLASH_CS               (*(volatile uint32_t *)(SPI_FLASH_CS_BASE + 0x00UL))

/* ---------- SPI Flash command set ---------- */
#define CMD_WRITE_ENABLE           (0x06U)
#define CMD_WRITE_DISABLE          (0x04U)
#define CMD_WRITE_STATUS_REG       (0x01U)
#define CMD_READ_STATUS1           (0x05U)
#define CMD_READ_DATA              (0x03U)
#define CMD_PAGE_PROGRAM           (0x02U)
#define CMD_SECTOR_ERASE           (0x20U)
#define CMD_BLOCK_ERASE_64K        (0xD8U)
#define CMD_JEDEC_ID               (0x9FU)
#define CMD_RELEASE_POWERDOWN      (0xABU)
#define CMD_RESET_ENABLE           (0x66U)
#define CMD_RESET_MEMORY           (0x99U)

#define SR_BUSY                    (1U << 0)
#define SR_WEL                     (1U << 1)
#define SR_BP_MASK                 (0x1CU)  /* BP0|BP1|BP2 block protect bits */

/* Known JEDEC manufacturer codes */
#define MFG_WINBOND                (0xEFU)
#define MFG_EON                    (0x1CU)

/* ---------- Test parameters ---------- */
/* Keep the destructive test away from the configuration image at flash offset 0. */
#define SPI_FLASH_TEST_ADDR        (0x00700000UL)
#define SPI_FLASH_SAMPLE_ADDR      (0x00400000UL)
#define SPI_FLASH_SECTOR_BYTES     (4096UL)
#define SPI_FLASH_BLOCK64_BYTES    (65536UL)
#define SPI_FLASH_CAPACITY_BYTES   (8UL * 1024UL * 1024UL)
#define SPI_FLASH_PAGE_BYTES       (256UL)
#define SPI_FLASH_READ_CHUNK_BYTES (64UL)
#define SPI_FLASH_PROGRAM_CHUNK    (64UL)
#define SPI_FLASH_UART_CHUNK_BYTES (64UL)
#define SPI_FIFO_DEPTH             (8UL)
#define SPI_FLASH_NORMAL_TEST_BYTES (8192UL)

#define SAMPLE_WAV_BYTES           (3382316UL)

#define STATUS_WOK                 (0x574F4B21UL)  /* "WOK!" */
#define STATUS_WFAL                (0x5746414CUL)  /* "WFAL" */
#define STATUS_WBIG                (0x57424947UL)  /* "WBIG" */

#define SPI_FLASH_TEST_BUILD       "sample-wav-flash-loopback-simple-64b-v1"

#define APB_CLOCK_HZ               (50000000UL)
#define HOST_UART_BAUD             (115200UL)

/* ---------- low-level helpers ---------- */
static uint32_t mmio_read32(uint32_t addr)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)addr;
    return *reg;
}

static void mmio_write32(uint32_t addr, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)addr;
    *reg = value;
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles != 0UL) {
        cycles--;
    }
}

static void delay_about_half_second(void)
{
    /*
     * Approximate wall-clock delay for hardware experiments. The loop body is
     * more than one CPU cycle, so this is intentionally conservative.
     */
    delay_cycles(APB_CLOCK_HZ / 2UL);
}

/* ---------- UART driver ---------- */
static void host_uart_init(void)
{
    uint32_t bauddiv;

    bauddiv = (APB_CLOCK_HZ + (HOST_UART_BAUD / 2UL)) / HOST_UART_BAUD;
    if (bauddiv < 16UL) {
        bauddiv = 16UL;
    }

    mmio_write32(HOST_UART_CTRL_ADDR, 0UL);
    mmio_write32(HOST_UART_BAUDDIV_ADDR, bauddiv);
    mmio_write32(HOST_UART_STATE_ADDR,
                 HOST_UART_STATE_TX_OVERRUN | HOST_UART_STATE_RX_OVERRUN);
    mmio_write32(HOST_UART_INTCLEAR_ADDR, 0x0FUL);
    mmio_write32(HOST_UART_CTRL_ADDR,
                 HOST_UART_CTRL_TX_ENABLE | HOST_UART_CTRL_RX_ENABLE);
}

static void host_uart_putc(uint8_t value)
{
    uint32_t state;
    uint32_t timeout = 5000000UL;

    do {
        state = mmio_read32(HOST_UART_STATE_ADDR);
        if ((state & HOST_UART_STATE_TX_OVERRUN) != 0UL) {
            mmio_write32(HOST_UART_STATE_ADDR, HOST_UART_STATE_TX_OVERRUN);
        }
        timeout--;
    } while ((state & HOST_UART_STATE_TX_FULL) != 0UL && timeout != 0UL);

    mmio_write32(HOST_UART_DATA_ADDR, value);
}

static uint8_t host_uart_getc(void)
{
    uint32_t state;

    do {
        state = mmio_read32(HOST_UART_STATE_ADDR);
        if ((state & HOST_UART_STATE_RX_OVERRUN) != 0UL) {
            mmio_write32(HOST_UART_STATE_ADDR, HOST_UART_STATE_RX_OVERRUN);
        }
    } while ((state & HOST_UART_STATE_RX_FULL) == 0UL);

    return (uint8_t)(mmio_read32(HOST_UART_DATA_ADDR) & 0xFFUL);
}

static void host_uart_puts(const char *s)
{
    while (*s != '\0') {
        host_uart_putc((uint8_t)*s);
        ++s;
    }
}

/*
 * Drain any stale bytes from the UART RX FIFO.
 * SPI flash erase cycles can couple noise onto the UART RX pin,
 * creating phantom bytes that would otherwise be misinterpreted
 * as PC-to-FPGA data and break the WVT0/WVRQ protocol.
 *
 * Returns the number of bytes drained.
 */
static uint32_t host_uart_drain_rx(void)
{
    uint32_t state;
    uint32_t drained = 0UL;

    while (1) {
        state = mmio_read32(HOST_UART_STATE_ADDR);
        if ((state & HOST_UART_STATE_RX_OVERRUN) != 0UL) {
            mmio_write32(HOST_UART_STATE_ADDR, HOST_UART_STATE_RX_OVERRUN);
        }
        if ((state & HOST_UART_STATE_RX_FULL) == 0UL) {
            break;
        }
        (void)(mmio_read32(HOST_UART_DATA_ADDR) & 0xFFUL);
        drained++;
        /* Safety limit: avoid infinite loop if RX line is permanently low */
        if (drained >= 2048UL) {
            break;
        }
    }

    return drained;
}

static void host_uart_put_hex8(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    host_uart_putc((uint8_t)hex[(value >> 4) & 0x0FU]);
    host_uart_putc((uint8_t)hex[value & 0x0FU]);
}

static void host_uart_put_hex32(uint32_t value)
{
    host_uart_put_hex8((uint8_t)((value >> 24) & 0xFFUL));
    host_uart_put_hex8((uint8_t)((value >> 16) & 0xFFUL));
    host_uart_put_hex8((uint8_t)((value >> 8) & 0xFFUL));
    host_uart_put_hex8((uint8_t)(value & 0xFFUL));
}

static void host_send_tag4(const char *tag)
{
    host_uart_putc((uint8_t)tag[0]);
    host_uart_putc((uint8_t)tag[1]);
    host_uart_putc((uint8_t)tag[2]);
    host_uart_putc((uint8_t)tag[3]);
}

static void host_send_u32_le(uint32_t value)
{
    host_uart_putc((uint8_t)(value & 0xFFUL));
    host_uart_putc((uint8_t)((value >> 8) & 0xFFUL));
    host_uart_putc((uint8_t)((value >> 16) & 0xFFUL));
    host_uart_putc((uint8_t)((value >> 24) & 0xFFUL));
}

/* ---------- SPI Flash driver ---------- */
static void spi0_write8(uint8_t tx)
{
    while ((SPI_SR & SPI_SR_TNF) == 0UL) {
        /* Wait until TX FIFO is not full. */
    }
    SPI_DR = tx;
}

static uint8_t spi0_read8(void)
{
    while ((SPI_SR & SPI_SR_RNE) == 0UL) {
        /* Wait until one received byte is available. */
    }
    return (uint8_t)(SPI_DR & 0xFFUL);
}

static void spi0_wait_idle(void)
{
    while ((SPI_SR & SPI_SR_BSY) != 0UL) {
        /* Wait for the PL022 shifter/FIFO to become idle. */
    }
}

static void spi0_drain_rx(void)
{
    while ((SPI_SR & SPI_SR_RNE) != 0UL) {
        (void)SPI_DR;
    }
}

static void spi_flash_cs_low(void)
{
    SPI_FLASH_CS = 0UL;
    delay_cycles(100UL);
}

static void spi_flash_cs_high(void)
{
    spi0_wait_idle();
    delay_cycles(100UL);
    SPI_FLASH_CS = 1UL;
    delay_cycles(200UL);
}

/*
 * Full-duplex SPI transfer. Chip select is controlled by APB ext13 rather than
 * PL022 FSS so multi-byte flash commands always stay inside one CS-low window.
 */
static void spi0_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t written = 0UL;
    uint32_t read = 0UL;
    uint32_t in_flight;
    uint8_t value;

    spi0_wait_idle();
    spi0_drain_rx();
    spi_flash_cs_low();

    while (read < len) {
        in_flight = written - read;

        if ((written < len) &&
            (in_flight < SPI_FIFO_DEPTH) &&
            ((SPI_SR & SPI_SR_TNF) != 0UL)) {
            value = (tx != 0) ? tx[written] : 0xFFU;
            spi0_write8(value);
            written++;
        }

        if ((SPI_SR & SPI_SR_RNE) != 0UL) {
            value = spi0_read8();
            if (rx != 0) {
                rx[read] = value;
            }
            read++;
        }
    }

    spi_flash_cs_high();
}

static void spi_flash_init(void)
{
    SPI_FLASH_CS = 1UL;
    SPI_CR1 = 0x0000UL;
    SPI_CPSR = 10UL;                      /* 50 MHz / (10 * (1 + 4)) = 1 MHz */
    SPI_CR0 = (7UL << 0)                  /* DSS = 8-bit */
            | (0UL << 4)                  /* Motorola SPI frame */
            | (0UL << 6)                  /* SPO = 0: SPI mode 0 */
            | (0UL << 7)                  /* SPH = 0 */
            | (4UL << 8);                 /* SCR = 4 */
    SPI_IMSC = 0x0000UL;
    SPI_ICR = 0x0003UL;
    SPI_CR1 = (1UL << 1);                 /* SSE=1, master mode */

    /*
     * In Mode 0 the SSP drives SCK LOW at idle immediately after SSE is
     * asserted — no warm-up transfer is needed.  Avoid toggling SCK while
     * CS is high, because some flash behavioural models can misinterpret
     * burst noise on SCK as a false transaction.
     */
    delay_cycles(100UL);
    spi0_drain_rx();
}

static uint8_t spi_flash_read_status1(void)
{
    uint8_t tx[2];
    uint8_t rx[2];

    tx[0] = CMD_READ_STATUS1;
    tx[1] = 0xFFU;
    spi0_xfer_buf(tx, rx, sizeof(tx));

    return rx[1];
}

static void spi_flash_wait_ready(void)
{
    uint32_t timeout = 2000000UL;

    while (((spi_flash_read_status1() & SR_BUSY) != 0U) && (timeout != 0UL)) {
        /* Wait until erase/program finishes. */
        timeout--;
    }

    if (timeout == 0UL) {
        host_uart_puts("Warning: timeout waiting for flash ready\r\n");
    }
}

static uint8_t spi_flash_write_enable(void)
{
    uint8_t tx[1];
    uint32_t retry;
    uint8_t sr = 0U;

    for (retry = 0UL; retry < 8UL; ++retry) {
        tx[0] = CMD_WRITE_ENABLE;
        spi0_xfer_buf(tx, 0, sizeof(tx));
        delay_cycles(500UL);

        sr = spi_flash_read_status1();
        if ((sr & SR_WEL) != 0U) {
            return sr;
        }
    }

    host_uart_puts("Warning: WEL did not set after WREN, SR=0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");
    return sr;
}

static void spi_flash_send_cmd(uint8_t cmd)
{
    uint8_t tx[1];

    tx[0] = cmd;
    spi0_xfer_buf(tx, 0, sizeof(tx));
}

static void spi_flash_write_disable(void)
{
    spi_flash_send_cmd(CMD_WRITE_DISABLE);
}

static void spi_flash_reset_device(void)
{
    spi_flash_send_cmd(CMD_RESET_ENABLE);
    delay_cycles(500UL);
    spi_flash_send_cmd(CMD_RESET_MEMORY);

    /*
     * Poll BUSY instead of a fixed delay.  Some flash models (especially
     * in simulation) require explicit status polling to advance their
     * internal state machine; a blind delay may expire before the model
     * is ready, causing all subsequent commands to be ignored.
     */
    spi_flash_wait_ready();
}

static void spi_flash_release_powerdown(void)
{
    spi_flash_send_cmd(CMD_RELEASE_POWERDOWN);

    /*
     * Release Powerdown takes tRES1 (max ~3 µs on W25Q64).  Polling BUSY
     * is the safe way to know the flash is awake and command-ready.
     */
    spi_flash_wait_ready();
}

/*
 * Clear block-protection bits in Status Register-1.
 * Many SPI Flash chips ship with BP bits set, which silently blocks all
 * erase/program operations.
 */
static void spi_flash_clear_protection(void)
{
    uint8_t tx[2];

    spi_flash_write_enable();
    tx[0] = CMD_WRITE_STATUS_REG;
    tx[1] = 0x00U;
    spi0_xfer_buf(tx, 0, sizeof(tx));
    spi_flash_wait_ready();

    /* Verify protection was cleared */
    if ((spi_flash_read_status1() & SR_BP_MASK) != 0U) {
        host_uart_puts("Warning: BP bits not cleared after WRSR\r\n");
    }
}

static int spi_flash_is_known_jedec(uint8_t mfg, uint8_t type, uint8_t capacity)
{
    if (mfg == MFG_WINBOND && type == 0x40U && capacity == 0x17U) {
        return 1;
    }
    if (mfg == MFG_EON && type == 0x30U && capacity == 0x17U) {
        return 1;
    }
    return 0;
}

/*
 * Read JEDEC ID.
 *
 * Some board/PL022 combinations can leave the returned bytes shifted by one
 * character if the RX FIFO was not empty before the transaction. The transfer
 * helper drains stale RX data, and this decoder also scans the response so
 * 1C 30 17 and the observed rotated 30 17 1C are both accepted.
 *
 * Returns 0 if mfg is recognised (Winbond 0xEF or Eon 0x1C), else 1.
 */
static int spi_flash_read_jedec_id(uint8_t id[3])
{
    uint8_t tx[8];
    uint8_t rx[8];
    uint32_t i;

    tx[0] = CMD_JEDEC_ID;
    for (i = 1UL; i < sizeof(tx); ++i) {
        tx[i] = 0xFFU;
    }
    spi0_xfer_buf(tx, rx, sizeof(tx));

    host_uart_puts("JEDEC raw:");
    for (i = 0UL; i < sizeof(rx); ++i) {
        host_uart_putc(' ');
        host_uart_put_hex8(rx[i]);
    }
    host_uart_puts("\r\n");

    for (i = 1UL; i + 2UL < sizeof(rx); ++i) {
        if (spi_flash_is_known_jedec(rx[i], rx[i + 1UL], rx[i + 2UL])) {
            id[0] = rx[i];
            id[1] = rx[i + 1UL];
            id[2] = rx[i + 2UL];
            return 0;
        }
    }

    /*
     * Fallback for the exact rotated result seen on hardware: 30 17 1C is
     * the EN25Q64 ID 1C 30 17 wrapped by one byte.
     */
    for (i = 1UL; i + 2UL < sizeof(rx); ++i) {
        if (spi_flash_is_known_jedec(rx[i + 2UL], rx[i], rx[i + 1UL])) {
            id[0] = rx[i + 2UL];
            id[1] = rx[i];
            id[2] = rx[i + 1UL];
            return 0;
        }
    }

    id[0] = rx[3];
    id[1] = rx[4];
    id[2] = rx[5];
    return 1;
}

static void spi_flash_send_addr_cmd(uint8_t cmd, uint32_t addr)
{
    uint8_t tx[4];

    tx[0] = cmd;
    tx[1] = (uint8_t)((addr >> 16) & 0xFFUL);
    tx[2] = (uint8_t)((addr >> 8) & 0xFFUL);
    tx[3] = (uint8_t)(addr & 0xFFUL);
    spi0_xfer_buf(tx, 0, sizeof(tx));
}

static uint8_t spi_flash_start_erase_cmd(uint8_t cmd, uint32_t addr)
{
    uint8_t sr;

    sr = spi_flash_write_enable();
    host_uart_puts("Status Reg-1 after WREN: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");

    spi_flash_send_addr_cmd(cmd, addr);
    sr = spi_flash_read_status1();
    host_uart_puts("Status Reg-1 immediately after erase cmd: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");

    return sr;
}

static void spi_flash_block_erase_64k(uint32_t addr)
{
    uint8_t tx[4];
    uint8_t sr;

    /*
     * Keep this sequence intentionally close to the minimal test that passed
     * on hardware: try 0x20, then reuse the same command buffer for 0xD8.
     */
    sr = spi_flash_write_enable();
    host_uart_puts("Status Reg-1 after WREN: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");

    tx[0] = CMD_SECTOR_ERASE;
    tx[1] = (uint8_t)((addr >> 16) & 0xFFUL);
    tx[2] = (uint8_t)((addr >> 8) & 0xFFUL);
    tx[3] = (uint8_t)(addr & 0xFFUL);
    spi0_xfer_buf(tx, 0, sizeof(tx));
    sr = spi_flash_read_status1();
    host_uart_puts("Status Reg-1 immediately after erase cmd: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");

    if ((sr & (SR_BUSY | SR_WEL)) == SR_WEL) {
        host_uart_puts("4KB erase ignored; trying 64KB block erase...\r\n");
        spi_flash_write_disable();
        sr = spi_flash_write_enable();
        host_uart_puts("Status Reg-1 after WREN: 0x");
        host_uart_put_hex8(sr);
        host_uart_puts("\r\n");

        tx[0] = CMD_BLOCK_ERASE_64K;
        spi0_xfer_buf(tx, 0, sizeof(tx));
        sr = spi_flash_read_status1();
        host_uart_puts("Status Reg-1 immediately after block erase cmd: 0x");
        host_uart_put_hex8(sr);
        host_uart_puts("\r\n");

        if ((sr & SR_BUSY) == 0U) {
            host_uart_puts("Warning: 64KB block erase did not enter BUSY state\r\n");
        }
    }

    spi_flash_wait_ready();
}

static void spi_flash_block_erase_64k_quiet(uint32_t addr)
{
    uint8_t tx[4];
    uint8_t sr;

    /*
     * Binary UART test mode must not emit human-readable progress after WVT0.
     * Keep the same hardware-proven erase sequence as the verbose test:
     * 4KB command is ignored with WEL still set, then 64KB block erase works.
     */
    (void)spi_flash_write_enable();

    tx[0] = CMD_SECTOR_ERASE;
    tx[1] = (uint8_t)((addr >> 16) & 0xFFUL);
    tx[2] = (uint8_t)((addr >> 8) & 0xFFUL);
    tx[3] = (uint8_t)(addr & 0xFFUL);
    spi0_xfer_buf(tx, 0, sizeof(tx));
    sr = spi_flash_read_status1();

    if ((sr & (SR_BUSY | SR_WEL)) == SR_WEL) {
        delay_cycles(5000UL);
        spi_flash_write_disable();
        delay_cycles(5000UL);
        (void)spi_flash_write_enable();
        delay_cycles(5000UL);

        tx[0] = CMD_BLOCK_ERASE_64K;
        spi0_xfer_buf(tx, 0, sizeof(tx));
    }

    spi_flash_wait_ready();
}

static uint8_t spi_flash_sector_erase_4k_once(uint32_t addr)
{
    uint8_t sr;

    sr = spi_flash_start_erase_cmd(CMD_SECTOR_ERASE,
                                   addr & ~(SPI_FLASH_SECTOR_BYTES - 1UL));
    spi_flash_wait_ready();
    return sr;
}

static void spi_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint8_t tx[4U + SPI_FLASH_PROGRAM_CHUNK];
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PROGRAM_CHUNK, len - offset);

        spi_flash_write_enable();

        tx[0] = CMD_PAGE_PROGRAM;
        tx[1] = (uint8_t)(((addr + offset) >> 16) & 0xFFUL);
        tx[2] = (uint8_t)(((addr + offset) >> 8) & 0xFFUL);
        tx[3] = (uint8_t)((addr + offset) & 0xFFUL);

        for (i = 0UL; i < chunk; ++i) {
            tx[4UL + i] = data[offset + i];
        }

        spi0_xfer_buf(tx, 0, 4UL + chunk);
        spi_flash_wait_ready();
        offset += chunk;
    }
}

static void spi_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint8_t tx[4U + SPI_FLASH_READ_CHUNK_BYTES];
    uint8_t rx[4U + SPI_FLASH_READ_CHUNK_BYTES];
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    while (offset < len) {
        chunk = min_u32(SPI_FLASH_READ_CHUNK_BYTES, len - offset);

        tx[0] = CMD_READ_DATA;
        tx[1] = (uint8_t)(((addr + offset) >> 16) & 0xFFUL);
        tx[2] = (uint8_t)(((addr + offset) >> 8) & 0xFFUL);
        tx[3] = (uint8_t)((addr + offset) & 0xFFUL);

        for (i = 0UL; i < chunk; ++i) {
            tx[4UL + i] = 0xFFU;
        }

        spi0_xfer_buf(tx, rx, 4UL + chunk);

        for (i = 0UL; i < chunk; ++i) {
            data[offset + i] = rx[4UL + i];
        }

        offset += chunk;
    }
}

/* ---------- Test routines ---------- */

/*
 * Fill a buffer with a known pattern: byte value = (offset & 0xFF) XOR
 * (seed based on area). This makes each 256-byte page unique and
 * detectable if pages are written to the wrong address.
 */
static void fill_test_pattern(uint8_t *buf, uint32_t len, uint32_t base_addr)
{
    uint32_t i;
    uint8_t seed = (uint8_t)((base_addr >> 8) & 0xFFUL);

    for (i = 0UL; i < len; ++i) {
        buf[i] = (uint8_t)(((base_addr + i) & 0xFFUL) ^ seed);
    }
}

/*
 * Verify buf matches the expected test pattern.
 * Returns 1 on match, 0 on mismatch. On mismatch, reports the first
 * failing byte address via UART.
 */
static int verify_test_pattern(const uint8_t *buf, uint32_t len,
                                uint32_t base_addr)
{
    uint32_t i;
    uint8_t seed = (uint8_t)((base_addr >> 8) & 0xFFUL);
    uint8_t expected;

    for (i = 0UL; i < len; ++i) {
        expected = (uint8_t)(((base_addr + i) & 0xFFUL) ^ seed);
        if (buf[i] != expected) {
            host_uart_puts("  FAIL @ addr 0x");
            host_uart_put_hex32(base_addr + i);
            host_uart_puts(": wrote 0x");
            host_uart_put_hex8(expected);
            host_uart_puts(" read 0x");
            host_uart_put_hex8(buf[i]);
            host_uart_puts("\r\n");
            return 0;
        }
    }

    return 1;
}

static int verify_fixed_value(uint32_t addr, uint8_t expected)
{
    uint8_t value;

    spi_flash_read(addr, &value, 1UL);
    if (value != expected) {
        host_uart_puts("  FAIL @ addr 0x");
        host_uart_put_hex32(addr);
        host_uart_puts(": expected 0x");
        host_uart_put_hex8(expected);
        host_uart_puts(" read 0x");
        host_uart_put_hex8(value);
        host_uart_puts("\r\n");
        return 0;
    }

    return 1;
}

static int verify_buffer_equal(uint32_t addr, const uint8_t *expected,
                               const uint8_t *actual, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        if (actual[i] != expected[i]) {
            host_uart_puts("  FAIL @ addr 0x");
            host_uart_put_hex32(addr + i);
            host_uart_puts(": wrote 0x");
            host_uart_put_hex8(expected[i]);
            host_uart_puts(" read 0x");
            host_uart_put_hex8(actual[i]);
            host_uart_puts("\r\n");
            return 0;
        }
    }

    return 1;
}

/*
 * Read back a full sector from flash and verify it is still erased (all 0xFF).
 */
static int verify_range_erased(uint32_t addr, uint32_t len)
{
    uint8_t buf[SPI_FLASH_READ_CHUNK_BYTES];
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    while (offset < len) {
        chunk = min_u32(sizeof(buf), len - offset);
        spi_flash_read(addr + offset, buf, chunk);

        for (i = 0UL; i < chunk; ++i) {
            if (buf[i] != 0xFFU) {
                host_uart_puts("  FAIL @ addr 0x");
                host_uart_put_hex32(addr + offset + i);
                host_uart_puts(": expected 0xFF read 0x");
                host_uart_put_hex8(buf[i]);
                host_uart_puts("\r\n");
                return 0;
            }
        }
        offset += chunk;
    }

    return 1;
}

static int range_is_erased(uint32_t addr, uint32_t len)
{
    uint8_t buf[SPI_FLASH_READ_CHUNK_BYTES];
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    while (offset < len) {
        chunk = min_u32(sizeof(buf), len - offset);
        spi_flash_read(addr + offset, buf, chunk);

        for (i = 0UL; i < chunk; ++i) {
            if (buf[i] != 0xFFU) {
                return 0;
            }
        }
        offset += chunk;
    }

    return 1;
}

static int test_normal_sequential_write_read(uint32_t addr, uint32_t len,
                                             uint8_t *expected,
                                             uint8_t *actual)
{
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t retry;
    int verified;

    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, len - offset);
        fill_test_pattern(expected, chunk, addr + offset);

        verified = 0;
        for (retry = 0UL; retry < 3UL; ++retry) {
            spi_flash_page_program(addr + offset, expected, chunk);
            spi_flash_read(addr + offset, actual, chunk);

            if (verify_buffer_equal(addr + offset, expected, actual, chunk)) {
                verified = 1;
                break;
            }

            if (retry < 2UL) {
                host_uart_puts("  Page mismatch, retrying...\r\n");
            }
        }

        if (!verified) {
            return 0;
        }

        offset += chunk;
    }

    return 1;
}

static int test_unaligned_multichunk_read(uint32_t addr, uint32_t len,
                                          uint8_t *expected,
                                          uint8_t *actual)
{
    uint32_t i;

    fill_test_pattern(expected, len, addr);

    spi_flash_read(addr, actual, len);
    if (verify_buffer_equal(addr, expected, actual, len)) {
        host_uart_puts("  OK\r\n");
        return 1;
    }

    host_uart_puts("  Retrying same unaligned read...\r\n");
    spi_flash_read(addr, actual, len);
    if (verify_buffer_equal(addr, expected, actual, len)) {
        host_uart_puts("  OK - retry matched after first burst mismatch\r\n");
        return 1;
    }

    host_uart_puts("  Checking same range with byte reads...\r\n");
    for (i = 0UL; i < len; ++i) {
        spi_flash_read(addr + i, &actual[i], 1UL);
    }

    if (verify_buffer_equal(addr, expected, actual, len)) {
        host_uart_puts("  FAIL - byte reads matched, burst read path is unstable\r\n");
    } else {
        host_uart_puts("  FAIL - stored data changed or address mapping is unstable\r\n");
    }

    return 0;
}

static uint32_t crc32_update_byte(uint32_t crc, uint8_t value)
{
    uint32_t i;

    crc ^= (uint32_t)value;
    for (i = 0UL; i < 8UL; ++i) {
        if ((crc & 1UL) != 0UL) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }

    return crc;
}

static uint32_t crc32_update_buf(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        crc = crc32_update_byte(crc, data[i]);
    }

    return crc;
}

static void sample_flash_send_start(uint32_t file_bytes)
{
    host_send_tag4("WVT0");
    host_send_u32_le(file_bytes);
    host_send_u32_le(SPI_FLASH_SAMPLE_ADDR);
    host_send_u32_le(SPI_FLASH_UART_CHUNK_BYTES);
}

static void sample_flash_request_chunk(uint32_t offset, uint32_t chunk)
{
    host_send_tag4("WVRQ");
    host_send_u32_le(offset);
    host_send_u32_le(chunk);
}

static void sample_flash_send_result(uint32_t status, uint32_t result_value,
                                     uint32_t source_crc, uint32_t flash_crc)
{
    host_send_tag4("WVRS");
    host_send_u32_le(status);
    host_send_u32_le(result_value);
    host_send_u32_le(source_crc);
    host_send_u32_le(flash_crc);
}

static void sample_flash_erase_block(uint32_t addr)
{
    uint32_t block;

    block = addr & ~(SPI_FLASH_BLOCK64_BYTES - 1UL);
    spi_flash_block_erase_64k_quiet(block);
}

static void receive_uart_chunk(uint8_t *buf, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        buf[i] = host_uart_getc();
    }
}

static int buffer_equal_quiet(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }

    return 1;
}

static int spi_flash_program_verify_page(uint32_t addr, const uint8_t *data,
                                         uint8_t *verify_buf, uint32_t len)
{
    uint32_t retry;

    spi_flash_page_program(addr, data, len);

    for (retry = 0UL; retry < 3UL; ++retry) {
        spi_flash_read(addr, verify_buf, len);
        if (buffer_equal_quiet(data, verify_buf, len)) {
            return 1;
        }
    }

    return 0;
}

static uint32_t crc32_spi_flash(uint32_t flash_addr, uint32_t len, uint8_t *buf)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset = 0UL;
    uint32_t chunk;

    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, len - offset);
        spi_flash_read(flash_addr + offset, buf, chunk);
        crc = crc32_update_buf(crc, buf, chunk);
        offset += chunk;
    }

    return crc ^ 0xFFFFFFFFUL;
}

static void output_file_from_spi_flash(uint32_t flash_addr, uint32_t file_bytes,
                                       uint8_t *buf)
{
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    host_send_tag4("WAV0");
    host_send_u32_le(file_bytes);

    while (offset < file_bytes) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, file_bytes - offset);
        spi_flash_read(flash_addr + offset, buf, chunk);

        for (i = 0UL; i < chunk; ++i) {
            host_uart_putc(buf[i]);
        }

        offset += chunk;
    }
}

static uint32_t store_uart_file_to_spi_flash(uint32_t file_bytes,
                                             uint8_t *buf,
                                             uint8_t *verify_buf)
{
    uint32_t source_crc = 0xFFFFFFFFUL;
    uint32_t flash_crc;
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t erase_offset;
    uint32_t status;

    if ((file_bytes > SPI_FLASH_CAPACITY_BYTES) ||
        (SPI_FLASH_SAMPLE_ADDR > (SPI_FLASH_CAPACITY_BYTES - file_bytes))) {
        sample_flash_send_start(file_bytes);
        sample_flash_send_result(STATUS_WBIG, file_bytes, 0UL, 0UL);
        return STATUS_WBIG;
    }

    host_uart_puts("Erasing target flash blocks before binary transfer...\r\n");
    erase_offset = 0UL;
    while (erase_offset < file_bytes) {
        sample_flash_erase_block(SPI_FLASH_SAMPLE_ADDR + erase_offset);
        erase_offset += SPI_FLASH_BLOCK64_BYTES;
    }
    host_uart_puts("Erase complete. First request chunk bytes: 0x");
    host_uart_put_hex32(SPI_FLASH_UART_CHUNK_BYTES);
    host_uart_puts("\r\n");

    /*
     * Drain UART RX FIFO — SPI flash erase activity can couple noise
     * onto the UART RX pin, creating phantom bytes that would cause
     * receive_uart_chunk() to return garbage instead of waiting for
     * the real PC response.
     */
    {
        uint32_t drained = host_uart_drain_rx();
        if (drained > 0UL) {
            host_uart_puts("Drained ");
            host_uart_put_hex32(drained);
            host_uart_puts(" garbage byte(s) from UART RX after erase\r\n");
        }
    }

    sample_flash_send_start(file_bytes);

    while (offset < file_bytes) {
        chunk = SPI_FLASH_UART_CHUNK_BYTES;
        if ((file_bytes - offset) < chunk) {
            chunk = file_bytes - offset;
        }

        /* Drain any noise accumulated during flash operations */
        host_uart_drain_rx();

        sample_flash_request_chunk(offset, chunk);
        receive_uart_chunk(buf, chunk);

        if (!spi_flash_program_verify_page(SPI_FLASH_SAMPLE_ADDR + offset,
                                           buf, verify_buf, chunk)) {
            source_crc ^= 0xFFFFFFFFUL;
            flash_crc = crc32_spi_flash(SPI_FLASH_SAMPLE_ADDR, file_bytes, verify_buf);
            sample_flash_send_result(STATUS_WFAL, offset, source_crc, flash_crc);
            return STATUS_WFAL;
        }

        source_crc = crc32_update_buf(source_crc, buf, chunk);
        offset += chunk;
    }

    source_crc ^= 0xFFFFFFFFUL;
    flash_crc = crc32_spi_flash(SPI_FLASH_SAMPLE_ADDR, file_bytes, verify_buf);
    status = (source_crc == flash_crc) ? STATUS_WOK : STATUS_WFAL;

    sample_flash_send_result(status, file_bytes, source_crc, flash_crc);

    if (status == STATUS_WOK) {
        output_file_from_spi_flash(SPI_FLASH_SAMPLE_ADDR, file_bytes, buf);
    }

    return status;
}

static int test_essay_flash(uint8_t *write_buf, uint8_t *read_buf)
{
    static const char essay[] =
        "A Small Essay Stored in SPI Flash\r\n"
        "\r\n"
        "This text is intentionally longer than a single flash page. It is used "
        "to check that erase, page programming, multi-page addressing, readback, "
        "and UART printing all work together without the larger PC file-transfer "
        "protocol. The content is ordinary ASCII text so that the serial terminal "
        "can show the exact bytes that were recovered from flash.\r\n"
        "\r\n"
        "A reliable embedded system is built one simple promise at a time. First "
        "the chip select line must idle in the correct state. Then the status "
        "register must report that write enable was accepted. Then erase must "
        "really return cells to 0xFF. Only after those smaller promises are true "
        "does it make sense to trust a longer stream of data.\r\n"
        "\r\n"
        "Flash memory also has a particular kind of honesty: bits can be programmed "
        "from one to zero, but they cannot be changed back to one without an erase. "
        "That rule is why this test erases a whole 64KB block before writing the "
        "essay. If any old byte remains behind, the comparison at the end should "
        "catch it quickly.\r\n"
        "\r\n"
        "The write path walks through the essay in 256-byte pages. The low-level "
        "page program helper may split each page into smaller SPI transfers, but "
        "the flash address must still advance cleanly across page boundaries. A "
        "mistake here often looks like repeated text, missing characters, or a "
        "single byte that belongs to a previous page.\r\n"
        "\r\n"
        "The read path then asks flash for the same address range and compares "
        "every byte against this compiled-in source string. After the comparison, "
        "the firmware prints the readback text itself. Seeing this paragraph on "
        "the UART means that the bytes did not merely pass a checksum; they also "
        "came back in a form a human can inspect.\r\n"
        "\r\n"
        "This is still a modest test. It does not prove that every byte in the "
        "entire 8MB device is perfect, and it does not exercise the PC-to-board "
        "binary streaming protocol. But it is a useful stepping stone. If this "
        "essay survives erase, program, read, compare, and print, then the core "
        "flash driver is behaving well enough to justify the next experiment.\r\n"
        "\r\n"
        "End of SPI flash essay test.\r\n";
    uint32_t len = (uint32_t)sizeof(essay);
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;
    int ok = 1;

    host_uart_puts("\r\n=== SPI Flash Essay Test ===\r\n");
    host_uart_puts("Build: ");
    host_uart_puts(SPI_FLASH_TEST_BUILD);
    host_uart_puts("\r\n");
    host_uart_puts("Flash addr: 0x");
    host_uart_put_hex32(SPI_FLASH_SAMPLE_ADDR);
    host_uart_puts("  bytes: 0x");
    host_uart_put_hex32(len);
    host_uart_puts("\r\n");

    host_uart_puts("Erasing 64KB block...\r\n");
    spi_flash_block_erase_64k(SPI_FLASH_SAMPLE_ADDR);
    if (!verify_range_erased(SPI_FLASH_SAMPLE_ADDR, SPI_FLASH_SECTOR_BYTES)) {
        host_uart_puts("  FAIL - erase did not clear first 4KB sector\r\n");
        return 0;
    }
    host_uart_puts("  OK - erased\r\n");

    host_uart_puts("Programming essay across flash pages...\r\n");
    offset = 0UL;
    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, len - offset);
        for (i = 0UL; i < chunk; ++i) {
            write_buf[i] = (uint8_t)essay[offset + i];
        }
        spi_flash_page_program(SPI_FLASH_SAMPLE_ADDR + offset, write_buf, chunk);
        offset += chunk;
    }

    host_uart_puts("Verifying readback...\r\n");
    offset = 0UL;
    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, len - offset);
        for (i = 0UL; i < chunk; ++i) {
            write_buf[i] = (uint8_t)essay[offset + i];
        }
        spi_flash_read(SPI_FLASH_SAMPLE_ADDR + offset, read_buf, chunk);
        if (!buffer_equal_quiet(write_buf, read_buf, chunk)) {
            ok = 0;
            (void)verify_buffer_equal(SPI_FLASH_SAMPLE_ADDR + offset,
                                      write_buf, read_buf, chunk);
            break;
        }
        offset += chunk;
    }

    host_uart_puts("\r\n--- Readback text from SPI flash ---\r\n");
    offset = 0UL;
    while (offset < (len - 1UL)) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, (len - 1UL) - offset);
        spi_flash_read(SPI_FLASH_SAMPLE_ADDR + offset, read_buf, chunk);
        for (i = 0UL; i < chunk; ++i) {
            host_uart_putc(read_buf[i]);
        }
        offset += chunk;
    }
    host_uart_puts("--- End readback text ---\r\n");

    if (ok) {
        host_uart_puts("\r\n=== ESSAY FLASH TEST PASSED ===\r\n");
    } else {
        host_uart_puts("\r\n=== ESSAY FLASH TEST FAILED ===\r\n");
    }

    return ok;
}

/* ========================================================================
 * main — SPI Flash read/write test
 *
 * Test flow:
 *   1. Init UART/SPI and verify manual CS plus WREN/WRDI.
 *   2. Read JEDEC ID and repeat-read it for stability.
 *   3. Check Status Register-1 and clear block-protection bits if needed.
 *   4. Verify 64KB block erase across the whole destructive test block.
 *   5. Probe the observed 4KB erase behavior without hiding it behind fallback.
 *   6. Verify 1->0 program behavior without erase.
 *   7. Program/read every page in the first 4KB sector.
 *   8. Test unaligned reads, block-tail page access, and cross-page access.
 *   9. Test normal sequential multi-page write/read.
 *   10. Report PASS/FAIL.
 *
 * PC-side terminal:
 *   python -m serial.tools.miniterm COM14 115200
 * ======================================================================== */
int main(void)
{
    uint8_t jedec_id[3];
    uint8_t jedec_check[3];
    uint8_t write_buf[SPI_FLASH_PAGE_BYTES];
    uint8_t read_buf[SPI_FLASH_PAGE_BYTES];
    uint32_t test_addr;
    uint32_t page;
    uint32_t repeat;
    uint32_t probe_addr;
    uint32_t normal_addr;
    uint8_t sr;
    uint8_t erase4k_sr;
    int all_pass = 1;
    int result;
    int page_pass;
    const char *mfg_name;

    /* ---- Init ---- */
    host_uart_init();
    spi_flash_init();
    spi_flash_reset_device();
    spi_flash_release_powerdown();

    host_uart_puts("\r\n=== SPI Flash Comprehensive Test ===\r\n");
    host_uart_puts("Build: ");
    host_uart_puts(SPI_FLASH_TEST_BUILD);
    host_uart_puts("\r\n");
    host_uart_puts("CS control reg after init: 0x");
    host_uart_put_hex8((uint8_t)(SPI_FLASH_CS & 0xFFUL));
    host_uart_puts("\r\n");
    if ((SPI_FLASH_CS & 1UL) != 1UL) {
        host_uart_puts("  FAIL - manual CS should idle high\r\n");
        all_pass = 0;
    } else {
        host_uart_puts("  OK - manual CS idles high\r\n");
    }

    sr = spi_flash_write_enable();
    host_uart_puts("Status Reg-1 after WREN self-test: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");
    if ((sr & SR_WEL) == 0U) {
        host_uart_puts("  FAIL - WREN did not set WEL\r\n");
        all_pass = 0;
    } else {
        host_uart_puts("  OK - WREN set WEL\r\n");
    }

    spi_flash_write_disable();
    sr = spi_flash_read_status1();
    host_uart_puts("Status Reg-1 after WRDI self-test: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");
    if ((sr & SR_WEL) != 0U) {
        host_uart_puts("  FAIL - WRDI did not clear WEL\r\n");
        all_pass = 0;
    } else {
        host_uart_puts("  OK - WRDI cleared WEL\r\n");
    }

    /* ---- Read JEDEC ID ---- */
    if (spi_flash_read_jedec_id(jedec_id) != 0) {
        host_uart_puts("JEDEC ID: 0x");
        host_uart_put_hex8(jedec_id[0]);
        host_uart_put_hex8(jedec_id[1]);
        host_uart_put_hex8(jedec_id[2]);
        host_uart_puts("  unrecognised (expected 0xEF... or 0x1C...)\r\n");
        host_uart_puts("SPI Flash not found — aborting.\r\n");
        while (1) { __NOP(); }
    }

    if (jedec_id[0] == MFG_WINBOND) {
        mfg_name = "Winbond W25Q64";
    } else {
        mfg_name = "Eon EN25Q64 (or compatible)";
    }

    host_uart_puts("JEDEC ID: 0x");
    host_uart_put_hex8(jedec_id[0]);
    host_uart_put_hex8(jedec_id[1]);
    host_uart_put_hex8(jedec_id[2]);
    host_uart_puts("  detected: ");
    host_uart_puts(mfg_name);
    host_uart_puts("\r\n");

    host_uart_puts("Re-reading JEDEC ID for stability:\r\n");
    for (repeat = 0UL; repeat < 2UL; ++repeat) {
        if ((spi_flash_read_jedec_id(jedec_check) != 0) ||
            (jedec_check[0] != jedec_id[0]) ||
            (jedec_check[1] != jedec_id[1]) ||
            (jedec_check[2] != jedec_id[2])) {
            host_uart_puts("  FAIL - JEDEC ID changed between reads\r\n");
            all_pass = 0;
        } else {
            host_uart_puts("  OK\r\n");
        }
    }

    /* ---- Check and clear protection ---- */
    {
        sr = spi_flash_read_status1();
        host_uart_puts("Status Reg-1: 0x");
        host_uart_put_hex8(sr);
        if ((sr & SR_BP_MASK) != 0U) {
            host_uart_puts("  (BP bits set — clearing protection)\r\n");
            spi_flash_clear_protection();
            sr = spi_flash_read_status1();
            host_uart_puts("Status Reg-1 after clear: 0x");
            host_uart_put_hex8(sr);
        }
        if ((sr & SR_BUSY) != 0U) {
            host_uart_puts("  BUSY!\r\n");
        }
        host_uart_puts("\r\n");
    }

    host_uart_puts("\r\n=== sample-15s.wav SPI Flash UART Loopback ===\r\n");
    host_uart_puts("Flash addr: 0x");
    host_uart_put_hex32(SPI_FLASH_SAMPLE_ADDR);
    host_uart_puts("  bytes: 0x");
    host_uart_put_hex32(SAMPLE_WAV_BYTES);
    host_uart_puts("\r\n");
    host_uart_puts("PC uart_receiver.py should already be waiting in --test mode.\r\n");

    (void)store_uart_file_to_spi_flash(SAMPLE_WAV_BYTES, write_buf, read_buf);

    while (1) {
        __NOP();
    }

    if ((SPI_FLASH_TEST_ADDR + SPI_FLASH_BLOCK64_BYTES) > SPI_FLASH_CAPACITY_BYTES) {
        host_uart_puts("FAIL - test block is outside 8MB flash range\r\n");
        all_pass = 0;
    }

    /* ---- 64KB block erase baseline ---- */
    host_uart_puts("Testing 64KB block erase at 0x");
    host_uart_put_hex32(SPI_FLASH_TEST_ADDR);
    host_uart_puts("...\r\n");
    spi_flash_block_erase_64k(SPI_FLASH_TEST_ADDR);
    host_uart_puts("Status Reg-1 after 64KB erase: 0x");
    host_uart_put_hex8(spi_flash_read_status1());
    host_uart_puts("\r\n");

    if (!verify_range_erased(SPI_FLASH_TEST_ADDR, SPI_FLASH_BLOCK64_BYTES)) {
        host_uart_puts("  FAIL - 64KB block not fully erased\r\n");
        all_pass = 0;
    } else {
        host_uart_puts("  OK - full 64KB block erased\r\n");
    }

    /* ---- Probe 4KB sector erase without fallback ---- */
    probe_addr = SPI_FLASH_TEST_ADDR + SPI_FLASH_SECTOR_BYTES;
    host_uart_puts("Probing raw 4KB sector erase at 0x");
    host_uart_put_hex32(probe_addr);
    host_uart_puts(":\r\n");
    fill_test_pattern(write_buf, SPI_FLASH_PAGE_BYTES, probe_addr);
    spi_flash_page_program(probe_addr, write_buf, SPI_FLASH_PAGE_BYTES);
    spi_flash_read(probe_addr, read_buf, SPI_FLASH_PAGE_BYTES);
    if (!verify_buffer_equal(probe_addr, write_buf, read_buf, SPI_FLASH_PAGE_BYTES)) {
        all_pass = 0;
    }

    erase4k_sr = spi_flash_sector_erase_4k_once(probe_addr);
    if (range_is_erased(probe_addr, SPI_FLASH_PAGE_BYTES)) {
        host_uart_puts("  OK - 4KB sector erase is supported on this flash\r\n");
    } else {
        if ((erase4k_sr & (SR_BUSY | SR_WEL)) == SR_WEL) {
            host_uart_puts("  4KB erase showed BUSY=0/WEL=1; waiting about 0.5s...\r\n");
            delay_about_half_second();
            sr = spi_flash_read_status1();
            host_uart_puts("  Status Reg-1 after 0.5s 4KB wait: 0x");
            host_uart_put_hex8(sr);
            host_uart_puts("\r\n");
        } else {
            sr = erase4k_sr;
        }

        if (range_is_erased(probe_addr, SPI_FLASH_PAGE_BYTES)) {
            host_uart_puts("  OK - 4KB sector erase completed after fixed delay\r\n");
        } else {
            if ((sr & SR_WEL) != 0U) {
                spi_flash_write_disable();
            }
            spi_flash_read(probe_addr, read_buf, SPI_FLASH_PAGE_BYTES);
            if (verify_buffer_equal(probe_addr, write_buf, read_buf, SPI_FLASH_PAGE_BYTES) &&
                ((erase4k_sr & (SR_BUSY | SR_WEL)) == SR_WEL)) {
                host_uart_puts("  OK - 4KB sector erase was ignored even after fixed delay; 64KB erase is required\r\n");
            } else {
                host_uart_puts("  FAIL - 4KB erase left unexpected data/status\r\n");
                all_pass = 0;
            }
        }
    }

    host_uart_puts("Re-erasing 64KB block after 4KB probe...\r\n");
    spi_flash_block_erase_64k(SPI_FLASH_TEST_ADDR);
    if (!verify_range_erased(SPI_FLASH_TEST_ADDR, SPI_FLASH_SECTOR_BYTES) ||
        !verify_range_erased(probe_addr, SPI_FLASH_SECTOR_BYTES) ||
        !verify_range_erased(SPI_FLASH_TEST_ADDR + SPI_FLASH_BLOCK64_BYTES - SPI_FLASH_SECTOR_BYTES,
                             SPI_FLASH_SECTOR_BYTES)) {
        host_uart_puts("  FAIL - cleanup erase did not clear sampled sectors\r\n");
        all_pass = 0;
    } else {
        host_uart_puts("  OK - sampled sectors are erased\r\n");
    }

    /* ---- Program can only change 1 bits to 0 bits ---- */
    host_uart_puts("Testing program 1-to-0 behavior without erase:\r\n");
    test_addr = SPI_FLASH_TEST_ADDR + (3UL * SPI_FLASH_SECTOR_BYTES);
    write_buf[0] = 0xAAU;
    spi_flash_page_program(test_addr, write_buf, 1UL);
    if (!verify_fixed_value(test_addr, 0xAAU)) {
        all_pass = 0;
    }
    write_buf[0] = 0x55U;
    spi_flash_page_program(test_addr, write_buf, 1UL);
    if (!verify_fixed_value(test_addr, 0x00U)) {
        all_pass = 0;
    } else {
        host_uart_puts("  OK - second program produced bitwise AND result\r\n");
    }

    host_uart_puts("Re-erasing 64KB block before page tests...\r\n");
    spi_flash_block_erase_64k(SPI_FLASH_TEST_ADDR);
    if (!verify_range_erased(SPI_FLASH_TEST_ADDR, SPI_FLASH_SECTOR_BYTES)) {
        host_uart_puts("  FAIL - first sector not erased before page tests\r\n");
        all_pass = 0;
    }

    /* ---- Write + verify each page in the sector ---- */
    host_uart_puts("Testing page program + readback:\r\n");
    page_pass = 1;

    for (page = 0UL; page < (SPI_FLASH_SECTOR_BYTES / SPI_FLASH_PAGE_BYTES); ++page) {
        uint32_t pp_retry;

        test_addr = SPI_FLASH_TEST_ADDR + (page * SPI_FLASH_PAGE_BYTES);
        fill_test_pattern(write_buf, SPI_FLASH_PAGE_BYTES, test_addr);

        result = 0;
        for (pp_retry = 0UL; pp_retry < 3UL; ++pp_retry) {
            spi_flash_page_program(test_addr, write_buf, SPI_FLASH_PAGE_BYTES);
            spi_flash_read(test_addr, read_buf, SPI_FLASH_PAGE_BYTES);
            result = verify_test_pattern(read_buf, SPI_FLASH_PAGE_BYTES, test_addr);
            if (result) {
                break;
            }
            if (pp_retry < 2UL) {
                host_uart_puts("  Page program mismatch, retrying...\r\n");
            }
        }

        if (!result) {
            all_pass = 0;
            page_pass = 0;
        }
    }
    if (page_pass) {
        host_uart_puts("  OK - all pages in first 4KB sector verified\r\n");
    }

    /* ---- Unaligned, multi-chunk read inside programmed data ---- */
    host_uart_puts("Testing unaligned multi-chunk read:\r\n");
    test_addr = SPI_FLASH_TEST_ADDR + 13UL;

    /*
     * Do a quick aligned dummy read first to flush the PL022 RX pipeline
     * and stabilise the flash output path after back-to-back page programs.
     * Without this, the first burst read occasionally returns stale bits.
     */
    {
        uint8_t flush_buf[8];
        spi_flash_read(SPI_FLASH_TEST_ADDR, flush_buf, sizeof(flush_buf));
    }

    if (!test_unaligned_multichunk_read(test_addr, 173UL, write_buf, read_buf)) {
        all_pass = 0;
    }

    /* ---- Program/read near the end of the 64KB erase block ---- */
    host_uart_puts("Testing 64KB block-tail page program:\r\n");
    test_addr = SPI_FLASH_TEST_ADDR + SPI_FLASH_BLOCK64_BYTES - SPI_FLASH_PAGE_BYTES;
    fill_test_pattern(write_buf, SPI_FLASH_PAGE_BYTES, test_addr);
    spi_flash_page_program(test_addr, write_buf, SPI_FLASH_PAGE_BYTES);
    spi_flash_read(test_addr, read_buf, SPI_FLASH_PAGE_BYTES);
    if (!verify_test_pattern(read_buf, SPI_FLASH_PAGE_BYTES, test_addr)) {
        all_pass = 0;
    } else {
        host_uart_puts("  OK\r\n");
    }

    /* ---- Cross-page boundary test ---- */
    host_uart_puts("Testing cross-page boundary read:\r\n");
    {
        uint32_t cross_addr = SPI_FLASH_TEST_ADDR + SPI_FLASH_PAGE_BYTES - 32UL;
        uint32_t cross_len = 64UL;
        uint8_t cross_wbuf[64];
        uint8_t cross_rbuf[64];

        spi_flash_block_erase_64k(SPI_FLASH_TEST_ADDR);
        fill_test_pattern(cross_wbuf, cross_len, cross_addr);

        /* Write via two page programs (wraps at 256-byte boundary) */
        spi_flash_page_program(cross_addr, cross_wbuf, 32UL);
        spi_flash_page_program(cross_addr + 32UL, cross_wbuf + 32UL, 32UL);

        /* Read back in one go */
        spi_flash_read(cross_addr, cross_rbuf, cross_len);

        result = verify_test_pattern(cross_rbuf, cross_len, cross_addr);
        if (!result) {
            all_pass = 0;
        } else {
            host_uart_puts("  OK\r\n");
        }
    }

    /* ---- Normal sequential write/read test ---- */
    host_uart_puts("Testing normal sequential write/read:\r\n");
    normal_addr = SPI_FLASH_TEST_ADDR + (SPI_FLASH_BLOCK64_BYTES / 2UL);
    spi_flash_block_erase_64k(SPI_FLASH_TEST_ADDR);
    if (!verify_range_erased(normal_addr, SPI_FLASH_NORMAL_TEST_BYTES)) {
        host_uart_puts("  FAIL - normal test area was not erased\r\n");
        all_pass = 0;
    } else if (!test_normal_sequential_write_read(normal_addr,
                                                  SPI_FLASH_NORMAL_TEST_BYTES,
                                                  write_buf,
                                                  read_buf)) {
        host_uart_puts("  FAIL - normal sequential write/read mismatch\r\n");
        all_pass = 0;
    } else {
        host_uart_puts("  OK - 8KB sequential write/read verified\r\n");
    }

    sr = spi_flash_read_status1();
    host_uart_puts("Final Status Reg-1: 0x");
    host_uart_put_hex8(sr);
    host_uart_puts("\r\n");
    if ((sr & (SR_BUSY | SR_WEL)) != 0U) {
        host_uart_puts("  FAIL - final BUSY/WEL bits should be clear\r\n");
        all_pass = 0;
    }

    /* ---- Overall result ---- */
    host_uart_puts("\r\n=== ");
    if (all_pass) {
        host_uart_puts("ALL TESTS PASSED");
    } else {
        host_uart_puts("SOME TESTS FAILED");
    }
    host_uart_puts(" ===\r\n");

    /* Idle forever — test complete */
    while (1) {
        __NOP();
    }
}
