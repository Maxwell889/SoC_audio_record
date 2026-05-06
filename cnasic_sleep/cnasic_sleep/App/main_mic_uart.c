#include <stdint.h>

#include "soc.h"

/*
 * Audio recording flow required by the final coursework:
 *
 *   1. CPU enables hardware compression and drains packed ADPCM words from FIFO.
 *   2. Software writes a standard IMA ADPCM WAV file layout.
 *   3. The complete WAV byte stream is stored in SPI Flash.
 *   4. The WAV is read back from SPI Flash and sent to the host through UART.
 *
 * The audio and SPI Flash register maps come from soc-wuxi-arm/hw/README.md.
 * SPI Flash is behind ARM PL022 SSP at APB ext12: SPI0_BASE=0x4000C000.
 * Chip select is controlled by APB ext13 because the PL022 FSS output can
 * pulse between bytes; the observed Eon-compatible flash uses 64KB erase.
 */

#define AUDIO_BASE                 (0x20010000UL)
#define AUDIO_CONTROL              (*(volatile uint32_t *)(AUDIO_BASE + 0x00UL))
#define AUDIO_STATUS               (*(volatile uint32_t *)(AUDIO_BASE + 0x20UL))
#define AUDIO_DATA                 (*(volatile uint32_t *)(AUDIO_BASE + 0x40UL))

#define AUDIO_CTRL_RX_ENABLE       (1UL << 0)
#define AUDIO_CTRL_I2S_CLEAR       (1UL << 1)
#define AUDIO_CTRL_FIFO_CLEAR      (1UL << 2)
#define AUDIO_CTRL_COMPRESS        (1UL << 3)

#define AUDIO_STATUS_FIFO_EMPTY    (1UL << 0)
#define AUDIO_STATUS_USEDW_SHIFT   (4U)
#define AUDIO_STATUS_USEDW_MASK    (0x7FFUL << AUDIO_STATUS_USEDW_SHIFT)

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

#define SPI_FLASH_CS_BASE          (0x4000D000UL)
#define SPI_FLASH_CS               (*(volatile uint32_t *)(SPI_FLASH_CS_BASE + 0x00UL))

#define W25_CMD_WRITE_ENABLE       (0x06U)
#define W25_CMD_WRITE_DISABLE      (0x04U)
#define W25_CMD_WRITE_STATUS_REG   (0x01U)
#define W25_CMD_READ_STATUS1       (0x05U)
#define W25_CMD_READ_DATA          (0x03U)
#define W25_CMD_PAGE_PROGRAM       (0x02U)
#define W25_CMD_SECTOR_ERASE_4K    (0x20U)
#define W25_CMD_BLOCK_ERASE_64K    (0xD8U)
#define W25_CMD_JEDEC_ID           (0x9FU)
#define W25_CMD_RELEASE_POWERDOWN  (0xABU)
#define W25_CMD_RESET_ENABLE       (0x66U)
#define W25_CMD_RESET_MEMORY       (0x99U)

#define W25_SR_BUSY                (1U << 0)
#define W25_SR_WEL                 (1U << 1)
#define W25_SR_BP_MASK             (0x1CU)

#define HOST_UART_BASE             (0x40005000UL)
#define HOST_UART_DATA_ADDR        (HOST_UART_BASE + 0x00UL)
#define HOST_UART_STATE_ADDR       (HOST_UART_BASE + 0x04UL)
#define HOST_UART_CTRL_ADDR        (HOST_UART_BASE + 0x08UL)
#define HOST_UART_INTCLEAR_ADDR    (HOST_UART_BASE + 0x0CUL)
#define HOST_UART_BAUDDIV_ADDR     (HOST_UART_BASE + 0x10UL)
#define HOST_UART_DATA             (*(volatile uint32_t *)(HOST_UART_BASE + 0x00UL))
#define HOST_UART_STATE            (*(volatile uint32_t *)(HOST_UART_BASE + 0x04UL))
#define HOST_UART_CTRL             (*(volatile uint32_t *)(HOST_UART_BASE + 0x08UL))
#define HOST_UART_INTCLEAR         (*(volatile uint32_t *)(HOST_UART_BASE + 0x0CUL))
#define HOST_UART_BAUDDIV          (*(volatile uint32_t *)(HOST_UART_BASE + 0x10UL))

#define HOST_UART_STATE_TX_FULL    (1UL << 0)
#define HOST_UART_STATE_RX_FULL    (1UL << 1)
#define HOST_UART_STATE_TX_OVERRUN (1UL << 2)
#define HOST_UART_STATE_RX_OVERRUN (1UL << 3)
#define HOST_UART_CTRL_TX_ENABLE   (1UL << 0)
#define HOST_UART_CTRL_RX_ENABLE   (1UL << 1)

