// Spinlocks with an order check and a deadlock timeout.
// See spinlock.h and docs/locking.md.

#include "spinlock.h"
#include "arch/x86_64/atomic.h"
#include "cpu/percpu.h"
#include "cpu/smp.h"
#include "io/tsc.h"
#include "io/serial.h"
#include "kernel.h"
#include "string/string.h"

extern uint64_t tsc_freq_val;

void spin_lock_init(struct spinlock* lock, const char* name, int level, bool recursive){
    lock->locked     = 0;
    lock->depth      = 0;
    lock->owner      = 0;
    lock->flags      = 0;
    lock->holder_rip = 0;
    lock->name       = name;
    lock->level      = (uint16_t)level;
    lock->recursive  = recursive ? 1 : 0;
}

static void spin_report(const char* what, struct spinlock* lock, struct spinlock* other){
    smp_panic_begin();
    serial_write_unlocked("\nspinlock: ");
    serial_write_unlocked(what);
    serial_write_unlocked(" ");
    serial_write_unlocked(lock->name);
    if(other){
        serial_write_unlocked(" while holding ");
        serial_write_unlocked(other->name);
    }
    serial_write_unlocked(" on cpu ");
    serial_write_unlocked(itoa((int)this_cpu()->index));
    if(lock->owner){
        serial_write_unlocked(", held by cpu ");
        serial_write_unlocked(itoa((int)lock->owner->index));
        serial_write_unlocked(" from ");
        serial_write_hex_unlocked((uint64_t)lock->holder_rip);
    }
    serial_write_unlocked("\n");
    panic("spinlock check failed\n");
}

int spin_held_level(void){
    struct cpu* c = this_cpu();
    int level = 0;
    for(int i = 0; i < c->held_count; i++){
        if(c->held[i]->level > level){
            level = c->held[i]->level;
        }
    }
    return level;
}

bool spin_held(struct spinlock* lock){
    return lock->locked && lock->owner == this_cpu();
}

// Out of line so __builtin_return_address(1) is the caller of
// spin_lock even at -O0.
static void __attribute__((noinline)) spin_wait(struct spinlock* lock, struct cpu* c){
    uint64_t start = read_tsc();
    uint64_t limit = tsc_freq_val ? (SAMOS_LOCK_TIMEOUT_US / 1000000) * tsc_freq_val : 0;
    uint32_t spins = 0;
    while(!atomic_cas(&lock->locked, 0, 1)){
        while(lock->locked){
            cpu_relax();
            // A CPU holding this lock may be waiting for us to flush
            // our TLB (smp.c); answer while we wait.
            smp_poll(c);
            if(SAMOS_LOCK_DEBUG && limit && ++spins % 1024 == 0 && read_tsc() - start > limit){
                spin_report("timeout waiting for", lock, 0);
            }
        }
    }
}

void spin_lock(struct spinlock* lock){
    uint64_t flags = irq_save();
    struct cpu* c = this_cpu();
    if(lock->owner == c && lock->locked){
        if(!lock->recursive){
            spin_report("recursive acquire of", lock, 0);
        }
        lock->depth++;
        return;
    }
    if(SAMOS_LOCK_DEBUG){
        for(int i = 0; i < c->held_count; i++){
            if(c->held[i]->level >= lock->level){
                spin_report("lock order: taking", lock, c->held[i]);
            }
        }
        if(c->held_count == CPU_MAX_HELD_LOCKS){
            spin_report("too many locks held, taking", lock, 0);
        }
    }
    if(!atomic_cas(&lock->locked, 0, 1)){
        spin_wait(lock, c);
    }
    lock->owner      = c;
    lock->depth      = 1;
    lock->flags      = flags;
    lock->holder_rip = __builtin_return_address(0);
    if(c->held_count < CPU_MAX_HELD_LOCKS){
        c->held[c->held_count++] = lock;
    }
}

void spin_unlock(struct spinlock* lock){
    struct cpu* c = this_cpu();
    if(SAMOS_LOCK_DEBUG && (lock->owner != c || !lock->locked)){
        spin_report("unlock by a cpu that does not hold", lock, 0);
    }
    if(--lock->depth){
        return;
    }
    for(int i = c->held_count - 1; i >= 0; i--){
        if(c->held[i] == lock){
            for(int j = i; j < c->held_count - 1; j++){
                c->held[j] = c->held[j + 1];
            }
            c->held_count--;
            break;
        }
    }
    uint64_t flags = lock->flags;
    lock->owner = 0;
    atomic_store(&lock->locked, 0);
    irq_restore(flags);
}
