// aarch64 C entry: banner, memory size and bootargs from the
// device tree, "SamOS aarch64: hello", then halt.
//
// A standalone spike until the hardware layer splits the kernel: boot.S calls
// aarch64_main instead of the shared kernel_main, and the output
// helpers here are the early console that arch_console_putc will
// wrap. Test hook: samos.fault_test in bootargs executes brk #0x5a
// to show the exception dump (vectors.S).
#include <stdint.h>
#include "firmware/fdt.h"
#include "arch/aarch64/pl011.h"

#define MAX_MEMORY_REGIONS 8

void aarch64_main(uint64_t dtb, uint64_t boot_el);
void aarch64_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr);

static void put_hex(uint64_t v)
{
    char buf[19];
    int i = sizeof buf - 1;
    buf[i] = 0;
    do {
        buf[--i] = "0123456789abcdef"[v & 0xf];
        v >>= 4;
    } while (v);
    buf[--i] = 'x';
    buf[--i] = '0';
    pl011_puts(buf + i);
}

static void put_dec(uint64_t v)
{
    char buf[21];
    int i = sizeof buf - 1;
    buf[i] = 0;
    do {
        buf[--i] = '0' + v % 10;
        v /= 10;
    } while (v);
    pl011_puts(buf + i);
}

static uint64_t current_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return el >> 2;
}

// Is `word` a whole space-separated word (or word=value) in `s`?
static int cmdline_has(const char* s, const char* word)
{
    while (*s) {
        while (*s == ' ')
            s++;
        const char* w = word;
        const char* p = s;
        while (*w && *p == *w) {
            p++;
            w++;
        }
        if (!*w && (*p == 0 || *p == ' ' || *p == '='))
            return 1;
        while (*s && *s != ' ')
            s++;
    }
    return 0;
}

static void halt(void)
{
    for (;;)
        __asm__ volatile("msr daifset, #0xf\n\twfi");
}

void aarch64_main(uint64_t dtb, uint64_t boot_el)
{
    struct fdt f;
    int r = fdt_init(&f, (const void*)dtb, 0);
    const char* how = pl011_init(r == 0 ? &f : 0);

    pl011_puts("\nSamOS aarch64: booting (entered at EL");
    put_dec(boot_el);
    pl011_puts(", running at EL");
    put_dec(current_el());
    pl011_puts(")\nconsole: pl011 at ");
    put_hex(pl011_base());
    pl011_puts(" (");
    pl011_puts(how);
    pl011_puts(")\n");
    if (r < 0) {
        pl011_puts("fdt: no valid device tree at ");
        put_hex(dtb);
        pl011_puts(", error ");
        put_dec(-r);
        pl011_puts("\n");
        halt();
    }
    pl011_puts("fdt: ");
    put_hex(dtb);
    pl011_puts(", version ");
    put_dec(f.version);
    pl011_puts(", ");
    put_dec(f.size);
    pl011_puts(" bytes\n");

    struct fdt_region mem[MAX_MEMORY_REGIONS];
    int n = fdt_memory_regions(&f, mem, MAX_MEMORY_REGIONS);
    uint64_t total = 0;
    for (int i = 0; i < n && i < MAX_MEMORY_REGIONS; i++) {
        pl011_puts("memory: ");
        put_hex(mem[i].base);
        pl011_puts(" size ");
        put_hex(mem[i].size);
        pl011_puts("\n");
        total += mem[i].size;
    }
    pl011_puts("memory: ");
    put_dec(total >> 20);
    pl011_puts(" MiB in ");
    put_dec(n < 0 ? 0 : n);
    pl011_puts(" region(s)\n");

    const char* args = fdt_bootargs(&f);
    pl011_puts("bootargs: ");
    pl011_puts(args ? args : "(none)");
    pl011_puts("\n");

    if (args && cmdline_has(args, "samos.fault_test")) {
        pl011_puts("fault test: brk #0x5a\n");
        __asm__ volatile("brk #0x5a");
    }

    pl011_puts("SamOS aarch64: hello\n");
    halt();
}

static const char* const vector_names[4] = { "sync", "irq", "fiq", "serror" };
static const char* const vector_groups[4] = {
    "current EL, SP_EL0", "current EL, SP_ELx", "lower EL, AArch64", "lower EL, AArch32",
};

void aarch64_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr)
{
    pl011_puts("\nexception: ");
    pl011_puts(vector_names[vector & 3]);
    pl011_puts(" (");
    pl011_puts(vector_groups[(vector >> 2) & 3]);
    pl011_puts(")\n  ESR ");
    put_hex(esr);
    pl011_puts(" (EC ");
    put_hex(esr >> 26);
    pl011_puts(")\n  ELR ");
    put_hex(elr);
    pl011_puts("\n  FAR ");
    put_hex(far);
    pl011_puts("\n  SPSR ");
    put_hex(spsr);
    pl011_puts("\nexception: halted\n");
    halt();
}