#define WAV_SAMPLE_RATE_HZ         (15625UL)  /* 1 MHz I2S SCK / 64 bits */
#define WAV_CHANNELS               (1U)
#define WAV_BITS_PER_SAMPLE        (16U)
#define WAV_HEADER_BYTES           (44UL)
#define WAV_BYTES_PER_SAMPLE       (WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8U))

#define IMA_ADPCM_FORMAT_TAG       (0x0011U)
#define IMA_ADPCM_BITS_PER_SAMPLE  (4U)
#define ADPCM_WAV_HEADER_BYTES     (60UL)
#define ADPCM_BLOCK_HEADER_BYTES   (4UL)
#define ADPCM_BLOCK_DATA_BYTES     (4092UL)
#define ADPCM_BLOCK_ALIGN          (ADPCM_BLOCK_HEADER_BYTES + ADPCM_BLOCK_DATA_BYTES)
#define ADPCM_SAMPLES_PER_BLOCK    ((ADPCM_BLOCK_DATA_BYTES * 2UL) + 1UL)

#define RECORD_SECONDS             (25UL)
#define AUDIO_PCM_WARMUP_SAMPLES   (1536UL)

#define SPI_FLASH_WAV_ADDR         (0x00400000UL)
#define SPI_FLASH_PAGE_BYTES       (256UL)
#define SPI_FLASH_SECTOR_BYTES     (4096UL)
#define SPI_FLASH_ERASE_BYTES      (65536UL)
#define SPI_FLASH_CAPACITY_BYTES   (8UL * 1024UL * 1024UL)
#define SPI_FLASH_READ_CHUNK_BYTES (64UL)
#define SPI_FLASH_PROGRAM_CHUNK    (64UL)
#define SPI_FIFO_DEPTH             (8UL)

#define RUN_UART_SAMPLE_WAV_FLASH_TEST (0U)
#define SAMPLE_WAV_FILE_BYTES          (3382316UL)
#define SAMPLE_WAV_FILE_CRC32          (0xD7BACA31UL)

#define APB_CLOCK_HZ               (50000000UL)
#define HOST_UART_BAUD             (115200UL)

#define DONE_FLAG_ADDR             (0x00008000UL)
#define DONE_FLAG_VALUE            (0x52454344UL)  /* "RECD" */
#define UART_STUCK_FLAG_VALUE      (0x55415254UL)  /* "UART" */
#define WAV_TEST_PASS_FLAG_VALUE   (0x574F4B21UL)  /* "WOK!" */
#define WAV_TEST_FAIL_FLAG_VALUE   (0x5746414CUL)  /* "WFAL" */
#define WAV_TEST_TOO_BIG_VALUE     (0x57424947UL)  /* "WBIG" */

static volatile uint32_t * const done_flag = (volatile uint32_t *)DONE_FLAG_ADDR;
static volatile uint32_t * const done_size = (volatile uint32_t *)(DONE_FLAG_ADDR + 4UL);
static volatile uint32_t * const done_jedec_id = (volatile uint32_t *)(DONE_FLAG_ADDR + 8UL);
static volatile uint32_t * const done_uart_state = (volatile uint32_t *)(DONE_FLAG_ADDR + 12UL);
static volatile uint32_t * const done_source_crc = (volatile uint32_t *)(DONE_FLAG_ADDR + 16UL);
static volatile uint32_t * const done_flash_crc = (volatile uint32_t *)(DONE_FLAG_ADDR + 20UL);

static uint8_t flash_page_buf[SPI_FLASH_PAGE_BYTES];
static uint8_t flash_verify_buf[SPI_FLASH_PAGE_BYTES];
static uint32_t flash_page_addr;
static uint32_t flash_page_len;

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

static uint32_t wav_pcm_data_bytes(void)
{
    uint32_t requested = RECORD_SECONDS * WAV_SAMPLE_RATE_HZ * WAV_BYTES_PER_SAMPLE;
    uint32_t max_data = SPI_FLASH_CAPACITY_BYTES - SPI_FLASH_WAV_ADDR - WAV_HEADER_BYTES;

    return min_u32(requested, max_data) & ~1UL;
}

static uint32_t adpcm_block_count(void)
{
    uint32_t requested_samples = RECORD_SECONDS * WAV_SAMPLE_RATE_HZ;
    uint32_t requested_blocks;
    uint32_t max_blocks;

    requested_blocks = (requested_samples + ADPCM_SAMPLES_PER_BLOCK - 1UL) / ADPCM_SAMPLES_PER_BLOCK;
    max_blocks = (SPI_FLASH_CAPACITY_BYTES - SPI_FLASH_WAV_ADDR - ADPCM_WAV_HEADER_BYTES) / ADPCM_BLOCK_ALIGN;

    return min_u32(requested_blocks, max_blocks);
}

