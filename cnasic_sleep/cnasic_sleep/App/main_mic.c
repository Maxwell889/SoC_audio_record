#include <stdint.h>

#include "soc.h"

/*
 * Microphone (INMP441) test for FPGA board.
 *
 * Tests:
 *   1. Audio register access via AHB (remapped addresses)
 *   2. PCM mode: start I2S, read & print 20 samples
 *   3. ADPCM mode: read & print 5 packed words
 *
 * Build: mic-board-test-v1
 *
 * Hardware workaround (2026-05-04):
 *   HADDR[4:2] are stuck-at-0 on FPGA.  audio_ahb_if.v has been remapped:
 *     CONTROL: word_addr=0x00, C offset +0x00 (unchanged)
 *     STATUS:  word_addr=0x08, C offset +0x20 (was +0x04)
 *     DATA:    word_addr=0x10, C offset +0x40 (was +0x08)
 *
 *   This uses HADDR[7:5] for decode instead of HADDR[4:2].
 *   Software should use these new offsets.
 */

/* ---------- Audio (remapped offsets for HADDR[4:2] stuck-at-0) ---------- */
#define AUDIO_CONTROL   (*(volatile uint32_t *)0x20010000UL)
#define AUDIO_STATUS    (*(volatile uint32_t *)0x20010020UL)
#define AUDIO_DATA      (*(volatile uint32_t *)0x20010040UL)

#define CTRL_RX_ENABLE  (1UL << 0)
#define CTRL_I2S_CLEAR  (1UL << 1)
#define CTRL_FIFO_CLEAR (1UL << 2)
#define CTRL_COMPRESS   (1UL << 3)

#define STAT_FIFO_EMPTY (1UL << 0)
#define STAT_RX_ENABLE  (1UL << 3)
#define STAT_USEDW_SHF  4U
#define STAT_USEDW_MSK  (0x7FFUL << STAT_USEDW_SHF)
#define STAT_COMPRESS   (1UL << 15)

#define PCM_SAMPLE_COUNT 256U

/* ---------- UART ---------- */
#define UART_BASE       (0x40005000UL)
#define UART_DATA       (*(volatile uint32_t *)(UART_BASE + 0x00UL))
#define UART_STATE      (*(volatile uint32_t *)(UART_BASE + 0x04UL))
#define UART_CTRL       (*(volatile uint32_t *)(UART_BASE + 0x08UL))
#define UART_BAUDDIV    (*(volatile uint32_t *)(UART_BASE + 0x10UL))

#define UART_TX_FULL    (1UL << 0)
#define UART_RX_FULL    (1UL << 1)
#define UART_TX_OVERRUN (1UL << 2)
#define UART_RX_OVERRUN (1UL << 3)
#define UART_TX_EN      (1UL << 0)
#define UART_RX_EN      (1UL << 1)

#define CLK_HZ          50000000UL
#define BAUD            115200UL

/* ---------- helpers ---------- */
static void delay(volatile uint32_t n)
{
    while (n) n--;
}

static void uart_init(void)
{
    uint32_t div = (CLK_HZ + BAUD / 2) / BAUD;
    if (div < 16) div = 16;
    UART_CTRL = 0;
    UART_BAUDDIV = div;
    UART_STATE = UART_TX_OVERRUN | UART_RX_OVERRUN;
    UART_CTRL = UART_TX_EN | UART_RX_EN;
}

static void uart_putc(uint8_t c)
{
    uint32_t t = 5000000;
    do {
        uint32_t s = UART_STATE;
        if (s & UART_TX_OVERRUN) UART_STATE = UART_TX_OVERRUN;
        t--;
    } while ((UART_STATE & UART_TX_FULL) && t);
    UART_DATA = c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc((uint8_t)*s++);
}

static void uart_hex8(uint8_t v)
{
    static const char h[] = "0123456789ABCDEF";
    uart_putc((uint8_t)h[v >> 4]);
    uart_putc((uint8_t)h[v & 0x0F]);
}

static void uart_hex32(uint32_t v)
{
    uart_hex8((uint8_t)(v >> 24));
    uart_hex8((uint8_t)(v >> 16));
    uart_hex8((uint8_t)(v >> 8));
    uart_hex8((uint8_t)(v));
}

