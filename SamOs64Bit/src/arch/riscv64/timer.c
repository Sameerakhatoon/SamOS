// Timer: Sstc stimecmp when the hart has it, SBI set_timer
// otherwise.

#include "arch/riscv64/timer.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/sbi.h"
#include "arch/riscv64/trap.h"
#include "firmware/fdt.h"
#include "string/string.h"

// stimecmp is CSR 0x14d (by number, as csr_read stringifies its
// argument and the assembler needs _sstc for the name).
static bool use_sstc;
static uint64_t timebase, interval, next_deadline;
static volatile uint64_t ticks;

static void set_deadline(uint64_t t)
{
    if (use_sstc) {
        csr_write(0x14d, t);
    } else {
        sbi_set_timer(t);
    }
}

// True when the hart's ISA lists `ext`: riscv,isa-extensions (a string
// list, in newer trees), else riscv,isa ("rv64imafdc_zicsr_sstc", the
// multi-letter extensions after an '_').
static bool isa_has(const struct fdt* f, int cpu, const char* ext)
{
    int n = strlen(ext);
    uint32_t len;
    const char* list = fdt_getprop(f, cpu, "riscv,isa-extensions", &len);
    if (list) {
        for (uint32_t i = 0; i < len; i += strnlen(list + i, len - i) + 1) {
            if (len - i > (uint32_t)n && strncmp(list + i, ext, n + 1) == 0) {
                return true;
            }
        }
        return false;
    }
    const char* isa = fdt_getprop_string(f, cpu, "riscv,isa");
    for (const char* s = isa; s && *s; s++) {
        if (s > isa && s[-1] == '_' && strncmp(s, ext, n) == 0 && (s[n] == '_' || s[n] == 0)) {
            return true;
        }
    }
    return false;
}

// The DT can list Sstc while M-mode has not set menvcfg.STCE, which
// makes stimecmp an illegal instruction in S-mode; so try it too.
static bool stimecmp_works(void)
{
    trap_probe_begin();
    (void)csr_read(0x14d);
    return trap_probe_end() == 0;
}

uint64_t timer_probe_timebase(const struct fdt* f, int cpu)
{
    uint64_t t = fdt_getprop_u32(f, cpu, "timebase-frequency", 0);
    return t ? t : fdt_getprop_u32(f, fdt_parent(f, cpu), "timebase-frequency", 0);
}

int timer_init(const struct fdt* f, int cpu, unsigned hz)
{
    timebase = timer_probe_timebase(f, cpu);
    if (timebase == 0 || hz == 0) {
        console_printf("timer: no timebase-frequency\n");
        return -1;
    }
    // Both tests always run (and print), so a boot without Sstc shows
    // the probe's illegal-instruction fixup working.
    bool dt_sstc = isa_has(f, cpu, "sstc");
    bool works = stimecmp_works();
    console_printf("timer: device tree %s sstc, stimecmp %s\n", dt_sstc ? "lists" : "does not list",
                   works ? "works" : "traps");
    use_sstc = dt_sstc && works;
    if (!use_sstc && !sbi_probe(SBI_EXT_TIME)) {
        console_printf("timer: no sstc and no SBI TIME extension\n");
        return -1;
    }
    interval = timebase / hz;
    next_deadline = rdtime() + interval;
    set_deadline(next_deadline);
    csr_set(sie, SIE_STIE);
    console_printf("timer: %s, %u Hz, timebase %lu Hz, interval %lu\n",
                   use_sstc ? "sstc stimecmp" : "sbi set_timer", hz, timebase, interval);
    return 0;
}

void timer_interrupt(void)
{
    ticks++;
    next_deadline += interval;
    set_deadline(next_deadline);
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint64_t timer_timebase(void)
{
    return timebase;
}

uint64_t timer_interval(void)
{
    return interval;
}

bool timer_uses_sstc(void)
{
    return use_sstc;
}