static void le16_to_buf(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void le32_to_buf(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value & 0xFFUL);
    buf[1] = (uint8_t)((value >> 8) & 0xFFUL);
    buf[2] = (uint8_t)((value >> 16) & 0xFFUL);
    buf[3] = (uint8_t)((value >> 24) & 0xFFUL);
}

static void copy_text(uint8_t *buf, const char *text, uint32_t count)
{
    uint32_t i;

    for (i = 0UL; i < count; ++i) {
        buf[i] = (uint8_t)text[i];
    }
}

static void wav_make_pcm_header(uint8_t header[WAV_HEADER_BYTES], uint32_t pcm_data_bytes)
{
    uint32_t byte_rate = WAV_SAMPLE_RATE_HZ * WAV_BYTES_PER_SAMPLE;
    uint16_t block_align = (uint16_t)WAV_BYTES_PER_SAMPLE;

    copy_text(&header[0], "RIFF", 4UL);
    le32_to_buf(&header[4], 36UL + pcm_data_bytes);
    copy_text(&header[8], "WAVE", 4UL);

    copy_text(&header[12], "fmt ", 4UL);
    le32_to_buf(&header[16], 16UL);
    le16_to_buf(&header[20], 1U);
    le16_to_buf(&header[22], WAV_CHANNELS);
    le32_to_buf(&header[24], WAV_SAMPLE_RATE_HZ);
    le32_to_buf(&header[28], byte_rate);
    le16_to_buf(&header[32], block_align);
    le16_to_buf(&header[34], WAV_BITS_PER_SAMPLE);

    copy_text(&header[36], "data", 4UL);
    le32_to_buf(&header[40], pcm_data_bytes);
}

static void wav_make_adpcm_header(uint8_t header[ADPCM_WAV_HEADER_BYTES], uint32_t block_count)
{
    uint32_t data_bytes = block_count * ADPCM_BLOCK_ALIGN;
    uint32_t sample_count = block_count * ADPCM_SAMPLES_PER_BLOCK;
    uint32_t avg_bytes_per_sec;

    avg_bytes_per_sec = (WAV_SAMPLE_RATE_HZ * ADPCM_BLOCK_ALIGN) / ADPCM_SAMPLES_PER_BLOCK;

    copy_text(&header[0], "RIFF", 4UL);
    le32_to_buf(&header[4], 52UL + data_bytes);
    copy_text(&header[8], "WAVE", 4UL);

    copy_text(&header[12], "fmt ", 4UL);
    le32_to_buf(&header[16], 20UL);
    le16_to_buf(&header[20], IMA_ADPCM_FORMAT_TAG);
    le16_to_buf(&header[22], WAV_CHANNELS);
    le32_to_buf(&header[24], WAV_SAMPLE_RATE_HZ);
    le32_to_buf(&header[28], avg_bytes_per_sec);
    le16_to_buf(&header[32], (uint16_t)ADPCM_BLOCK_ALIGN);
    le16_to_buf(&header[34], IMA_ADPCM_BITS_PER_SAMPLE);
    le16_to_buf(&header[36], 2U);
    le16_to_buf(&header[38], (uint16_t)ADPCM_SAMPLES_PER_BLOCK);

    copy_text(&header[40], "fact", 4UL);
    le32_to_buf(&header[44], 4UL);
    le32_to_buf(&header[48], sample_count);

    copy_text(&header[52], "data", 4UL);
    le32_to_buf(&header[56], data_bytes);
}

static void audio_clear_fifos(uint32_t mode_bits)
{
    AUDIO_CONTROL = 0UL;
    AUDIO_CONTROL = mode_bits | AUDIO_CTRL_I2S_CLEAR | AUDIO_CTRL_FIFO_CLEAR;
    AUDIO_CONTROL = mode_bits;
}

static void audio_start_pcm(void)
{
    audio_clear_fifos(0UL);
    AUDIO_CONTROL = AUDIO_CTRL_RX_ENABLE;
}

static void audio_start_adpcm(void)
{
    audio_clear_fifos(AUDIO_CTRL_COMPRESS);
    AUDIO_CONTROL = AUDIO_CTRL_COMPRESS | AUDIO_CTRL_RX_ENABLE;
}

static void audio_stop(void)
{
    AUDIO_CONTROL = 0UL;
}

static uint16_t audio_read_pcm_sample_blocking(void)
{
    while ((AUDIO_STATUS & AUDIO_STATUS_FIFO_EMPTY) != 0UL) {
        /* Wait until the PCM FIFO has at least one 16-bit sample. */
    }

    return (uint16_t)(AUDIO_DATA & 0xFFFFUL);
}

static uint32_t audio_read_adpcm_word_blocking(void)
{
    while ((AUDIO_STATUS & AUDIO_STATUS_FIFO_EMPTY) != 0UL) {
        /* Wait until the ADPCM FIFO has one packed 32-bit word. */
    }

    return AUDIO_DATA;
}

