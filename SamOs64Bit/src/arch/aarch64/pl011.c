// PL011 early console. See pl011.h.
//
// Transmit polled; the aarch64 device work: receive by interrupt (arch.c). The UART is
// used as the firmware (or QEMU) left it: programming the baud rate
// needs the UART clock from the device tree's clocks phandle, which a
// real board will need but QEMU ignores. Linux's earlycon does
// the same.
#include "arch/aarch64/pl011.h"
#include "arch/aarch64/mmu.h"

#define PL011_DR        0x00
#define PL011_FR        0x18
#define PL011_FR_TXFF   (1 << 5)    // transmit FIFO full

#define PL011_QEMU_VIRT 0x09000000UL
// The Pi 5's RP1 UART0 (GPIO 14/15) is a PL011 behind RP1's PCIe
// link ("arm,pl011-axi"). With pciex4_reset=0 the firmware leaves the
// link and RP1's window at 0x1f00000000 (UART0 at +0x30000, RP1
// datasheet); used when the device tree's PCI ranges do not translate.
// Not run on a board yet.
#define PL011_RP1_UART0 0x1f00030000UL

static volatile uint32_t* uart;
static uint64_t uart_pa;

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
        } else if (node >= 0 && fdt_node_is_compatible(f, node, "arm,pl011-axi")) {
            base = base_of(f, node);
            how = "stdout-path, RP1";
            if (!base) {
                base = PL011_RP1_UART0;
                how = "stdout-path, RP1 at the firmware's window";
            }
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
    // Device memory in the high half (with the MMU off, base itself).
    uart_pa = base;
    uart = (volatile uint32_t*)mmu_map_device(base, 0x1000);
    return how;
}

uint64_t pl011_base(void)
{
    return uart_pa;
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

// Receive, for the keyboard over the serial line.
#define PL011_FR_RXFE   (1 << 4)    // receive FIFO empty
#define PL011_IMSC      0x38
#define PL011_ICR       0x44
#define PL011_RX_INTS   ((1 << 4) | (1 << 6))     // RXIM, RTIM (receive timeout)

void pl011_rx_enable(void)
{
    if (!uart)
        return;
    uart[PL011_ICR / 4] = PL011_RX_INTS;
    uart[PL011_IMSC / 4] |= PL011_RX_INTS;
}

int pl011_getc(void)
{
    if (!uart)
        return -1;
    if (uart[PL011_FR / 4] & PL011_FR_RXFE) {
        uart[PL011_ICR / 4] = PL011_RX_INTS;
        return -1;
    }
    return (int)(uart[PL011_DR / 4] & 0xff);
}
