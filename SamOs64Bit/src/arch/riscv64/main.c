// riscv64 C entry: boot to a serial hello.
//
// A standalone spike until the hardware layer splits the hardware layer; then this
// becomes the riscv64 arch_early_init and hands off to kernel_main.
// Output starts on the SBI console and moves to the 16550 UART the
// device tree names.

#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/uart16550.h"
#include "firmware/fdt.h"
#include "string/string.h"

bool riscv64_test_mode;

static struct fdt fdt;

void riscv64_halt(void)
{
    console_printf("riscv64: halted\n");
    if (riscv64_test_mode) {
        sbi_shutdown();
    }
    for (;;) {
        __asm__ volatile("csrci sstatus, 2; wfi");
    }
}

// True when `word` is a whole space-separated word of `args`.
static bool cmdline_has(const char* args, const char* word)
{
    int n = strlen(word);
    for (const char* s = args; s && *s; s++) {
        if ((s == args || s[-1] == ' ') && strncmp(s, word, n) == 0 && (s[n] == ' ' || s[n] == 0)) {
            return true;
        }
    }
    return false;
}

// HSM state of every hart: the boot hart is started, the rest stay
// stopped in OpenSBI until the riscv64 SMP work. Hart ids on QEMU virt are 0..n-1.
static void print_harts(void)
{
    if (!sbi_probe(SBI_EXT_HSM)) {
        return;
    }
    console_printf("hsm:");
    for (unsigned long id = 0; id < 64; id++) {
        long s = sbi_hart_status(id);
        if (s < 0) {
            break;
        }
        console_printf(" %lu=%s", id, s == SBI_HSM_STARTED ? "started" : s == SBI_HSM_STOPPED ? "stopped" : "other");
    }
    console_printf("\n");
}

// /chosen stdout-path, else the first ns16550a. QEMU's OpenSBI writes
// DBCN output to the same UART, so the switch is seamless.
static void console_to_uart(void)
{
    int node = fdt_stdout_node(&fdt);
    if (node < 0 || !fdt_node_is_compatible(&fdt, node, "ns16550a")) {
        node = fdt_find_compatible(&fdt, -1, "ns16550a");
    }
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(&fdt, node, 0, &reg) != 0) {
        console_printf("console: no ns16550a, staying on SBI\n");
        return;
    }
    uart16550_init(reg.base, fdt_getprop_u32(&fdt, node, "reg-shift", 0));
    console_set_putc(uart16550_putc);
    console_printf("console: ns16550a at 0x%lx\n", reg.base);
}

static void print_memory(void)
{
    struct fdt_region mem[8];
    int n = fdt_memory_regions(&fdt, mem, 8);
    uint64_t total = 0;
    for (int i = 0; i < n && i < 8; i++) {
        console_printf("memory: 0x%lx size 0x%lx\n", mem[i].base, mem[i].size);
        total += mem[i].size;
    }
    console_printf("memory: %lu MiB in %d region(s)\n", total >> 20, n);
}

// Children of /cpus with device_type "cpu" and not disabled.
static int count_harts(void)
{
    int count = 0;
    int cpus = fdt_path_offset(&fdt, "/cpus");
    for (int n = cpus < 0 ? cpus : fdt_first_child(&fdt, cpus); n >= 0; n = fdt_next_sibling(&fdt, n)) {
        const char* type = fdt_getprop_string(&fdt, n, "device_type");
        const char* status = fdt_getprop_string(&fdt, n, "status");
        if (type && strncmp(type, "cpu", 4) == 0 && !(status && strncmp(status, "disabled", 9) == 0)) {
            count++;
        }
    }
    return count;
}

void riscv64_main(uint64_t hartid, uint64_t dtb)
{
    sbi_init();
    console_printf("\nSamOS riscv64 (S-R1)\n");
    console_printf("boot hart: %lu\n", hartid);
    console_printf("dtb: 0x%lx\n", dtb);
    sbi_print_info();

    // 1 MiB is far beyond any real tree; it only bounds a bad header.
    int err = fdt_init(&fdt, (const void*)dtb, 1 << 20);
    if (err != 0) {
        console_printf("fdt: no device tree at 0x%lx (error %d)\n", dtb, err);
    } else {
        console_to_uart();
        print_memory();
        console_printf("harts: %d\n", count_harts());
        const char* args = fdt_bootargs(&fdt);
        console_printf("bootargs: %s\n", args ? args : "");
        riscv64_test_mode = cmdline_has(args, "samos.test=1");
        if (cmdline_has(args, "samos.trap_test")) {
            // OpenSBI guards its own image (RAM base) with PMP, so an
            // S-mode load there is a load access fault.
            console_printf("trap test: load from 0x80000000\n");
            (void)*(volatile uint64_t*)0x80000000;
        }
    }
    print_harts();
    console_printf("SamOS riscv64: hello\n");
    riscv64_halt();
}