static uint32_t audio_fifo_usedw(void)
{
    return (AUDIO_STATUS & AUDIO_STATUS_USEDW_MASK) >> AUDIO_STATUS_USEDW_SHIFT;
}

static void audio_drain_pcm_samples(uint32_t count)
{
    uint32_t i;

    for (i = 0UL; i < count; ++i) {
        (void)audio_read_pcm_sample_blocking();
    }
}

static void host_uart_init(void)
{
    uint32_t bauddiv;

    bauddiv = (APB_CLOCK_HZ + (HOST_UART_BAUD / 2UL)) / HOST_UART_BAUD;
    if (bauddiv < 16UL) {
        bauddiv = 16UL;
    }

    mmio_write32(HOST_UART_CTRL_ADDR, 0UL);
    mmio_write32(HOST_UART_BAUDDIV_ADDR, bauddiv);
    mmio_write32(HOST_UART_STATE_ADDR, HOST_UART_STATE_TX_OVERRUN | HOST_UART_STATE_RX_OVERRUN);
    mmio_write32(HOST_UART_INTCLEAR_ADDR, 0x0FUL);
    mmio_write32(HOST_UART_CTRL_ADDR, HOST_UART_CTRL_TX_ENABLE | HOST_UART_CTRL_RX_ENABLE);
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

        if (timeout == 0UL) {
            *done_flag = UART_STUCK_FLAG_VALUE;
            *done_uart_state = state;
            return;
        }

        timeout--;
    } while ((state & HOST_UART_STATE_TX_FULL) != 0UL);

    mmio_write32(HOST_UART_DATA_ADDR, value);
    delay_cycles(APB_CLOCK_HZ / HOST_UART_BAUD);
}

static uint8_t host_uart_getc(void)
{
    uint32_t state;

    do {
        state = mmio_read32(HOST_UART_STATE_ADDR);

        if ((state & HOST_UART_STATE_RX_OVERRUN) != 0UL) {
            *done_flag = UART_STUCK_FLAG_VALUE;
            *done_uart_state = state;
            mmio_write32(HOST_UART_STATE_ADDR, HOST_UART_STATE_RX_OVERRUN);
        }
    } while ((state & HOST_UART_STATE_RX_FULL) == 0UL);

    return (uint8_t)(mmio_read32(HOST_UART_DATA_ADDR) & 0xFFUL);
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

static void host_send_u16_le(uint16_t value)
{
    host_uart_putc((uint8_t)(value & 0xFFU));
    host_uart_putc((uint8_t)((value >> 8) & 0xFFU));
}

static void host_send_frame_header(uint32_t wav_bytes)
{
    host_send_tag4("WAV0");
    host_send_u32_le(wav_bytes);
}

static void host_send_buf(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        host_uart_putc(data[i]);
    }
}

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
    SPI_CPSR = 10UL;
    SPI_CR0 = (7UL << 0)    /* DSS = 8-bit */
            | (0UL << 4)    /* Motorola SPI frame */
            | (0UL << 6)    /* SPO = 0: SPI mode 0 */
            | (0UL << 7)    /* SPH = 0 */
            | (4UL << 8);   /* 50 MHz / (10 * (1 + 4)) = 1 MHz */
    SPI_IMSC = 0x0000UL;
    SPI_ICR = 0x0003UL;
    SPI_CR1 = (1UL << 1);   /* SSE=1, master mode */

    /*
     * Mode 0: SCK idles LOW immediately after SSE is asserted.
     * Avoid dummy transfers while CS is high.
     */
    delay_cycles(100UL);
    spi0_drain_rx();
}

static uint8_t w25_read_status1(void)
{
    uint8_t tx[2];
    uint8_t rx[2];
    uint8_t sr;

    tx[0] = W25_CMD_READ_STATUS1;
    tx[1] = 0xFFU;
    spi0_xfer_buf(tx, rx, sizeof(tx));
    sr = rx[1];

    return sr;
}

static void w25_wait_ready(void)
{
    while ((w25_read_status1() & W25_SR_BUSY) != 0U) {
        /* Wait until erase/program finishes. */
    }
}

static uint8_t w25_write_enable(void)
{
    uint8_t tx[1];
    uint32_t retry;
    uint8_t sr = 0U;

    for (retry = 0UL; retry < 8UL; ++retry) {
        tx[0] = W25_CMD_WRITE_ENABLE;
        spi0_xfer_buf(tx, 0, sizeof(tx));
        delay_cycles(500UL);

        sr = w25_read_status1();
        if ((sr & W25_SR_WEL) != 0U) {
            return sr;
        }
    }

    return sr;
}

