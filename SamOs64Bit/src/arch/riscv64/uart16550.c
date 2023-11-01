// Polled NS16550A output.
//
// OpenSBI has already programmed the UART for its own banner (QEMU
// ignores the divisor anyway), so init keeps the line settings and
// only masks the UART's interrupts and enables the FIFOs. The MMU is
// off, so the registers are plain physical addresses.

#include "arch/riscv64/uart16550.h"

#define UART_THR 0  // transmit holding (write)
#define UART_IER 1  // interrupt enable
#define UART_FCR 2  // FIFO control (write)
#define UART_LSR 5  // line status
#define UART_LSR_THRE 0x20

static volatile uint8_t* uart_base;
static uint32_t uart_shift;

static inline void reg_write(int reg, uint8_t v)
{
    uart_base[reg << uart_shift] = v;
}

static inline uint8_t reg_read(int reg)
{
    return uart_base[reg << uart_shift];
}

void uart16550_init(uintptr_t base, uint32_t reg_shift)
{
    uart_base = (volatile uint8_t*)base;
    uart_shift = reg_shift;
    reg_write(UART_IER, 0);
    reg_write(UART_FCR, 0x07);  // enable and clear both FIFOs
}

void uart16550_putc(char c)
{
    while (!(reg_read(UART_LSR) & UART_LSR_THRE)) {
    }
    reg_write(UART_THR, (uint8_t)c);
}
