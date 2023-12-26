// riscv64 C entry: boot to a serial hello.
//
// A standalone spike until the hardware layer splits the hardware layer; then this
// becomes the riscv64 arch_early_init and hands off to kernel_main.
// Output starts on the SBI console and moves to the 16550 UART the
// device tree names.
//
// Then the trap path's tests (bootargs), the 100 Hz timer and
// the PLIC with the UART's receive interrupt, and two kernel threads
// that only the timer switches.

#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/kthread.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/mmu_test.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/timer.h"
#include "arch/riscv64/trap.h"
#include "arch/riscv64/uart16550.h"
#include "firmware/fdt.h"
#include "string/string.h"

#define TICK_HZ 100

bool riscv64_test_mode;

static struct fdt fdt;
static int uart_node = -1;
static uintptr_t uart_va;

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
    uart_va = mmu_map_mmio(reg.base, reg.size);
    if (!uart_va) {
        console_printf("console: cannot map the ns16550a at 0x%lx\n", reg.base);
        return;
    }
    uart16550_init(uart_va, fdt_getprop_u32(&fdt, node, "reg-shift", 0));
    console_set_putc(uart16550_putc);
    uart_node = node;
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

static bool is_cpu(const struct fdt* f, int node)
{
    const char* type = fdt_getprop_string(f, node, "device_type");
    const char* status = fdt_getprop_string(f, node, "status");
    return type && strncmp(type, "cpu", 4) == 0 && !(status && strncmp(status, "disabled", 9) == 0);
}

// Children of /cpus with device_type "cpu" and not disabled.
static int count_harts(void)
{
    int count = 0;
    int cpus = fdt_path_offset(&fdt, "/cpus");
    for (int n = cpus < 0 ? cpus : fdt_first_child(&fdt, cpus); n >= 0; n = fdt_next_sibling(&fdt, n)) {
        count += is_cpu(&fdt, n);
    }
    return count;
}

int riscv64_cpu_node(const struct fdt* f, uint64_t hartid)
{
    int cpus = fdt_path_offset(f, "/cpus");
    for (int n = cpus < 0 ? cpus : fdt_first_child(f, cpus); n >= 0; n = fdt_next_sibling(f, n)) {
        struct fdt_region reg;
        if (is_cpu(f, n) && fdt_reg(f, n, 0, &reg) == 0 && reg.base == hartid) {
            return n;
        }
    }
    return FDT_ERR_NOTFOUND;
}

// samos.trap_test[=load|illegal|ebreak]: one exception from a known
// instruction (boot.S), so the dump's sepc can be checked against the
// address printed here. Load and illegal are fatal; a breakpoint is
// stepped over and boot goes on.
static void trap_tests(const char* args)
{
    if (cmdline_has(args, "samos.trap_test") || cmdline_has(args, "samos.trap_test=load")) {
        // OpenSBI guards its own image (RAM base) with PMP, so an
        // S-mode load there is a load access fault. Through the
        // direct map, which maps it like all of /memory.
        uint64_t va = (uint64_t)phys_to_virt(0x80000000);
        console_printf("trap test: load from 0x%lx (pa 0x80000000) at 0x%lx\n", va, (uint64_t)trap_test_load);
        trap_test_load(va);
    }
    if (cmdline_has(args, "samos.trap_test=illegal")) {
        console_printf("trap test: illegal instruction at 0x%lx\n", (uint64_t)trap_test_illegal);
        trap_test_illegal();
    }
    if (cmdline_has(args, "samos.trap_test=ebreak")) {
        console_printf("trap test: ebreak at 0x%lx\n", (uint64_t)trap_test_ebreak);
        trap_test_ebreak();
        console_printf("trap test: ebreak returned\n");
    }
}

// samos.mmu_test (frame allocator, uaccess), samos.mmu_noasid
// (flush on every satp switch instead), samos.mmu_fault=<kind>.
static void mmu_tests(const char* args)
{
    if (cmdline_has(args, "samos.mmu_noasid")) {
        mmu_disable_asids();
        console_printf("mmu: asids off\n");
    }
    if (cmdline_has(args, "samos.mmu_test")) {
        pfa_test();
        uaccess_test();
    }
    const char* f = args;
    while (f && *f && strncmp(f, "samos.mmu_fault=", 16) != 0) {
        f++;
    }
    if (f && *f) {
        mmu_fault_test(f + 16);
    }
}

// ---- UART receive interrupt -------------------------------------

static unsigned uart_irq;
static volatile char rx_line[64];
static volatile int rx_len;
static volatile bool rx_done;

// Drain the FIFO; a line ends at CR or LF.
static void uart_interrupt(struct trap_frame* frame)
{
    int c;
    while ((c = uart16550_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            rx_done = rx_done || rx_len > 0;
        } else if (!rx_done && rx_len < (int)sizeof(rx_line) - 1) {
            rx_line[rx_len++] = (char)c;
        }
    }
}