static void w25_send_cmd(uint8_t cmd)
{
    uint8_t tx[1];

    tx[0] = cmd;
    spi0_xfer_buf(tx, 0, sizeof(tx));
}

static void w25_write_disable(void)
{
    w25_send_cmd(W25_CMD_WRITE_DISABLE);
}

static void w25_reset_device(void)
{
    w25_send_cmd(W25_CMD_RESET_ENABLE);
    delay_cycles(500UL);
    w25_send_cmd(W25_CMD_RESET_MEMORY);
    w25_wait_ready();
}

static void w25_release_powerdown(void)
{
    w25_send_cmd(W25_CMD_RELEASE_POWERDOWN);
    w25_wait_ready();
}

static void w25_clear_protection(void)
{
    uint8_t tx[2];

    if ((w25_read_status1() & W25_SR_BP_MASK) == 0U) {
        return;
    }

    w25_write_enable();

    tx[0] = W25_CMD_WRITE_STATUS_REG;
    tx[1] = 0x00U;
    spi0_xfer_buf(tx, 0, sizeof(tx));
    w25_wait_ready();
    w25_write_disable();
}

static void w25_read_jedec_id(uint8_t id[3])
{
    uint8_t tx[4];
    uint8_t rx[4];

    tx[0] = W25_CMD_JEDEC_ID;
    tx[1] = 0xFFU;
    tx[2] = 0xFFU;
    tx[3] = 0xFFU;
    spi0_xfer_buf(tx, rx, sizeof(tx));

    id[0] = rx[1];
    id[1] = rx[2];
    id[2] = rx[3];
}

static void spi_flash_erase_range(uint32_t addr, uint32_t len)
{
    uint8_t tx[4];
    uint8_t sr;
    uint32_t block;
    uint32_t end;

    block = addr & ~(SPI_FLASH_ERASE_BYTES - 1UL);
    end = (addr + len + SPI_FLASH_ERASE_BYTES - 1UL) & ~(SPI_FLASH_ERASE_BYTES - 1UL);

    while (block < end) {
        /*
         * Keep the same board-proven sequence used by main_flash.c:
         * this Eon-compatible flash ignores 4KB erase but leaves WEL set;
         * WRDI/WREN then 64KB erase is the reliable path on the board.
         */
        w25_write_enable();

        tx[0] = W25_CMD_SECTOR_ERASE_4K;
        tx[1] = (uint8_t)((block >> 16) & 0xFFUL);
        tx[2] = (uint8_t)((block >> 8) & 0xFFUL);
        tx[3] = (uint8_t)(block & 0xFFUL);
        spi0_xfer_buf(tx, 0, sizeof(tx));

        sr = w25_read_status1();
        if ((sr & (W25_SR_BUSY | W25_SR_WEL)) == W25_SR_WEL) {
            delay_cycles(5000UL);
            w25_write_disable();
            delay_cycles(5000UL);
            w25_write_enable();
            delay_cycles(5000UL);

            tx[0] = W25_CMD_BLOCK_ERASE_64K;
            spi0_xfer_buf(tx, 0, sizeof(tx));
        }

        /*
         * Status polling can return before the slow block erase is really
         * usable on this board path. Wait conservatively before page writes
         * so early WAV/header pages are not ignored.
         */
        delay_cycles(APB_CLOCK_HZ / 2UL);
        w25_wait_ready();

        block += SPI_FLASH_ERASE_BYTES;
    }
}

static void spi_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint8_t tx[4U + SPI_FLASH_PROGRAM_CHUNK];
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PROGRAM_CHUNK, len - offset);

        (void)w25_write_enable();

        tx[0] = W25_CMD_PAGE_PROGRAM;
        tx[1] = (uint8_t)(((addr + offset) >> 16) & 0xFFUL);
        tx[2] = (uint8_t)(((addr + offset) >> 8) & 0xFFUL);
        tx[3] = (uint8_t)((addr + offset) & 0xFFUL);

        for (i = 0UL; i < chunk; ++i) {
            tx[4UL + i] = data[offset + i];
        }

        spi0_xfer_buf(tx, 0, 4UL + chunk);
        w25_wait_ready();
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

        tx[0] = W25_CMD_READ_DATA;
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

static void flash_stream_begin(uint32_t addr)
{
    flash_page_addr = addr;
    flash_page_len = 0UL;
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

static void flash_stream_flush(void)
{
    uint32_t retry;

    if (flash_page_len != 0UL) {
        for (retry = 0UL; retry < 3UL; ++retry) {
            spi_flash_page_program(flash_page_addr, flash_page_buf, flash_page_len);
            spi_flash_read(flash_page_addr, flash_verify_buf, flash_page_len);
            if (buffer_equal_quiet(flash_page_buf, flash_verify_buf, flash_page_len)) {
                break;
            }
            delay_cycles(5000UL);
        }
        flash_page_addr += flash_page_len;
        flash_page_len = 0UL;
    }
}

static void flash_stream_write_byte(uint8_t value)
{
    flash_page_buf[flash_page_len] = value;
    flash_page_len++;

    if (flash_page_len == SPI_FLASH_PAGE_BYTES) {
        flash_stream_flush();
    }
}

static void flash_stream_write(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        flash_stream_write_byte(data[i]);
    }
}

