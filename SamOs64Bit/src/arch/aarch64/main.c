// aarch64 C entry: banner, memory size and bootargs from the
// device tree, "SamOS aarch64: hello".
// Then the GICv3 and the 100 Hz generic timer tick, IRQs on,
// and the idle thread sleeping in wfi.
// Runs with the MMU on, high (boot.S, mmu.c): prints the layout,
// the memory map and the page-frame allocator first.
//
// After hello the shared kernel_main takes over (arch.c's
// aarch64_enter_kernel): scheduler, processes, syscalls, the ELF
// loader, the heap over the same PFA as x86, the VFS on the RAM disk
// from -initrd. The lines above stay the early console's; the trap
// spike (samos.sched_test) still runs its kernel threads instead, and
// a GICv2 machine (no tick) halts after hello.
//
// Entered from the UEFI loader (BOOTAA64.EFI), x0 is its
// struct samos_boot_info instead of a DTB: the memory map is UEFI's
// (memmap.c), the command line and initrd come from boot_info, and
// the device tree is the firmware's or, with ACPI only, one built
// from the MADT, SPCR, GTDT and FADT (acpi_dt.c).
//
// Test hooks in bootargs:
//   samos.fault_test[=brk|undef|dabort|svc]  take that exception
//       (fault_test.S); the first three dump and halt, svc returns.
//   samos.sched_test  two busy kernel threads preempted by the tick,
//       then the tick counter checked against CNTPCT_EL0 while idle.
//   samos.mmu_test  mapping probes, the PFA over all RAM, user address
//       spaces (mmu_test.c); samos.fault_test=textwrite|dataexec|ident|
//       wxn|uexec take the matching abort there.
#include <stdint.h>
#include "firmware/fdt.h"
#include "boot/boot_info.h"
#include "arch/aarch64/acpi_dt.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/pl011.h"
#include "arch/aarch64/sched.h"
#include "arch/aarch64/timer.h"
#include "arch/aarch64/trap.h"

#define MAX_MEMORY_REGIONS 8
#define TICK_HZ            100
#define WORK_LINES         8        // lines each test thread prints
#define WORK_MS            40       // busy time between lines (4 ticks)
#define WORK_STACK         8192

// One whole line even if a tick lands in the middle of it.
#define SAY(...) do { uint64_t d_ = irq_save(); console_printf(__VA_ARGS__); irq_restore(d_); } while (0)

void aarch64_main(uint64_t arg, uint64_t boot_el);
void fault_test_brk(void);
void fault_test_undef(void);
void fault_test_dabort(uint64_t addr);
uint64_t fault_test_svc(uint64_t x0);
void mmu_test(const struct fdt* f);
int mmu_fault_test(const char* kind);
void aarch64_gic_ready(void);
void aarch64_enter_kernel(const struct fdt* f, uint64_t dtb_pa, const char* bootargs,
                          const struct samos_boot_info* loader) __attribute__((noreturn));

static uint8_t work_stacks[2][WORK_STACK] __attribute__((aligned(16)));
static uint64_t timer_epoch;        // CNTPCT when the tick started

static uint64_t current_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return el >> 2;
}

// Find `key` as a whole space-separated word in `s`. Copies its
// "=value" (or "" for a bare word) into out; 0 if found, -1 if not.
static int cmdline_value(const char* s, const char* key, char* out, int size)
{
    while (s && *s) {
        while (*s == ' ')
            s++;
        const char* k = key;
        const char* p = s;
        while (*k && *p == *k) {
            p++;
            k++;
        }
        if (!*k && (*p == 0 || *p == ' ' || *p == '=')) {
            int n = 0;
            if (*p == '=')
                for (p++; *p && *p != ' ' && n < size - 1; p++)
                    out[n++] = *p;
            out[n] = 0;
            return 0;
        }
        while (*s && *s != ' ')
            s++;
    }
    return -1;
}

