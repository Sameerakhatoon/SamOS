// The other harts and the panic stop on riscv64: x86's
// arch/x86_64/smp.c over the SBI. The portable half (calls, IPI
// dispatch, panic bookkeeping) is cpu/smp.c; arch_send_ipi is arch.c's,
// the remote TLB flush paging.c's riscv64_tlb_shootdown (SBI
// RFENCE).
//
// Bring-up (arch_start_cpus, on the boot hart, where x86 starts its
// APs): every enabled /cpus child other than the boot hart, in device
// tree order, one at a time. Hart ids are whatever the tree says (not
// dense, not from 0, the boot hart any of them), so cpu index and hart
// id differ:
//   1. its struct cpu (cpus[1], [2], ...), its trap stack, and a start
//      ticket: the stack it starts on, the cpu, the handshake;
//   2. SBI HSM hart_start at hart_entry (boot.S, physical) with the
//      ticket: satp, stvec, sscratch, sp, then riscv64_hart_main;
//   3. wait up to 5 s for it to come online, else give up on it.
// The hart claims its ticket (WAIT -> CLAIMED) before it touches
// anything else, the boot hart gives up (WAIT -> GAVE_UP); one wins
// (as on x86). A hart that arrives after the boot hart gave up
// marks the ticket PARKED and stops itself with hart_stop; the ticket
// and its stack are never freed, so a late hart cannot share the next
// one's (x86 holds its late AP with INIT instead).
// The started hart sets tp, loads the kernel's address space, its
// PLIC context, its timer and its software interrupt, then enters the
// scheduler (task_next) as an x86 AP does.

#include "arch/arch.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/aia.h"
#include "arch/riscv64/plic.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/timer.h"
#include "boot/cmdline.h"
#include "bootmarker.h"
#include "cpu/smp.h"
#include "firmware/fdt.h"
#include "firmware/fdt_dev.h"
#include "io/serial.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"
#include "task/task.h"

#define HART_START_WAIT     0
#define HART_START_CLAIMED  1
#define HART_START_GAVE_UP  2
#define HART_START_PARKED   3

#define HART_START_WINDOW_US  5000000   // TCG on a loaded host starts a vCPU slowly
#define HART_STACK_SIZE       (16 * 1024)

// hart_entry reads it with the MMU off (boot.S).
uint64_t riscv64_hart_satp;
// Set by arch_stop_other_cpus to the panicking cpu's index + 1.
volatile uint32_t riscv64_stop_harts;

void hart_entry(void);

struct hart_ticket {
    uint64_t          stack_top;   // first: hart_high loads sp from it
    struct cpu*       cpu;
    volatile uint32_t start;       // HART_START_*
};

// ------------------------------------------------------------------
// The started hart.
// ------------------------------------------------------------------

void riscv64_hart_main(uint64_t hartid, struct hart_ticket* t)
{
    if (!atomic_cas(&t->start, HART_START_WAIT, HART_START_CLAIMED)) {
        atomic_store(&t->start, HART_START_PARKED);
        sbi_hart_stop();
        riscv64_park();
    }
    struct cpu* c = t->cpu;
    __asm__ volatile("mv tp, %0" : : "r"(c));
    paging_switch(kernel_desc());
    const struct fdt* f = riscv64_fdt();
    plic_init_hart(f, riscv64_cpu_node(f, hartid));
    aia_init_hart(f, riscv64_cpu_node(f, hartid));
    csr_set(sie, SIE_SSIE);
    timer_start_hart();

    // The line before `online`: itoa's buffer is static, and once this
    // hart is online the boot hart starts the next one, which prints
    // the same line.
    serial_write("smp: cpu ");
    serial_write(itoa((int)c->index));
    serial_write(" hart ");
    serial_write(itoa((int)hartid));
    serial_write(" online\n");
    c->online = 1;
    atomic_inc(&cpu_online_count);
    task_next();
}

// ------------------------------------------------------------------
// Bring-up on the boot hart.
// ------------------------------------------------------------------

static void say_hart(uint64_t hartid, const char* what, long err)
{
    serial_write("smp: hart ");
    serial_write(itoa((int)hartid));
    serial_write(what);
    if (err) {
        serial_write(" (sbi error ");
        serial_write(itoa((int)err));
        serial_write(")");
    }
    serial_write("\n");
}

static bool wait_until(volatile uint32_t* v, uint32_t want, uint64_t us)
{
    uint64_t end = arch_time_us() + us;
    while (*v != want && arch_time_us() < end) {
        cpu_relax();
    }
    return *v == want;
}

// No answer in the window (or at once for samos.smp_late_ap). True
// when the boot hart gave up and the slot is free again; false when
// the hart claimed its ticket meanwhile and is coming online.
static bool give_up(struct hart_ticket* t, uint64_t hartid, bool test)
{
    if (!atomic_cas(&t->start, HART_START_WAIT, HART_START_GAVE_UP) && t->start == HART_START_CLAIMED) {
        if (wait_until(&t->cpu->online, 1, HART_START_WINDOW_US)) {
            return false;
        }
        panic("smp: a hart claimed its slot but never came online\n");
    }
    say_hart(hartid, test ? " did not start (samos.smp_late_ap)" : " did not start", 0);
    if (test && wait_until(&t->start, HART_START_PARKED, HART_START_WINDOW_US)) {
        say_hart(hartid, " answered late and parked", 0);
    }
    return true;
}

