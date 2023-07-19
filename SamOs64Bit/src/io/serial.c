#include "serial.h"
#include "io.h"
#include <stdint.h>

// COM1 serial console. Register offsets from the 16550
// datasheet: +0 data / divisor-low (DLAB=1), +1 IER / divisor-
// high, +2 FIFO control, +3 line control, +4 modem control,
// +5 line status.
#define COM_DATA 0
#define COM_IER  1
#define COM_FCR  2
#define COM_LCR  3
#define COM_MCR  4
#define COM_LSR  5
#define COM_LSR_THR_EMPTY 0x20

static int serial_ready = 0;

void serial_init(void)
{
    uint16_t p = SERIAL_COM1_PORT;
    outb(p + COM_IER, 0x00);   // no UART interrupts, we poll
    outb(p + COM_LCR, 0x80);   // DLAB on
    outb(p + COM_DATA, 0x01);  // divisor 1 = 115200 baud
    outb(p + COM_IER, 0x00);
    outb(p + COM_LCR, 0x03);   // 8N1, DLAB off
    outb(p + COM_FCR, 0xC7);   // FIFO on, cleared, 14-byte threshold
    outb(p + COM_MCR, 0x03);   // DTR + RTS

    // A floating bus (no UART fitted) reads back 0xFF; don't spin
    // forever waiting for THR-empty on such a machine.
    if (insb(p + COM_LSR) == 0xFF)
    {
        return;
    }
    serial_ready = 1;
}

static void serial_raw_putc(char c)
{
    // Bounded wait: a stuck UART must not hang the kernel.
    for (int spins = 0; spins < 100000; spins++)
    {
        if (insb(SERIAL_COM1_PORT + COM_LSR) & COM_LSR_THR_EMPTY)
        {
            break;
        }
    }
    outb(SERIAL_COM1_PORT + COM_DATA, (unsigned char)c);
}

void serial_putc(char c)
{
    if (!serial_ready)
    {
        return;
    }
    if (c == '\n')
    {
        serial_raw_putc('\r');
    }
    serial_raw_putc(c);
}

void serial_write(const char* str)
{
    while (str && *str)
    {
        serial_putc(*str++);
    }
}

void serial_write_hex(unsigned long long value)
{
    static const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        serial_putc(digits[(value >> shift) & 0xF]);
    }
}
