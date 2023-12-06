// Polled NS16550A output. Interrupt-driven input.
//
// OpenSBI has already programmed the UART for its own banner (QEMU
// ignores the divisor anyway), so init keeps the line settings and
// only masks the UART's interrupts and enables the FIFOs. The MMU is
// off, so the registers are plain physical addresses.

#include "arch/riscv64/uart16550.h"

#define UART_RBR 0  // receive buffer (read)
#define UART_THR 0  // transmit holding (write)
#define UART_IER 1  // interrupt enable
#define UART_FCR 2  // FIFO control (write)
#define UART_MCR 4  // modem control
#define UART_LSR 5  // line status
#define UART_IER_ERBFI 0x01  // received data available
#define UART_MCR_DTR   0x01
#define UART_MCR_RTS   0x02
#define UART_MCR_OUT2  0x08
#define UART_LSR_DR    0x01
#define UART_LSR_THRE  0x20

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

void uart16550_enable_rx_irq(void)
{
    reg_write(UART_MCR, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
    reg_write(UART_IER, UART_IER_ERBFI);
}

// Reading RBR takes the FIFO byte by byte; the line drops once it is
// empty, so the PLIC completion that follows does not re-raise it.
int uart16550_getc(void)
{
    if (!(reg_read(UART_LSR) & UART_LSR_DR)) {
        return -1;
    }
    return reg_read(UART_RBR);
}