static void flash_stream_write_le16(uint16_t value)
{
    flash_stream_write_byte((uint8_t)(value & 0xFFU));
    flash_stream_write_byte((uint8_t)((value >> 8) & 0xFFU));
}

static void flash_stream_write_le32(uint32_t value)
{
    flash_stream_write_byte((uint8_t)(value & 0xFFUL));
    flash_stream_write_byte((uint8_t)((value >> 8) & 0xFFUL));
    flash_stream_write_byte((uint8_t)((value >> 16) & 0xFFUL));
    flash_stream_write_byte((uint8_t)((value >> 24) & 0xFFUL));
}

static uint32_t record_wav_to_spi_flash(void)
{
    uint8_t header[WAV_HEADER_BYTES];
    uint32_t pcm_bytes;
    uint32_t bytes_written;
    uint16_t sample;
    uint32_t erase_bytes;

    pcm_bytes = wav_pcm_data_bytes();
    wav_make_pcm_header(header, pcm_bytes);

    erase_bytes = WAV_HEADER_BYTES + pcm_bytes;
    erase_bytes = (erase_bytes + SPI_FLASH_ERASE_BYTES - 1UL) & ~(SPI_FLASH_ERASE_BYTES - 1UL);

    spi_flash_erase_range(SPI_FLASH_WAV_ADDR, erase_bytes);
    flash_stream_begin(SPI_FLASH_WAV_ADDR);
    flash_stream_write(header, WAV_HEADER_BYTES);

    bytes_written = 0UL;
    audio_start_pcm();
    audio_drain_pcm_samples(AUDIO_PCM_WARMUP_SAMPLES);

    while (bytes_written < pcm_bytes) {
        sample = audio_read_pcm_sample_blocking();
        flash_stream_write_le16(sample);
        bytes_written += 2UL;
        (void)audio_fifo_usedw();
    }

    audio_stop();
    flash_stream_flush();

    return WAV_HEADER_BYTES + bytes_written;
}

static void record_one_adpcm_block_to_flash(void)
{
    uint32_t i;
    uint32_t word;

    /*
     * IMA ADPCM WAV stores predictor/index before each compressed block.
     * The hardware encoder is cleared per block, so predictor=0,index=0 here
     * matches the hardware state used to encode the following ADPCM bytes.
     * Do not discard ADPCM words after this header unless the header is also
     * updated to the encoder state after the discarded samples.
     */
    flash_stream_write_le16(0U);
    flash_stream_write_byte(0U);
    flash_stream_write_byte(0U);

    audio_start_adpcm();

    for (i = 0UL; i < (ADPCM_BLOCK_DATA_BYTES / 4UL); ++i) {
        word = audio_read_adpcm_word_blocking();
        flash_stream_write_le32(word);
        (void)audio_fifo_usedw();
    }

    audio_stop();
}

static uint32_t record_compressed_wav_to_spi_flash(void)
{
    uint8_t header[ADPCM_WAV_HEADER_BYTES];
    uint32_t blocks;
    uint32_t block;
    uint32_t wav_bytes;
    uint32_t erase_bytes;

    blocks = adpcm_block_count();
    wav_make_adpcm_header(header, blocks);

    wav_bytes = ADPCM_WAV_HEADER_BYTES + (blocks * ADPCM_BLOCK_ALIGN);
    erase_bytes = (wav_bytes + SPI_FLASH_ERASE_BYTES - 1UL) & ~(SPI_FLASH_ERASE_BYTES - 1UL);

    spi_flash_erase_range(SPI_FLASH_WAV_ADDR, erase_bytes);
    flash_stream_begin(SPI_FLASH_WAV_ADDR);
    flash_stream_write(header, ADPCM_WAV_HEADER_BYTES);

    for (block = 0UL; block < blocks; ++block) {
        record_one_adpcm_block_to_flash();
    }

    flash_stream_flush();

    return wav_bytes;
}

static void send_one_adpcm_block_to_uart(void)
{
    uint32_t i;
    uint32_t word;

    /*
     * Keep the WAV block header aligned with the hardware encoder reset:
     * predictor=0, index=0, reserved=0, then the compressed nibbles.
     */
    host_send_u16_le(0U);
    host_uart_putc(0U);
    host_uart_putc(0U);

    audio_start_adpcm();

    for (i = 0UL; i < (ADPCM_BLOCK_DATA_BYTES / 4UL); ++i) {
        word = audio_read_adpcm_word_blocking();
        host_send_u32_le(word);
        (void)audio_fifo_usedw();
    }

    audio_stop();
}