static void uart_dec(uint32_t v)
{
    char b[12]; uint32_t p = sizeof(b);
    if (!v) { uart_putc('0'); return; }
    while (v && p) { p--; b[p] = (char)('0' + v % 10); v /= 10; }
    while (p < sizeof(b)) uart_putc((uint8_t)b[p++]);
}

/* ---------- Audio ---------- */
static void audio_start_pcm(void)
{
    AUDIO_CONTROL = 0;
    AUDIO_CONTROL = CTRL_I2S_CLEAR | CTRL_FIFO_CLEAR;
    AUDIO_CONTROL = 0;
    AUDIO_CONTROL = CTRL_RX_ENABLE;
}

static void audio_start_adpcm(void)
{
    AUDIO_CONTROL = 0;
    AUDIO_CONTROL = CTRL_I2S_CLEAR | CTRL_FIFO_CLEAR | CTRL_COMPRESS;
    AUDIO_CONTROL = 0;
    AUDIO_CONTROL = CTRL_RX_ENABLE | CTRL_COMPRESS;
}

static void audio_stop(void)
{
    AUDIO_CONTROL = 0;
}

/*
 * Poll PCM FIFO using STATUS register (should work with remapped address).
 * Falls back to timeout-based approach if STATUS seems broken.
 */
static int audio_read_pcm(uint16_t *s)
{
    uint32_t t = CLK_HZ;  /* 1s timeout */
    uint32_t st;

    /* Try STATUS-based polling first */
    do {
        st = AUDIO_STATUS;
        t--;
    } while ((st & STAT_FIFO_EMPTY) && t);

    *s = (uint16_t)AUDIO_DATA;

    if (!t) {
        /* STATUS might still be broken. Return 0 but let caller decide. */
        return 0;
    }
    return 1;
}

static int audio_read_adpcm(uint32_t *w)
{
    uint32_t t = CLK_HZ;
    uint32_t st;

    do {
        st = AUDIO_STATUS;
        t--;
    } while ((st & STAT_FIFO_EMPTY) && t);

    *w = AUDIO_DATA;

    if (!t) return 0;
    return 1;
}

static uint32_t audio_usedw(void)
{
    return (AUDIO_STATUS & STAT_USEDW_MSK) >> STAT_USEDW_SHF;
}

static void audio_dump_status(void)
{
    uint32_t st = AUDIO_STATUS;
    uart_puts("STATUS=0x"); uart_hex32(st);
    uart_puts(" empty="); uart_putc((st & STAT_FIFO_EMPTY) ? '1' : '0');
    uart_puts(" rx_en="); uart_putc((st & STAT_RX_ENABLE) ? '1' : '0');
    uart_puts(" comp="); uart_putc((st & STAT_COMPRESS) ? '1' : '0');
    uart_puts(" usedw="); uart_dec(audio_usedw());
    uart_puts("\r\n");
}

