#include <stdint.h>

#include "soc.h"

/*
 * Minimal UART echo test — verify that UART TX and RX both work.
 * Does NOT touch SPI Flash, audio, or any other peripheral.
 *
 * To restore the full recording firmware:
 *   cp main_full_backup.c main.c
 */

/* ---------- UART register definitions (same as original) ---------- */
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

/* ========================================================================
 * main — simple UART echo test
 *
 * Expected behaviour:
 *   1. Opens a serial terminal (115200 8N1) and reset the board.
 *   2. You should see the banner text.
 *   3. Type a character — the board echoes back (character + 1).
 *      Examples: 'A' -> 'B', '0' -> '1', 'x' -> 'y'.
 *   4. Type 'q' to stop.
 *
 * PC-side terminal:
 *   python -m serial.tools.miniterm COM14 115200
 * ======================================================================== */
int main(void)
{
    host_uart_init();
    int* addr;

    host_uart_puts("=== SoC UART test ===\r\n");
    host_uart_puts("Echo: every char you send is returned as (char+1).\r\n");
    addr = (int*)0x0009000;
	*addr = 0xabcdabcd;
    host_uart_puts("Type 'q' to stop.\r\n\r\n");

    while (1) {
        uint8_t ch = host_uart_getc();   /* block until RX FIFO has data */

        if (ch == 'q') {
            host_uart_puts("Bye!\r\n");
            *addr = 0xaaaaaaaa;
            break;
        }

        host_uart_putc(ch + 1U);         /* echo with increment */
    }

    /* idle forever — test complete */
    while (1) {
        __NOP();
    }
}