static void send_adpcm_wav_header_to_uart(uint32_t block_count)
{
    uint32_t data_bytes;
    uint32_t sample_count;
    uint32_t avg_bytes_per_sec;

    data_bytes = block_count * ADPCM_BLOCK_ALIGN;
    sample_count = block_count * ADPCM_SAMPLES_PER_BLOCK;
    avg_bytes_per_sec = (WAV_SAMPLE_RATE_HZ * ADPCM_BLOCK_ALIGN) / ADPCM_SAMPLES_PER_BLOCK;

    host_send_buf((const uint8_t *)"RIFF", 4UL);
    host_send_u32_le(52UL + data_bytes);
    host_send_buf((const uint8_t *)"WAVE", 4UL);

    host_send_buf((const uint8_t *)"fmt ", 4UL);
    host_send_u32_le(20UL);
    host_send_u16_le(IMA_ADPCM_FORMAT_TAG);
    host_send_u16_le(WAV_CHANNELS);
    host_send_u32_le(WAV_SAMPLE_RATE_HZ);
    host_send_u32_le(avg_bytes_per_sec);
    host_send_u16_le((uint16_t)ADPCM_BLOCK_ALIGN);
    host_send_u16_le(IMA_ADPCM_BITS_PER_SAMPLE);
    host_send_u16_le(2U);
    host_send_u16_le((uint16_t)ADPCM_SAMPLES_PER_BLOCK);

    host_send_buf((const uint8_t *)"fact", 4UL);
    host_send_u32_le(4UL);
    host_send_u32_le(sample_count);

    host_send_buf((const uint8_t *)"data", 4UL);
    host_send_u32_le(data_bytes);
}

static uint32_t record_compressed_wav_to_uart(void)
{
    uint32_t blocks;
    uint32_t block;
    uint32_t wav_bytes;

    blocks = adpcm_block_count();
    wav_bytes = ADPCM_WAV_HEADER_BYTES + (blocks * ADPCM_BLOCK_ALIGN);

    host_send_tag4("REC0");
    host_send_frame_header(wav_bytes);
    send_adpcm_wav_header_to_uart(blocks);

    for (block = 0UL; block < blocks; ++block) {
        send_one_adpcm_block_to_uart();
    }

    return wav_bytes;
}

static void output_wav_from_spi_flash(uint32_t flash_addr, uint32_t wav_bytes)
{
    uint8_t buf[64];
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t i;

    host_send_frame_header(wav_bytes);

    while (offset < wav_bytes) {
        chunk = min_u32(sizeof(buf), wav_bytes - offset);
        spi_flash_read(flash_addr + offset, buf, chunk);

        for (i = 0UL; i < chunk; ++i) {
            host_uart_putc(buf[i]);
        }

        offset += chunk;
    }
}

static int spi_flash_read_wav_probe(uint32_t flash_addr, uint8_t header[12])
{
    spi_flash_read(flash_addr, header, 12UL);

    if ((header[0] != 'R') || (header[1] != 'I') ||
        (header[2] != 'F') || (header[3] != 'F')) {
        return 0;
    }

    if ((header[8] != 'W') || (header[9] != 'A') ||
        (header[10] != 'V') || (header[11] != 'E')) {
        return 0;
    }

    return 1;
}

