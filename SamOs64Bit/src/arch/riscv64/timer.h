#ifndef SAMOS_RISCV64_TIMER_H
#define SAMOS_RISCV64_TIMER_H

// The periodic tick, riscv64's LAPIC timer / PIT.
//
// The time CSR counts at the device tree's timebase-frequency. A
// deadline raises the supervisor timer interrupt: written straight to
// stimecmp when the hart has Sstc, else through SBI set_timer (an
// ecall per tick, OpenSBI programs the M-mode mtimecmp). Deadlines
// are absolute and advance by a fixed interval, so ticks do not
// drift with interrupt latency.

#include <stdbool.h>
#include <stdint.h>

struct fdt;

// Program the first deadline at `hz` and enable STIE (sstatus.SIE is
// the caller's). `cpu` is the boot hart's /cpus node. 0 or -1.
int      timer_init(const struct fdt* f, int cpu, unsigned hz);
// Called on every supervisor timer interrupt: count, next deadline.
void     timer_interrupt(void);
uint64_t timer_ticks(void);
uint64_t timer_timebase(void);
// Counter ticks per timer tick.
uint64_t timer_interval(void);
bool     timer_uses_sstc(void);

#endif