// Start hart `hartid` in slot cpus[index]. True when it is online.
static bool start_hart(uint64_t hartid, int index, bool late_test)
{
    struct cpu* c = &cpus[index];
    memset(c, 0, sizeof(*c));
    c->self = c;
    c->index = index;
    c->hw_id = (uint32_t)hartid;
    c->arch.hart_id = hartid;
    c->arch.plic_ctx = -1;
    c->arch.imsic_hart = -1;
    struct hart_ticket* t = kzalloc(sizeof(*t));
    uint8_t* stack = kzalloc(HART_STACK_SIZE);
    if (!t || !stack || riscv64_trap_stack(c) != 0) {
        panic("smp: no memory for a hart's stacks\n");
    }
    t->stack_top = (uint64_t)(stack + HART_STACK_SIZE);
    t->cpu = c;
    t->start = late_test ? HART_START_GAVE_UP : HART_START_WAIT;
    smp_mb();
    long err = sbi_hart_start(hartid, kernel_image_pa(hart_entry), (uint64_t)t);
    if (err) {
        // Never started: nothing runs on these.
        say_hart(hartid, " did not start", err);
        kfree(t);
        kfree(stack);
        kfree(c->arch.kernel_stack - RISCV64_TRAP_STACK_SIZE);
        return false;
    }
    if (!wait_until(&c->online, 1, late_test ? 0 : HART_START_WINDOW_US) && give_up(t, hartid, late_test)) {
        return false;
    }
    kfree(t);
    return true;
}

int arch_start_cpus(void)
{
    const struct fdt* f = riscv64_fdt();
    if (!sbi_probe(SBI_EXT_HSM) || !sbi_probe(SBI_EXT_IPI) || !sbi_probe(SBI_EXT_RFNC)) {
        serial_write("smp: no SBI HSM, IPI or RFENCE, one hart\n");
        return 1;
    }
    riscv64_hart_satp = mmu_kernel_satp();
    bool late_test = boot_cmdline_has("samos.smp_late_ap");
    int next = 1;
    int node_cpus = fdt_path_offset(f, "/cpus");
    for (int n = node_cpus < 0 ? node_cpus : fdt_first_child(f, node_cpus); n >= 0; n = fdt_next_sibling(f, n)) {
        const char* type = fdt_getprop_string(f, n, "device_type");
        struct fdt_region reg;
        if (!type || strncmp(type, "cpu", 4) != 0 || fdt_reg(f, n, 0, &reg) != 0
            || reg.base == cpus[0].arch.hart_id) {
            continue;
        }
        // fdt_dev.c's rules (status, an MMU, the A extension);
        // on the JH7110 they leave out hart 0, the S7 monitor core.
        const char* unfit = fdt_hart_unfit(f, n);
        if (!unfit && next == SAMOS_MAX_CPUS) {
            unfit = "SAMOS_MAX_CPUS reached";
        }
        if (unfit) {
            serial_write("smp: hart ");
            serial_write(itoa((int)reg.base));
            serial_write(" skipped: ");
            serial_write(unfit);
            serial_write("\n");
            continue;
        }
        if (start_hart(reg.base, next, late_test)) {
            next++;
        }
        late_test = false;
    }
    serial_write("smp: ");
    serial_write(itoa((int)cpu_online_count));
    serial_write(" cpus online\n");
    boot_marker_set(BM_STAGE_SMP_CPUS, cpu_online_count);
    return (int)cpu_online_count;
}

// ------------------------------------------------------------------
// The panic stop and halt.
// ------------------------------------------------------------------

// Panic (x86: an NMI to every other CPU). No NMI here: an
// IPI stops a hart that has interrupts on (software_interrupt, trap.c,
// calls riscv64_stop_poll), and a hart spinning with them off stops in
// cpu_relax, which every spin loop calls (arch_impl.h). A hart running
// other code with interrupts off stops at its next spin or interrupt.
bool arch_stop_other_cpus(void)
{
    riscv64_stop_harts = this_cpu()->index + 1;
    smp_mb();
    for (int i = 0; i < SAMOS_MAX_CPUS; i++) {
        if (&cpus[i] != this_cpu() && cpus[i].online) {
            sbi_send_ipi(1, cpus[i].arch.hart_id);
        }
    }
    return true;
}

void riscv64_stop_poll(void)
{
    smp_nmi_stop();
}

// A hart stopped by another's panic parks without a word (riscv64_halt
// prints and, with samos.test, powers QEMU off: the panicking hart's
// job).
void arch_halt(void)
{
    if (riscv64_stop_harts && riscv64_stop_harts != this_cpu()->index + 1) {
        riscv64_park();
    }
    riscv64_halt();
}