static void host_send_flash_verify_probe(uint32_t status, const uint8_t header[12])
{
    uint32_t i;

    host_send_tag4("VFY0");
    host_send_u32_le(status);
    for (i = 0UL; i < 12UL; ++i) {
        host_uart_putc(header[i]);
    }
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

static void host_send_wav_flash_test_start(uint32_t wav_bytes)
{
    host_send_tag4("WVT0");
    host_send_u32_le(wav_bytes);
    host_send_u32_le(SAMPLE_WAV_FILE_CRC32);
    host_send_u32_le(SPI_FLASH_WAV_ADDR);
    host_send_u32_le(SPI_FLASH_PAGE_BYTES);
}

static void host_send_wav_chunk_request(uint32_t offset, uint32_t chunk)
{
    host_send_tag4("WVRQ");
    host_send_u32_le(offset);
    host_send_u32_le(chunk);
}

static void host_send_wav_compare_result(uint32_t wav_bytes,
                                         uint32_t source_crc,
                                         uint32_t flash_crc,
                                         uint32_t status)
{
    host_send_tag4("WVRS");
    host_send_u32_le(status);
    host_send_u32_le(wav_bytes);
    host_send_u32_le(source_crc);
    host_send_u32_le(flash_crc);
}

static uint32_t receive_uart_chunk_crc(uint8_t *buf, uint32_t len, uint32_t crc)
{
    uint32_t i;

    for (i = 0UL; i < len; ++i) {
        buf[i] = host_uart_getc();
        crc = crc32_update_byte(crc, buf[i]);
    }

    return crc;
}

static uint32_t crc32_spi_flash(uint32_t flash_addr, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset = 0UL;
    uint32_t chunk;

    while (offset < len) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, len - offset);
        spi_flash_read(flash_addr + offset, flash_page_buf, chunk);
        crc = crc32_update_buf(crc, flash_page_buf, chunk);
        offset += chunk;
    }

    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t store_uart_sample_wav_to_spi_flash_and_compare(uint32_t wav_bytes)
{
    uint32_t source_crc = 0xFFFFFFFFUL;
    uint32_t flash_crc;
    uint32_t offset = 0UL;
    uint32_t chunk;
    uint32_t erase_bytes;
    uint32_t status;

    *done_size = wav_bytes;
    *done_source_crc = 0UL;
    *done_flash_crc = 0UL;

    host_send_wav_flash_test_start(wav_bytes);

    if ((wav_bytes > SPI_FLASH_CAPACITY_BYTES) ||
        (SPI_FLASH_WAV_ADDR > (SPI_FLASH_CAPACITY_BYTES - wav_bytes))) {
        *done_flag = WAV_TEST_TOO_BIG_VALUE;
        host_send_wav_compare_result(wav_bytes, 0UL, 0UL, WAV_TEST_TOO_BIG_VALUE);
        return 0UL;
    }

    erase_bytes = (wav_bytes + SPI_FLASH_ERASE_BYTES - 1UL) & ~(SPI_FLASH_ERASE_BYTES - 1UL);
    spi_flash_erase_range(SPI_FLASH_WAV_ADDR, erase_bytes);

    /*
     * Drain UART RX FIFO — SPI flash erase activity can couple noise
     * onto the UART RX pin, creating phantom bytes.
     */
    host_uart_drain_rx();

    while (offset < wav_bytes) {
        chunk = min_u32(SPI_FLASH_PAGE_BYTES, wav_bytes - offset);

        /*
         * Host protocol: wait for "WVRQ" + offset + chunk, then send exactly
         * chunk bytes from sample-15s.wav. The next request is the ACK.
         */
        /* Drain any noise accumulated during flash operations */
        host_uart_drain_rx();
        host_send_wav_chunk_request(offset, chunk);
        source_crc = receive_uart_chunk_crc(flash_page_buf, chunk, source_crc);
        spi_flash_page_program(SPI_FLASH_WAV_ADDR + offset, flash_page_buf, chunk);
        offset += chunk;
    }

    source_crc ^= 0xFFFFFFFFUL;
    flash_crc = crc32_spi_flash(SPI_FLASH_WAV_ADDR, wav_bytes);
    if ((source_crc == SAMPLE_WAV_FILE_CRC32) &&
        (source_crc == flash_crc) &&
        (*done_uart_state == 0UL)) {
        status = WAV_TEST_PASS_FLAG_VALUE;
    } else {
        status = WAV_TEST_FAIL_FLAG_VALUE;
    }

    *done_source_crc = source_crc;
    *done_flash_crc = flash_crc;
    *done_flag = status;
    host_send_wav_compare_result(wav_bytes, source_crc, flash_crc, status);

    return wav_bytes;
}

int main(void)
{
#if (RUN_UART_SAMPLE_WAV_FLASH_TEST == 0U)
    uint32_t wav_bytes;
#endif

    *done_flag = 0UL;
    *done_size = 0UL;
    *done_jedec_id = 0UL;
    *done_uart_state = 0UL;
    *done_source_crc = 0UL;
    *done_flash_crc = 0UL;

    host_uart_init();

    /* Send a short sync tag; the PC receiver slides until it sees WAV0. */
    host_send_tag4("START");

#if RUN_UART_SAMPLE_WAV_FLASH_TEST
    {
    uint8_t jedec_id[3];

    spi_flash_init();
    w25_reset_device();
    w25_release_powerdown();
    w25_clear_protection();
    w25_read_jedec_id(jedec_id);
    *done_jedec_id = ((uint32_t)jedec_id[0] << 16)
                   | ((uint32_t)jedec_id[1] << 8)
                   | ((uint32_t)jedec_id[2]);
    (void)store_uart_sample_wav_to_spi_flash_and_compare(SAMPLE_WAV_FILE_BYTES);
    }
#else
    wav_bytes = record_compressed_wav_to_uart();
    *done_size = wav_bytes;
    *done_flag = DONE_FLAG_VALUE;
#endif

    while (1) {
        /* Recording mode sends "WAV0" + ADPCM WAV bytes directly over UART. */
    }
}