static int str_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void svc_test_handler(struct trap_frame* f, uint32_t imm)
{
    console_printf("svc: #0x%x at EL1, ELR 0x%lx, x0 0x%lx, x2 0x%lx\n", imm, f->elr, f->x[0], f->x[2]);
    f->x[0] += 1;
}

// `hole` is a physical address with nothing behind it (the end of
// RAM), for the data abort: mapped as Device memory, the load is an
// external abort from the bus, not a translation fault.
static void fault_test(const char* kind, uint64_t hole)
{
    if (!*kind)
        kind = "brk";
    console_printf("fault test: %s\n", kind);
    if (str_eq(kind, "brk")) {
        fault_test_brk();
    } else if (str_eq(kind, "undef")) {
        fault_test_undef();
    } else if (str_eq(kind, "dabort")) {
        fault_test_dabort(mmu_map_device(hole, PAGE_SIZE));
    } else if (str_eq(kind, "svc")) {
        trap_set_svc_handler(svc_test_handler);
        console_printf("fault test: svc returned 0x%lx\n", fault_test_svc(0x5a));
        trap_set_svc_handler(0);
        return;
    } else if (mmu_fault_test(kind) < 0) {
        console_printf("fault test: unknown kind, ignored\n");
        return;
    }
    console_printf("fault test: no exception taken\n");
}

// Sleep in wfi for n ticks, from one tick edge to another. Prints the
// time the counter saw and how often wfi woke up.
static void measure_ticks(uint64_t n)
{
    uint64_t t0 = timer_ticks();
    while (timer_ticks() == t0)
        __asm__ volatile("wfi");
    t0 = timer_ticks();
    uint64_t c0 = counter_read(), wakeups = 0;
    while (timer_ticks() < t0 + n) {
        __asm__ volatile("wfi");
        wakeups++;
    }
    uint64_t us = (counter_read() - c0) * 1000000 / counter_hz();
    uint64_t now = timer_ticks();
    uint64_t by_counter = (counter_read() - timer_epoch) * TICK_HZ / counter_hz();
    SAY("timer: %lu ticks took %lu us by CNTPCT\n", n, us);
    SAY("idle: %lu wfi wakeups in %lu ticks\n", wakeups, n);
    SAY("timer: %lu ticks since start, CNTPCT says %lu\n", now, by_counter);
}

// Busy-loop thread: never yields, so only the tick can switch it out.
// n and sum live in registers across every preemption; a bad restore
// breaks the checksum.
static void worker(void* arg)
{
    const char* name = arg;
    uint64_t n = 0, sum = 0;
    for (int line = 0; line < WORK_LINES; line++) {
        uint64_t end = counter_read() + counter_hz() * WORK_MS / 1000;
        while (counter_read() < end) {
            n++;
            sum += n;
        }
        SAY("thread %s: line %d, tick %lu\n", name, line, timer_ticks());
    }
    SAY("thread %s: done, %lu loops, checksum %s\n", name, n, sum == n * (n + 1) / 2 ? "ok" : "BAD");
}

static void sched_test(void)
{
    sched_spawn(worker, "A", work_stacks[0], WORK_STACK);
    sched_spawn(worker, "B", work_stacks[1], WORK_STACK);
    SAY("sched: 2 threads spawned\n");
    // Idle: runs only when neither thread is runnable.
    while (sched_runnable())
        __asm__ volatile("wfi");
    SAY("sched: threads done, %lu switches, now on thread %d\n", sched_switches(), sched_current());
    measure_ticks(TICK_HZ);
}