/* ---------- main ---------- */
int main(void)
{
    volatile uint32_t *spi_cs     = (volatile uint32_t *)0x4000D000UL;
    volatile uint32_t *audio_ctrl = (volatile uint32_t *)0x20010000UL;
    volatile uint32_t *audio_stat = (volatile uint32_t *)0x20010020UL;
    uint32_t v;
    uint32_t ctrl_after;
    uint32_t stat_after;
    unsigned int i;
    uint16_t s;
    uint16_t samples[PCM_SAMPLE_COUNT];
    int16_t ss;
    uint32_t w;
    uint32_t min_val;
    uint32_t max_val;
    uint32_t zero_cnt;
    uint32_t pcm_ok;
    uint32_t direct_reads;
    uint32_t scan_addr;
    uint32_t scan_val;
    uint32_t discard_count;
    uint32_t discard_nonzero;
    uint32_t discard_nonzero_words;

    uart_init();
    uart_puts("\r\n=== Mic Test (board) - v4 (remapped addrs) ===\r\n");

    /* ---- 0. Bus sanity check ---- */
    uart_puts("--- 0. Bus check ---\r\n");

    v = *spi_cs;
    uart_puts("SPI_CS(0x4000D000)="); uart_hex32(v); uart_puts("\r\n");

    v = *audio_ctrl;
    uart_puts("CTRL(0x20010000)="); uart_hex32(v); uart_puts("\r\n");
    v = *audio_stat;
    uart_puts("STAT(0x20010020)="); uart_hex32(v);
    if (v == 0) {
        uart_puts(" (cold 0)\r\n");
    } else {
        uart_puts("\r\n");
    }

    /* Write rx_enable, read back both CONTROL and STATUS */
    AUDIO_CONTROL = CTRL_I2S_CLEAR | CTRL_FIFO_CLEAR;
    AUDIO_CONTROL = 0;
    AUDIO_CONTROL = CTRL_RX_ENABLE;
    delay(1000);

    ctrl_after = *audio_ctrl;
    uart_puts("CTRL after RX_ENABLE="); uart_hex32(ctrl_after); uart_puts("\r\n");
    stat_after = *audio_stat;
    uart_puts("STAT after RX_ENABLE="); uart_hex32(stat_after); uart_puts("\r\n");

    if ((ctrl_after & CTRL_RX_ENABLE) == 0UL) {
        uart_puts("\r\nCONTROL writeback failed. AHB path broken.\r\n");
        while (1) __NOP();
    }

    /*
     * With remapped addresses, STATUS should NOT mirror CONTROL.
     * Check: if STAT == CTRL, the remap didn't take effect (still aliased).
     * If STAT has fifo_empty=1 and proper bits, remap works!
     */
    if (stat_after == ctrl_after) {
        uart_puts("WARN: STAT still mirrors CTRL -- remap did not fix alias.\r\n");
        uart_puts("HADDR[5] may also be stuck, or audio_ahb_if not re-synthesized.\r\n");
    } else if (stat_after != 0) {
        uart_puts("STAT differs from CTRL -- remap working!\r\n");
        uart_puts("STAT bit[0]="); uart_putc((stat_after & STAT_FIFO_EMPTY) ? '1' : '0');
        uart_puts(" bit[3]="); uart_putc((stat_after & STAT_RX_ENABLE) ? '1' : '0');
        uart_puts("\r\n");
    }

    uart_puts("Audio addr scan after RX_ENABLE:\r\n");
    for (i = 0; i <= 0x80U; i += 4U) {
        scan_addr = 0x20010000UL + (uint32_t)i;
        scan_val = *(volatile uint32_t *)scan_addr;
        uart_puts("  +0x");
        uart_hex8((uint8_t)i);
        uart_puts(" = ");
        uart_hex32(scan_val);
        if (scan_val == ctrl_after) {
            uart_puts("  (same as CTRL)");
        }
        uart_puts("\r\n");
    }

    AUDIO_CONTROL = 0;

    /* ---- 1. Register check ---- */
    uart_puts("\r\n--- 1. Audio registers ---\r\n");
    audio_dump_status();

    /* Pulse FIFO clear, check fifo_empty via STATUS */
    AUDIO_CONTROL = CTRL_FIFO_CLEAR;
    delay(10);
    v = AUDIO_STATUS;
    uart_puts("STAT after fifo_clear="); uart_hex32(v);
    uart_puts(" empty="); uart_putc((v & STAT_FIFO_EMPTY) ? '1' : '0');
    uart_puts("\r\n");

    if (!(v & STAT_FIFO_EMPTY)) {
        uart_puts("WARN: fifo_empty=0 after clear -- STATUS may still be aliased.\r\n");
        uart_puts("Will use direct DATA reads for PCM.\r\n");
    } else {
        uart_puts("fifo_empty=1 OK. STATUS is functional!\r\n");
    }

    AUDIO_CONTROL = 0;

    /* ---- 2. PCM capture ---- */
    uart_puts("--- 2. PCM mode ---\r\n");
    audio_start_pcm();

    if (!(AUDIO_CONTROL & CTRL_RX_ENABLE)) {
        uart_puts("FAIL: rx_enable=0 after start.\r\n");
        while (1) __NOP();
    }
    uart_puts("rx_enable confirmed via CONTROL readback.\r\n");

    uart_puts("  Draining 1536 startup samples while RX keeps running...\r\n");
    discard_count = 1536UL;
    discard_nonzero = 0UL;
    for (i = 0; i < discard_count; i++) {
        if (audio_read_pcm(&s)) {
            if (s != 0) discard_nonzero++;
        }
    }
    uart_puts("  discarded nonzero="); uart_dec(discard_nonzero);
    uart_puts("/"); uart_dec(discard_count); uart_puts("\r\n");
    audio_dump_status();

    /*
     * Try STATUS-based polling first. If that doesn't work (fifo_empty
     * never clears), fall back to direct DATA reads with delay.
     */
    uart_puts("  Reading "); uart_dec(PCM_SAMPLE_COUNT); uart_puts(" samples:\r\n");
    pcm_ok = 0;
    min_val = 0xFFFFFFFFUL;
    max_val = 0;
    zero_cnt = 0;
    direct_reads = 0;

    for (i = 0; i < PCM_SAMPLE_COUNT; i++) {
        if (audio_read_pcm(&s)) {
            /* STATUS-based read worked */
            samples[i] = s;
        } else {
            /* STATUS polling timed out. Fall back to direct DATA read. */
            if (direct_reads == 0) {
                uart_puts("  (STATUS poll timeout; switching to direct reads)\r\n");
            }
            s = (uint16_t)AUDIO_DATA;
            samples[i] = s;
            direct_reads++;
            /* Small delay between direct reads to let FIFO accumulate */
            delay(CLK_HZ / 16000);  /* ~1 sample period at 15.625kHz */
        }

        if ((uint32_t)s < min_val) min_val = (uint32_t)s;
        if ((uint32_t)s > max_val) max_val = (uint32_t)s;
        if (s == 0) zero_cnt++;

        ss = (int16_t)s;
        uart_puts("  ["); uart_dec(i); uart_puts("] 0x");
        uart_hex8((uint8_t)(s >> 8)); uart_hex8((uint8_t)s);
        uart_puts(" (");
        if (ss < 0) { uart_putc('-'); ss = -ss; }
        uart_dec((uint32_t)ss);
        uart_puts(")\r\n");
    }

    uart_puts("  min=0x"); uart_hex32(min_val);
    uart_puts(" max=0x"); uart_hex32(max_val);
    uart_puts(" zeros="); uart_dec(zero_cnt); uart_puts("/");
    uart_dec(PCM_SAMPLE_COUNT); uart_puts("\r\n");

    if (min_val != max_val) {
        uart_puts("  PASS: sample values vary -> I2S data is flowing!\r\n");
    } else if (min_val == 0 && max_val == 0) {
        uart_puts("  All zeros. Check INMP441 wiring, mic_sck toggling.\r\n");
    } else if (samples[0] == ctrl_after || samples[0] == 0x0001) {
        uart_puts("  WARN: data looks like CONTROL value -> DATA still aliased.\r\n");
        uart_puts("  Check audio_ahb_if re-synthesis and HADDR[6] routing.\r\n");
    } else {
        uart_puts("  All samples identical (0x"); uart_hex32(min_val);
        uart_puts("). Possibly stale FIFO or DC offset.\r\n");
    }

    audio_stop();

    /* ---- 3. ADPCM mode ---- */
    uart_puts("--- 3. ADPCM mode ---\r\n");
    audio_start_adpcm();

    if (!(AUDIO_CONTROL & CTRL_RX_ENABLE)) {
        uart_puts("FAIL: rx_enable=0 in ADPCM mode\r\n");
        while (1) __NOP();
    }

    uart_puts("  Draining 192 startup ADPCM words while RX keeps running...\r\n");
    discard_count = 192UL;
    discard_nonzero_words = 0UL;
    for (i = 0; i < discard_count; i++) {
        if (audio_read_adpcm(&w)) {
            if (w != 0UL) discard_nonzero_words++;
        }
    }
    uart_puts("  discarded nonzero words="); uart_dec(discard_nonzero_words);
    uart_puts("/"); uart_dec(discard_count); uart_puts("\r\n");
    audio_dump_status();

    uart_puts("  Reading 5 ADPCM words:\r\n");
    for (i = 0; i < 5; i++) {
        if (!audio_read_adpcm(&w)) {
            uart_puts("  STATUS poll timeout; using direct read.\r\n");
            w = AUDIO_DATA;
        }
        uart_puts("  ["); uart_dec(i); uart_puts("] 0x");
        uart_hex32(w); uart_puts("\r\n");
    }

    audio_stop();

    /* ---- done ---- */
    uart_puts("=== Mic test done ===\r\n");
    while (1) __NOP();
}
