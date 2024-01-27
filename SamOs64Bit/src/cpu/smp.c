// IPIs, TLB shootdown, cross-CPU calls, panic stop.
// Not in the book, which runs on the BSP alone.
//
// The portable half. Starting the other CPUs, sending an IPI
// and stopping CPUs on a panic are the arch's (arch_start_cpus,
// arch_send_ipi, arch_stop_other_cpus; x86: arch/x86_64/smp.c).

#include "smp.h"
#include "percpu.h"
#include "arch/arch.h"
#include "lib/spinlock/spinlock.h"
#include "io/serial.h"
#include "task/task.h"
#include "string/string.h"
#include "config.h"
#include "kernel.h"

struct cpu cpus[SAMOS_MAX_CPUS];
volatile uint32_t cpu_online_count = 1;

// One shootdown or cross-CPU call at a time.
static struct spinlock smp_lock  = SPINLOCK_INIT("smp", LOCK_LEVEL_SMP, 0);
static struct spinlock call_lock = SPINLOCK_INIT("smp_call", LOCK_LEVEL_SMP, 0);
static volatile uint64_t tlb_va;
static volatile uint32_t tlb_acks;

static volatile uint32_t panicking;
static volatile uint32_t panic_cpu;
static volatile uint32_t stopped;

// ------------------------------------------------------------------
// IPIs.
// ------------------------------------------------------------------

void smp_send_resched(struct cpu* target){
    if(target != this_cpu() && target->online){
        arch_send_ipi(target, ARCH_IPI_RESCHED);
    }
}

void smp_tlb_service(struct cpu* c){
    if(atomic_xchg(&c->tlb_pending, 0)){
        arch_tlb_flush_local(tlb_va);
        c->ipi_tlb++;
        atomic_dec(&tlb_acks);
    }
}

// Busy-wait for *v to become 0, answering requests meanwhile. A stuck
// target means a CPU spinning with interrupts off somewhere that does
// not poll: report it.
static void smp_wait_zero(volatile uint32_t* v, const char* what){
    struct cpu* me = this_cpu();
    uint64_t start = arch_time_us();
    while(atomic_load(v)){
        cpu_relax();
        smp_poll(me);
        if(SAMOS_LOCK_DEBUG && arch_time_us() - start > SAMOS_LOCK_TIMEOUT_US){
            serial_write_unlocked("smp: no answer to ");
            serial_write_unlocked(what);
            panic("smp: a CPU did not answer an IPI\n");
        }
    }
}

void smp_tlb_shootdown(struct paging_desc* desc, uint64_t va){
    if(!smp_active()){
        return;
    }
    bool kernel_half = va >= SAMOS_DIRECT_MAP_BASE;
    struct cpu* me = this_cpu();
    spin_lock(&smp_lock);
    tlb_va = va & ~0xFFFull;
    uint32_t n = 0;
    bool target[SAMOS_MAX_CPUS] = {0};
    for(int i = 0; i < SAMOS_MAX_CPUS; i++){
        struct cpu* c = &cpus[i];
        if(c != me && c->online && (kernel_half || c->user_desc == desc)){
            target[i] = true;
            n++;
        }
    }
    tlb_acks = n;
    for(int i = 0; i < SAMOS_MAX_CPUS; i++){
        if(target[i]){
            atomic_store(&cpus[i].tlb_pending, 1);
            arch_send_ipi(&cpus[i], ARCH_IPI_TLB);
        }
    }
    smp_wait_zero(&tlb_acks, "a TLB shootdown");
    spin_unlock(&smp_lock);
}

static void smp_call_run(void){
    struct cpu* c = this_cpu();
    void (*fn)(void*) = c->call_fn;
    if(!fn){
        return;
    }
    c->call_fn = 0;
    fn(c->call_arg);
    atomic_store(&c->call_done, 1);
}

void smp_call_async(struct cpu* target, void (*fn)(void*), void* arg){
    target->call_done = 0;
    target->call_arg  = arg;
    smp_wmb();
    target->call_fn   = fn;
    arch_send_ipi(target, ARCH_IPI_CALL);
}

void smp_call_wait(struct cpu* target){
    struct cpu* me = this_cpu();
    while(!atomic_load(&target->call_done)){
        cpu_relax();
        smp_poll(me);
    }
}

void smp_call(struct cpu* target, void (*fn)(void*), void* arg){
    if(target == this_cpu()){
        fn(arg);
        return;
    }
    spin_lock(&call_lock);
    smp_call_async(target, fn, arg);
    smp_call_wait(target);
    spin_unlock(&call_lock);
}

static void (*smp_input_fn)(void);

void smp_handle_ipi(enum arch_ipi ipi, struct trap_frame* frame){
    switch(ipi){
        case ARCH_IPI_TLB:
            smp_tlb_service(this_cpu());
            break;
        case ARCH_IPI_RESCHED:
            this_cpu()->ipi_resched++;
            task_resched(frame);
            break;
        case ARCH_IPI_CALL:
            smp_call_run();
            break;
        case ARCH_IPI_INPUT:
            if(smp_input_fn){
                smp_input_fn();
            }
            break;
    }
}

void smp_input_register(void (*fn)(void)){
    smp_input_fn = fn;
}

// ------------------------------------------------------------------
// Panic: every other CPU stops (arch_stop_other_cpus).
// ------------------------------------------------------------------

bool smp_panicking(void){
    return panicking != 0;
}

void smp_panic_begin(void){
    uint32_t me = this_cpu()->index;
    if(atomic_xchg(&panicking, 1)){
        if(panic_cpu != me){
            arch_halt();   // another CPU is already reporting
        }
        return;
    }
    panic_cpu = me;
    if(!smp_active() || !arch_stop_other_cpus()){
        return;
    }
    uint64_t end = arch_counter() + arch_counter_hz() / 10;
    while(stopped < cpu_online_count - 1 && arch_counter() < end){
        cpu_relax();
    }
    serial_write_unlocked("panic: stopped ");
    serial_write_unlocked(itoa((int)stopped));
    serial_write_unlocked(" other cpus\n");
}

bool smp_nmi_stop(void){
    if(!panicking || panic_cpu == this_cpu()->index){
        return false;
    }
    atomic_inc(&stopped);
    arch_halt();
}
