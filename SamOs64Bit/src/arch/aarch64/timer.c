// Generic timer tick. See timer.h.
//
// The EL1 physical timer (CNTP_*) rather than the virtual one: with
// CNTVOFF_EL2 = 0 (boot.S) they count the same, and the physical one
// is the timer the DT lists for a kernel without a hypervisor. At EL2
// entry boot.S sets CNTHCTL_EL2.EL1PCEN so EL1 may use it.
#include "arch/aarch64/timer.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/sysreg.h"
#include "arch/aarch64/trap.h"

#define CNTP_CTL_ENABLE     (1u << 0)
#define TIMER_DT_PHYS_NS    1           // interrupts: secure, non-secure phys, virt, hyp
#define TIMER_INTID_DEFAULT 30          // PPI 14

static uint64_t freq;
static uint32_t intid;
static uint64_t period;
static volatile uint64_t ticks;
static void (*tick_hook)(struct trap_frame* f);

uint64_t counter_read(void)
{
    isb();      // no early read ahead of preceding instructions
    return sysreg_read("cntpct_el0");
}

uint64_t counter_hz(void)
{
    return freq;
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint32_t timer_intid(void)
{
    return intid;
}

int timer_init(const struct fdt* f)
{
    int node = fdt_find_compatible(f, -1, "arm,armv8-timer");
    if (node < 0)
        node = fdt_find_compatible(f, -1, "arm,armv7-timer");     // The BCM2837's (Pi 3)
    uint32_t dt_freq = node >= 0 ? fdt_getprop_u32(f, node, "clock-frequency", 0) : 0;
    int id = node >= 0 ? gic_dt_intid(f, node, TIMER_DT_PHYS_NS) : -1;

    freq = sysreg_read("cntfrq_el0") & 0xffffffff;
    const char* how = "CNTFRQ_EL0, no DT clock-frequency";
    if (dt_freq == freq) {
        how = "CNTFRQ_EL0, DT clock-frequency agrees";
    } else if (dt_freq) {
        // Linux's rule: a clock-frequency in the DT is there because
        // the firmware got CNTFRQ_EL0 wrong, so it wins.
        console_printf("timer: CNTFRQ_EL0 says %lu Hz\n", freq);
        how = "DT clock-frequency, CNTFRQ_EL0 differs";
        freq = dt_freq;
    }
    if (!freq) {
        console_printf("timer: no counter frequency (CNTFRQ_EL0 0, none in the DT)\n");
        return -1;
    }
    intid = id >= 0 ? (uint32_t)id : TIMER_INTID_DEFAULT;
    console_printf("timer: %lu Hz counter (%s), EL1 physical timer INTID %u (%s)\n", freq, how, intid,
                   id >= 0 ? "DT interrupts" : "default");
    return 0;
}

// Each CPU has its own timer: the next deadline is this CPU's
// compare value plus a period. `ticks` counts every CPU's.
static void timer_irq(struct trap_frame* f)
{
    sysreg_write("cntp_cval_el0", sysreg_read("cntp_cval_el0") + period);
    __atomic_add_fetch(&ticks, 1, __ATOMIC_RELAXED);
    if (tick_hook)
        tick_hook(f);
}

void timer_start_cpu(void)
{
    sysreg_write("cntp_cval_el0", counter_read() + period);
    sysreg_write("cntp_ctl_el0", CNTP_CTL_ENABLE);     // IMASK clear
    isb();
    gic_enable(intid);
}

void timer_start(uint32_t hz, void (*on_tick)(struct trap_frame* f))
{
    tick_hook = on_tick;
    period = freq / hz;
    irq_register(intid, timer_irq);
    timer_start_cpu();
    console_printf("timer: %u Hz tick, period %lu counts\n", hz, period);
}
