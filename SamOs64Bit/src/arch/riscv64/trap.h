#ifndef SAMOS_RISCV64_TRAP_H
#define SAMOS_RISCV64_TRAP_H

// One frame for every trap, the riscv64 struct trap_frame.
//
// trap_entry (boot.S) builds it on the kernel stack: x1..x31 with xn
// at (n - 1) * 8, then the trap CSRs. The hardware pushes nothing, so
// unlike x86 the whole frame is software's; the layout is fixed by the
// assembly offsets below. A thread that is not running is a saved
// trap_frame too (kthread.c), as x86's task->registers is.

#define TRAP_FRAME_SIZE 288
#define TF_SEPC    248
#define TF_SSTATUS 256
#define TF_SCAUSE  264
#define TF_STVAL   272

#ifndef __ASSEMBLER__
#include <stdint.h>

struct trap_frame {
    uint64_t x[31];     // x[n - 1] is xn: see the TF_ indexes
    uint64_t sepc;      // where sret resumes
    uint64_t sstatus;   // SPP: the trap came from S-mode; SPIE: SIE before it
    uint64_t scause;
    uint64_t stval;
    uint64_t pad;       // keeps sp 16-byte aligned
};

_Static_assert(sizeof(struct trap_frame) == TRAP_FRAME_SIZE, "trap_frame size");
_Static_assert(__builtin_offsetof(struct trap_frame, sepc) == TF_SEPC, "sepc offset");
_Static_assert(__builtin_offsetof(struct trap_frame, stval) == TF_STVAL, "stval offset");

// Indexes into x[] by ABI name.
enum { TF_RA = 0, TF_SP = 1, TF_GP = 2, TF_TP = 3, TF_A0 = 9, TF_A7 = 16 };

typedef void (*INTERRUPT_CALLBACK_FUNCTION)(struct trap_frame* frame);

// Called by trap_entry; returns the frame to restore, which is another
// thread's after a preemptive switch.
struct trap_frame* riscv64_trap(struct trap_frame* frame);

// Exception fixup for feature probes: between begin and end an illegal
// instruction is skipped and counted instead of dumped. end returns
// the count (0 = the probed instruction worked).
void     trap_probe_begin(void);
unsigned trap_probe_end(void);

// The faulting instructions of the trap tests (boot.S), each a
// function so its address is the sepc the dump must show.
void trap_test_illegal(void);
uint64_t trap_test_load(uint64_t addr);
void trap_test_ebreak(void);
#endif

#endif