static void uefi_report(const struct samos_boot_info* bi, uint64_t pa)
{
    console_printf("uefi: boot_info at 0x%lx, version %u, %lu memory map entries, kernel 0x%lx-0x%lx\n", pa,
                   bi->version, *(const uint64_t*)phys_to_virt(bi->memory_map), bi->kernel_start, bi->kernel_end);
    console_printf("uefi: dtb 0x%lx, acpi rsdp 0x%lx, initrd 0x%lx (%lu bytes)\n", bi->dtb, bi->acpi_rsdp,
                   bi->initrd_base, bi->initrd_size);
    // A GOP framebuffer is the screen (arch.c screen_init);
    // without one, the aarch64 device code's ramfb.
    if (bi->fb_format == SAMOS_FB_FORMAT_NONE)
        console_printf("uefi: no GOP framebuffer\n");
    else
        console_printf("uefi: GOP framebuffer 0x%lx, %ux%u, pitch %u, format %u\n", bi->fb_base, bi->fb_width,
                       bi->fb_height, bi->fb_pitch, bi->fb_format);
    console_printf("uefi: cmdline \"%s\"\n", bi->cmdline);
}

void aarch64_main(uint64_t arg, uint64_t boot_el)
{
    struct fdt f;
    const struct samos_boot_info* bi = memmap_boot_info(arg);
    uint64_t dtb = bi ? bi->dtb : arg;
    int r = dtb ? fdt_init(&f, phys_to_virt(dtb), 0) : FDT_ERR_NOTFOUND;
    const char* how = pl011_init(r == 0 ? &f : 0);

    console_printf("\nSamOS aarch64: booting (entered at EL%lu, running at EL%lu)\n", boot_el, current_el());
    if (bi) {
        uefi_report(bi, arg);
        if (r < 0 && bi->acpi_rsdp && acpi_dt_build(bi->acpi_rsdp, bi->cmdline, &f) == 0) {
            r = 0;
            dtb = kimage_pa(f.blob);
            how = pl011_init(&f);
        }
    }
    console_printf("console: pl011 at 0x%lx (%s)\n", pl011_base(), how);
    if (r < 0) {
        console_printf("fdt: no valid device tree at 0x%lx, error %d\n", dtb, -r);
        aarch64_halt();
    }
    console_printf("fdt: 0x%lx, version %u, %u bytes\n", dtb, f.version, f.size);

    struct fdt_region mem[MAX_MEMORY_REGIONS];
    int n = fdt_memory_regions(&f, mem, MAX_MEMORY_REGIONS);
    uint64_t total = 0;
    for (int i = 0; i < n && i < MAX_MEMORY_REGIONS; i++) {
        console_printf("memory: 0x%lx size 0x%lx\n", mem[i].base, mem[i].size);
        total += mem[i].size;
    }
    console_printf("memory: %lu MiB in %d region(s)\n", total >> 20, n < 0 ? 0 : n);
    mmu_report();
    memmap_print(&f);

    const char* args = bi ? bi->cmdline : fdt_bootargs(&f);
    console_printf("bootargs: %s\n", args ? args : "(none)");

    char unused[2];
    if (cmdline_value(args, "samos.mmu_test", unused, sizeof unused) == 0)
        mmu_test(&f);

    char kind[16];
    if (cmdline_value(args, "samos.fault_test", kind, sizeof kind) == 0)
        fault_test(kind, n > 0 ? mem[0].base + mem[0].size : 0);

    int ticking = gic_init(&f) == 0 && timer_init(&f) == 0;
    int spike = cmdline_value(args, "samos.sched_test", unused, sizeof unused) == 0;
    if (ticking) {
        timer_epoch = counter_read();
        if (spike) {
            sched_init();
            timer_start(TICK_HZ, sched_tick);
            irq_enable();
            sched_test();
        } else {
            timer_start(TICK_HZ, 0);
            irq_enable();
            measure_ticks(10);
            irq_save();
        }
    }

    console_printf("SamOS aarch64: hello\n");
    if (!ticking) {
        console_printf("SamOS aarch64: the shared kernel needs a GICv3 tick, halted\n");
        aarch64_halt();
    }
    if (spike)
        for (;;)
            __asm__ volatile("wfi");    // the idle thread
    // The shared kernel, with IRQs masked until the first task.
    aarch64_gic_ready();
    aarch64_enter_kernel(&f, dtb, args, bi);
}