// The UART's PLIC source is its `interrupts` cell (10 on QEMU virt).
static void uart_irq_init(void)
{
    uart_irq = uart_node < 0 ? 0 : fdt_getprop_u32(&fdt, uart_node, "interrupts", 0);
    if (plic_register_handler(uart_irq, uart_interrupt) != 0) {
        console_printf("uart: no interrupt in the device tree\n");
        return;
    }
    plic_enable(uart_irq);
    uart16550_enable_rx_irq();
    console_printf("uart: rx interrupt on plic irq %u\n", uart_irq);
}

// samos.uart_test: wait (in wfi) up to 10 s for one line of input.
static void uart_test(void)
{
    console_printf("uart test: waiting for a line\n");
    uint64_t end = timer_ticks() + 10 * TICK_HZ;
    while (!rx_done && timer_ticks() < end) {
        wfi();
    }
    uint64_t flags = irq_save();
    char line[sizeof(rx_line)];
    int n = rx_done ? rx_len : 0;
    for (int i = 0; i < n; i++) {
        line[i] = rx_line[i];
    }
    line[n] = 0;
    irq_restore(flags);
    console_printf("uart rx: '%s' after %lu interrupt(s) on irq %u\n", line, plic_count(uart_irq), uart_irq);
}

// ---- preemption --------------------------------------------------

static volatile uint64_t demo_end;

// Busy loop, never yields: a line about every 2.5 ms of counter time
// until the demo's last tick. Lines go out with interrupts off so a
// switch cannot split one.
static void spinner(void* arg)
{
    const char* name = arg;
    uint64_t pace = timer_timebase() / 400;
    uint64_t next = rdtime();
    unsigned n = 0;
    while (timer_ticks() < demo_end) {
        if (rdtime() < next) {
            continue;
        }
        uint64_t flags = irq_save();
        if (strncmp(kthread_name(), name, 8) != 0) {
            console_printf("sched: thread %s runs as %s\n", name, kthread_name());
        }
        console_printf("thread %s: %u\n", name, n++);
        irq_restore(flags);
        next = rdtime() + pace;
    }
}

// Two threads for 50 ticks; the boot context idles in wfi meanwhile.
// The tick count is checked against the time CSR by the test.
static void sched_test(void)
{
    const unsigned ticks = 50;
    uint64_t t0 = timer_ticks(), c0 = rdtime();
    demo_end = t0 + ticks;
    console_printf("sched: threads a and b for %u ticks\n", ticks);
    // Both at once: this context is the idle thread, so a tick after
    // the first create would not come back here until a had ended.
    uint64_t flags = irq_save();
    int a = kthread_create("a", spinner, "a");
    int b = kthread_create("b", spinner, "b");
    irq_restore(flags);
    unsigned long idle_wakeups = 0;
    while (kthread_alive()) {
        wfi();
        idle_wakeups++;
    }
    uint64_t t1 = timer_ticks(), c1 = rdtime();
    console_printf("sched: %lu switches, idle woke %lu time(s)\n", sched_switches(), idle_wakeups);
    console_printf("sched: slices a %lu b %lu idle %lu\n", kthread_slices(a), kthread_slices(b), kthread_slices(0));
    console_printf("ticks: %lu in %lu counter ticks, interval %lu\n", t1 - t0, c1 - c0, timer_interval());
}

// Timer, PLIC and UART interrupt, then sstatus.SIE: nothing is
// pending-and-enabled before the handlers can serve it.
static void interrupts_init(uint64_t hartid, const char* args)
{
    int cpu = riscv64_cpu_node(&fdt, hartid);
    if (cpu < 0) {
        console_printf("cpus: no node for hart %lu, no interrupts\n", hartid);
        return;
    }
    bool timer_ok = timer_init(&fdt, cpu, TICK_HZ) == 0;
    bool plic_ok = plic_init(&fdt, cpu) == 0;
    if (plic_ok) {
        uart_irq_init();
    }
    csr_set(sstatus, SSTATUS_SIE);
    console_printf("interrupts: on\n");
    trap_tests(args);
    mmu_tests(args);
    if (timer_ok && plic_ok && cmdline_has(args, "samos.uart_test")) {
        uart_test();
    }
    if (timer_ok) {
        sched_test();
    }
}

void riscv64_main(uint64_t hartid, uint64_t dtb)
{
    sbi_init();
    console_printf("\nSamOS riscv64 (S-R3)\n");
    console_printf("boot hart: %lu\n", hartid);
    console_printf("dtb: 0x%lx\n", dtb);
    sbi_print_info();
    mmu_init();

    // 1 MiB is far beyond any real tree; it only bounds a bad header.
    // mmu_boot mapped the tree into the direct map.
    int err = fdt_init(&fdt, phys_to_virt(dtb), 1 << 20);
    if (err != 0) {
        console_printf("fdt: no device tree at 0x%lx (error %d)\n", dtb, err);
    } else {
        if (mmu_memory_init(&fdt) != 0) {
            riscv64_halt();
        }
        console_to_uart();
        mmu_report(uart_va);
        print_memory();
        console_printf("harts: %d\n", count_harts());
        const char* args = fdt_bootargs(&fdt);
        console_printf("bootargs: %s\n", args ? args : "");
        riscv64_test_mode = cmdline_has(args, "samos.test=1");
        interrupts_init(hartid, args);
    }
    print_harts();
    console_printf("SamOS riscv64: hello\n");
    riscv64_halt();
}
