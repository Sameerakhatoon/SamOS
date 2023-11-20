// PL011 early console. See pl011.h.
//
// Transmit only, polled. The UART is used as the firmware (or QEMU)
// left it: programming the baud rate needs the UART clock from the
// device tree's clocks phandle, which a real board will need
// but QEMU ignores. Linux's earlycon does the same.
#include "arch/aarch64/pl011.h"

#define PL011_DR        0x00
#define PL011_FR        0x18
#define PL011_FR_TXFF   (1 << 5)    // transmit FIFO full

#define PL011_QEMU_VIRT 0x09000000UL

static volatile uint32_t* uart;

static uint64_t base_of(const struct fdt* f, int node)
{
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) < 0)
        return 0;
    return reg.base;
}

const char* pl011_init(const struct fdt* f)
{
    uint64_t base = 0;
    const char* how = "fallback";
    if (f) {
        int node = fdt_stdout_node(f);
        if (node >= 0 && fdt_node_is_compatible(f, node, "arm,pl011")) {
            base = base_of(f, node);
            how = "stdout-path";
        }
        if (!base) {
            base = base_of(f, fdt_find_compatible(f, -1, "arm,pl011"));
            how = "compatible";
        }
    }
    if (!base) {
        base = PL011_QEMU_VIRT;
        how = "fallback";
    }
    uart = (volatile uint32_t*)base;
    return how;
}

uint64_t pl011_base(void)
{
    return (uint64_t)uart;
}

void pl011_putc(char c)
{
    if (!uart)
        return;
    if (c == '\n')
        pl011_putc('\r');
    while (uart[PL011_FR / 4] & PL011_FR_TXFF)
        ;
    uart[PL011_DR / 4] = (uint8_t)c;
}

void pl011_puts(const char* s)
{
    while (*s)
        pl011_putc(*s++);
}
