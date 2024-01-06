#ifndef SPINLOCK_H
#define SPINLOCK_H
// Spinlocks. Not in the book, which has one CPU and
// no locks at all.
//
// A spinlock is held with interrupts off: spin_lock saves RFLAGS and
// clears IF, spin_unlock of the outermost hold puts IF back. The
// kernel already runs every trap with IF clear (interrupt gates), so
// on SamOS this mostly documents the rule; it matters for the idle
// loop and for code a later milestone runs with interrupts on.
//
// Every lock has a level from docs/locking.md. A CPU may only take a
// lock whose level is above every lock it already holds; a recursive
// lock may be taken again by the CPU that holds it. With
// SAMOS_LOCK_DEBUG (on by default) both rules are checked on every
// acquire, so an inversion panics on the first run that takes the two
// locks in the wrong order, even on one CPU. A waiter that spins
// longer than SAMOS_LOCK_TIMEOUT_US prints the lock, the CPU holding
// it and the return address it was taken from, then panics.

#include <stdint.h>
#include <stdbool.h>

#ifndef SAMOS_LOCK_DEBUG
#define SAMOS_LOCK_DEBUG 1
#endif
#define SAMOS_LOCK_TIMEOUT_US  (30ull * 1000 * 1000)

// Lock levels, outermost first (docs/locking.md).
enum lock_level {
    LOCK_LEVEL_GFX     = 10,   // windows, graphics, terminals, mouse, screen output
    LOCK_LEVEL_PROCESS = 20,   // process table, pids, parents, zombies, keyboard rings
    LOCK_LEVEL_VFS     = 30,   // file descriptors, FAT, path lookups
    LOCK_LEVEL_DISK    = 40,   // every disk driver call and the sector caches
    LOCK_LEVEL_SCHED   = 50,   // run queues, sleep lists, task states
    LOCK_LEVEL_KMEM    = 60,   // PFA, multiheap, slabs, kernel-half page tables
    LOCK_LEVEL_IOAPIC  = 65,   // IOAPIC register pair
    LOCK_LEVEL_SMP     = 70,   // TLB shootdown and cross-CPU calls, one at a time
    LOCK_LEVEL_SERIAL  = 80,   // COM1
};

struct cpu;

struct spinlock {
    volatile uint32_t locked;
    uint32_t          depth;       // holds by the owner (recursive locks)
    struct cpu*       owner;
    uint64_t          flags;       // RFLAGS of the outermost acquire
    void*             holder_rip;  // where the outermost acquire was made
    const char*       name;
    uint16_t          level;
    uint8_t           recursive;
};

#define SPINLOCK_INIT(n, lvl, rec) { 0, 0, 0, 0, 0, (n), (lvl), (rec) }

void spin_lock_init(struct spinlock* lock, const char* name, int level, bool recursive);
void spin_lock(struct spinlock* lock);
void spin_unlock(struct spinlock* lock);
bool spin_held(struct spinlock* lock);   // by this CPU
// Highest level this CPU holds, 0 for none.
int  spin_held_level(void);

#endif
