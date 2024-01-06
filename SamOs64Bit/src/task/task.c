// Lecture 40 - task system rebuilt for 64-bit.
//
// What changed vs the 32-bit version:
//   - the saved register file is a struct trap_frame
//     (idt/idt.h) with all 15 general registers, plus the task's
//     FPU/SSE state.
//   - struct task uses paging_desc (4-level walk) instead of
//     paging_4gb_chunk (which built a 2-level 4 GiB identity
//     map). task_init calls paging_desc_new(PAGING_MAP_LEVEL_4).
//   - task_free uses paging_desc_free (lands in L43).
//   - copy_string_from_task (remap a kernel scratch page
//     into the task, switch CR3, strncpy) is gone; syscalls use
//     uaccess.c.
//   - a run queue with ready, blocked and sleeping states, an
//     idle loop that halts, and preemption from the 100 Hz tick.
//     The book's task_next rotated through every task and
//     busy-waited (udelay with interrupts off) when all of them were
//     asleep, and panicked when none was left. task_get_stack_item
//     is gone: arguments come in registers (isr80h_arg).
//   - ELF entry points are supported again since the L65
//     elfloader.
//   - a run queue and a sleep list per CPU, one sched
//     lock over all of them, work stealing, a per-CPU idle loop, and
//     kills carried out by the victim's own CPU.
//
// Kernel stacks: one per CPU (its TSS rsp0), not one per task.
// The kernel never keeps a task's state on a stack across a switch:
// everything is in task->registers by the time a trap handler calls
// task_next, and task_enter leaves the stack behind with an iretq.
// So a task can resume on any CPU. A syscall that must wait marks
// its task BLOCKED with the saved rip moved back onto its int 0x80,
// calls task_next, and runs the whole syscall again when it is
// woken, as on one CPU.

#include "task.h"
#include "process.h"
#include "kernel.h"
#include "status.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "string/string.h"
#include "idt/idt.h"
#include "cpu/percpu.h"
#include "cpu/smp.h"
#include "lib/spinlock/spinlock.h"
#include "arch/x86_64/atomic.h"
#include "io/serial.h"
#include "loader/formats/elfloader.h"  // L65 - elf loader back

static int  task_init(struct task* task, struct process* process);

// Every task, in creation order. Under sched_lock, like every run
// queue, sleep list and task state.
static struct task* task_tail = 0;
static struct task* task_head = 0;
static uint32_t     task_total = 0;
static bool         task_ever = false;   // a task was ever started
static volatile uint32_t idle_announced = 0;

static struct spinlock sched_lock = SPINLOCK_INIT("sched", LOCK_LEVEL_SCHED, 0);

// int 0x80 is CD 80: moving rip back by its length runs it again.
#define TASK_INT80_LENGTH 2

struct task* task_current(){
    return this_cpu()->current;
}

struct task* task_new(struct process* process){
    int res = 0;
    struct task* task = kzalloc(sizeof(struct task));
    if(!task){
        res = -ENOMEM;
        goto out;
    }

    res = task_init(task, process);
    if(res != SAMOS_ALL_OK){
        goto out;
    }

    spin_lock(&sched_lock);
    if(task_head == 0){
        task_head    = task;
        task_tail    = task;
    } else {
        task_tail->next = task;
        task->prev      = task_tail;
        task_tail       = task;
    }
    task_total++;
    spin_unlock(&sched_lock);

out:
    if(ISERR(res)){
        if(task){
            kfree(task);
        }
        return ERROR(res);
    }
    return task;
}

// No current task gives the head (NULL when the ring is
// empty). The book dereferenced current_task anyway and read 0 from
// page 0, which was identity-mapped; it is not any more.
struct task* task_get_next(){
    struct task* current = task_current();
    if(!current || !current->next){
        return task_head;
    }
    return current->next;
}

// ------------------------------------------------------------------
// Queues (sched_lock held).
// ------------------------------------------------------------------

static void rq_push_back(struct cpu* c, struct task* t){
    t->cpu      = c;
    t->run_next = NULL;
    if(c->run_tail){
        c->run_tail->run_next = t;
    } else {
        c->run_head = t;
    }
    c->run_tail = t;
    c->run_count++;
}

static void rq_push_front(struct cpu* c, struct task* t){
    t->cpu      = c;
    t->run_next = c->run_head;
    c->run_head = t;
    if(!c->run_tail){
        c->run_tail = t;
    }
    c->run_count++;
}

static struct task* rq_pop(struct cpu* c){
    struct task* t = c->run_head;
    if(!t){
        return NULL;
    }
    c->run_head = t->run_next;
    if(!c->run_head){
        c->run_tail = NULL;
    }
    t->run_next = NULL;
    c->run_count--;
    return t;
}

static void rq_remove(struct cpu* c, struct task* task){
    struct task* prev = NULL;
    for(struct task* t = c->run_head; t; prev = t, t = t->run_next){
        if(t != task){
            continue;
        }
        if(prev){
            prev->run_next = t->run_next;
        } else {
            c->run_head = t->run_next;
        }
        if(c->run_tail == t){
            c->run_tail = prev;
        }
        t->run_next = NULL;
        c->run_count--;
        return;
    }
}

static void sleep_remove(struct cpu* c, struct task* task){
    for(struct task** link = &c->sleep_head; *link; link = &(*link)->run_next){
        if(*link == task){
            *link = task->run_next;
            task->run_next = NULL;
            return;
        }
    }
}

// Take the task off whatever list holds it.
static void task_unqueue(struct task* t){
    if(!t->cpu){
        return;
    }
    if(t->state == TASK_READY){
        rq_remove(t->cpu, t);
    } else if(t->state == TASK_SLEEPING){
        sleep_remove(t->cpu, t);
    }
}

// The online CPU with the least work, this one on a tie.
static struct cpu* task_pick_cpu(void){
    struct cpu* me = this_cpu();
    struct cpu* best = me;
    uint32_t best_load = me->run_count + (me->current ? 1 : 0);
    for(int i = 0; i < SAMOS_MAX_CPUS; i++){
        struct cpu* c = &cpus[i];
        uint32_t load = c->run_count + (c->current ? 1 : 0);
        if(c->online && load < best_load){
            best = c;
            best_load = load;
        }
    }
    return best;
}

// The CPU with the longest queue other than `me`, if it has at
// least `min` tasks waiting.
static struct cpu* task_busiest(struct cpu* me, uint32_t min){
    struct cpu* best = NULL;
    for(int i = 0; i < SAMOS_MAX_CPUS; i++){
        struct cpu* c = &cpus[i];
        if(c != me && c->online && c->run_count >= min &&
           (!best || c->run_count > best->run_count)){
            best = c;
        }
    }
    return best;
}

// Work stealing: the oldest READY task of the busiest other CPU.
static struct task* task_steal(struct cpu* me){
    struct cpu* victim = task_busiest(me, 1);
    if(!victim){
        return NULL;
    }
    struct task* t = rq_pop(victim);
    if(t){
        t->cpu = me;
        me->steals++;
    }
    return t;
}

// ------------------------------------------------------------------
// Task lifetime.
// ------------------------------------------------------------------

int task_free(struct task* task){
    // Lecture 113 - paging_desc lives on the process now, so the
    // free path moves there. task_free only releases the task
    // record itself; the descriptor is torn down when the
    // process exits.
    spin_lock(&sched_lock);
    task_unqueue(task);
    if(task->prev){
        task->prev->next = task->next;
    }
    if(task->next){
        task->next->prev = task->prev;
    }
    if(task == task_head){
        task_head = task->next;
    }
    if(task == task_tail){
        task_tail = task->prev;
    }
    task_total--;
    if(this_cpu()->current == task){
        this_cpu()->current = NULL;
    }
    spin_unlock(&sched_lock);
    kfree(task);
    return 0;
}

// user_desc is set before CR3 changes: a CPU that edits this
// process's mappings either sees it here and sends a shootdown, or
// edited them before this CPU's TLB could hold them.
int task_switch(struct task* task){
    struct cpu* c = this_cpu();
    c->current   = task;
    c->user_desc = task->process->paging_desc;
    smp_mb();
    paging_switch(task->process->paging_desc);
    return 0;
}

// Lecture 40 + 113 - paging_desc accessor forwards through the
// owning process.
struct paging_desc* task_paging_desc(struct task* task){
    return task->process->paging_desc;
}

struct paging_desc* task_current_paging_desc(){
    if(!task_current()){
        panic("task_current_paging_desc: no current task\n");
    }
    return task_paging_desc(task_current());
}

// Every loaded task is already on a run queue; run the first
// one, or idle if there is none.
void task_run_first_ever_task(){
    task_next();
}

// Iretq into `task` with its own FPU/SSE state. Nothing
// between the fxrstor and the iretq may touch an x87 or SSE register;
// the kernel is built -mgeneral-regs-only.
static void task_enter_now(struct task* task){
    task_switch(task);
    serial_flush();
    __asm__ volatile("fxrstor64 %0" : : "m"(task->fpu_state));
    task_return(&task->registers);
}

// The task becomes this CPU's current one. False when it had been
// killed: then it is gone and the caller must pick another.
static bool task_claim(struct task* task){
    struct cpu* me = this_cpu();
    spin_lock(&sched_lock);
    task_unqueue(task);
    task->state        = TASK_RUNNING;
    task->block_reason = TASK_BLOCK_NONE;
    task->cpu          = me;
    me->current        = task;
    me->tasks_entered++;
    int kill = task->kill_pending;
    spin_unlock(&sched_lock);
    if(kill){
        process_exit(task->process, task->kill_status);
        return false;
    }
    return true;
}

// Run `task` now on this CPU (a child that was just started).
void task_enter(struct task* task){
    if(!task_claim(task)){
        task_next();
    }
    task_enter_now(task);
}

int task_page(){
    user_registers();
    task_switch(task_current());
    serial_flush();
    return 0;
}

// Copy the whole trap frame, then the FPU/SSE registers,
// which still hold the user's values: the trap path is integer-only.
void task_save_state(struct task* task, struct trap_frame* frame){
    task->registers = *frame;
    __asm__ volatile("fxsave64 %0" : "=m"(task->fpu_state));
}

void task_current_save_state(struct trap_frame* frame){
    if(!task_current()){
        panic("No current task to save\n");
    }
    task_save_state(task_current(), frame);
}

// ------------------------------------------------------------------
// Run queue. One per CPU.
// ------------------------------------------------------------------

bool task_run_queue_empty(void){
    return this_cpu()->run_count == 0;
}

// A NEW task goes to the CPU with the least work; an idle one is
// woken by the reschedule IPI, a busy one preempts its task for it.
void task_start(struct task* task){
    spin_lock(&sched_lock);
    task_ever = true;
    struct cpu* c = task_pick_cpu();
    task->state        = TASK_READY;
    task->block_reason = TASK_BLOCK_NONE;
    rq_push_back(c, task);
    spin_unlock(&sched_lock);
    smp_send_resched(c);
}

// Put a task that is not on any list at the back of this CPU's
// queue (the preempted current task, a caller that yields).
void task_ready(struct task* task){
    spin_lock(&sched_lock);
    if(task->state != TASK_READY){
        task->state        = TASK_READY;
        task->block_reason = TASK_BLOCK_NONE;
        rq_push_back(this_cpu(), task);
    }
    spin_unlock(&sched_lock);
}

// sched_lock held. Returns the CPU to kick, or NULL.
static struct cpu* task_wake_locked(struct task* task){
    if(task->state != TASK_BLOCKED && task->state != TASK_SLEEPING){
        return NULL;
    }
    struct cpu* c = task->cpu ? task->cpu : this_cpu();
    if(task->state == TASK_SLEEPING){
        sleep_remove(c, task);
    }
    task->state        = TASK_READY;
    task->block_reason = TASK_BLOCK_NONE;
    rq_push_front(c, task);
    return c;
}

// A task that waited (a key, a child, a deadline) goes to the front
// of the queue of the CPU it last ran on, which the reschedule IPI
// tells at once.
void task_wake(struct task* task){
    if(!task){
        return;
    }
    spin_lock(&sched_lock);
    struct cpu* c = task_wake_locked(task);
    spin_unlock(&sched_lock);
    if(c){
        smp_send_resched(c);
    }
}

// Called from a syscall on the current task: it leaves the CPU until
// task_wake, then executes the same int 0x80 again (the saved rax is
// still the syscall number, the arguments are still in place). A task
// another CPU has killed meanwhile stays runnable, so it dies.
void task_block(struct task* task, int reason){
    spin_lock(&sched_lock);
    task->registers.rip -= TASK_INT80_LENGTH;
    if(task->kill_pending){
        task->state = TASK_READY;
        rq_push_back(this_cpu(), task);
    } else {
        task->state        = TASK_BLOCKED;
        task->block_reason = reason;
    }
    spin_unlock(&sched_lock);
}

// Lecture 197 - sleeping if the deadline TSC hasn't been reached.
bool task_asleep(struct task* task){
    return task->state == TASK_SLEEPING &&
           task->sleeping.sleep_until_microseconds > tsc_microseconds();
}

// Lecture 197 / 198 - park the task until tsc_microseconds()
// exceeds the deadline. L197 shipped the deadline as
// `tsc_microseconds() * microseconds` (multiplication, bug).
// L198 part 2 fixes it to `+ microseconds`. SamOs follows.
// On this CPU's sleep list; its tick wakes it.
void task_sleep(struct task* task, TIME_MICROSECONDS microseconds){
    struct cpu* me = this_cpu();
    spin_lock(&sched_lock);
    task->sleeping.sleep_until_microseconds = tsc_microseconds() + microseconds;
    if(task->kill_pending){
        task->state = TASK_READY;
        rq_push_back(me, task);
    } else {
        task->state    = TASK_SLEEPING;
        task->cpu      = me;
        task->run_next = me->sleep_head;
        me->sleep_head = task;
    }
    spin_unlock(&sched_lock);
}

void task_wake_sleepers(void){
    struct cpu* me = this_cpu();
    if(!me->sleep_head){
        return;
    }
    TIME_MICROSECONDS now = tsc_microseconds();
    spin_lock(&sched_lock);
    struct task** link = &me->sleep_head;
    while(*link){
        struct task* t = *link;
        if(t->sleeping.sleep_until_microseconds <= now){
            *link = t->run_next;
            t->run_next     = NULL;
            t->state        = TASK_READY;
            t->block_reason = TASK_BLOCK_NONE;
            rq_push_front(me, t);
        } else {
            link = &t->run_next;
        }
    }
    spin_unlock(&sched_lock);
}

// Kill `task` with `status`. A task is only ever torn
// down by the CPU that runs it: this marks it, wakes it if it waits,
// and kicks the CPU it is on; that CPU ends it when the task next
// enters the kernel (task_check_killed) or is picked to run
// (task_claim). Caller holds the process lock.
void task_kill(struct task* task, int status){
    spin_lock(&sched_lock);
    struct cpu* kick = task->cpu;
    if(!task->kill_pending){
        task->kill_pending = 1;
        task->kill_status  = status;
    }
    if(task->state == TASK_BLOCKED || task->state == TASK_SLEEPING){
        kick = task_wake_locked(task);
    }
    spin_unlock(&sched_lock);
    if(kick){
        smp_send_resched(kick);
    }
}

void task_check_killed(void){
    struct task* t = task_current();
    if(t && t->kill_pending){
        process_exit(t->process, t->kill_status);
        task_next();
    }
}

// The idle loop of one CPU: the kernel on this CPU's stack, halting
// with interrupts on until an IRQ or IPI makes a task ready. An IRQ
// taken here is a ring-0 trap, so interrupt_handler saves no task
// state and no callback switches tasks; task_next picks the next one.
static void task_idle_once(bool none_left){
    this_cpu()->current = NULL;
    kernel_page();
    if(none_left && task_ever && !atomic_xchg(&idle_announced, 1)){
        print("sched: no process left, idling\n");
    }
    if(!none_left){
        idle_announced = 0;
    }
    serial_flush();
    __asm__ volatile("sti; hlt; cli" ::: "memory");
    task_wake_sleepers();
}

// Pick the next READY task and run it; never returns. The caller has
// already put the current task wherever it belongs (re-queued it,
// blocked it, put it to sleep or freed it). With an empty queue this
// CPU steals from the busiest one, else it idles.
void task_next(){
    struct cpu* me = this_cpu();
    me->current = NULL;
    while(1){
        spin_lock(&sched_lock);
        struct task* t = rq_pop(me);
        if(!t){
            t = task_steal(me);
        }
        bool none_left = task_total == 0;
        spin_unlock(&sched_lock);
        if(t){
            // rq_pop left it READY on no list; nothing but its own
            // exit frees a task, so it is still there.
            if(task_claim(t)){
                task_enter_now(t);
            }
            continue;
        }
        task_idle_once(none_left);
    }
}

// Preempt the current ring-3 task if this CPU has another one ready.
static void task_preempt(struct trap_frame* frame){
    struct cpu* me = this_cpu();
    if((frame->cs & 3) != 3 || !me->current || !me->run_count){
        return;
    }
    task_ready(me->current);
    task_next();
}

// The 100 Hz tick of this CPU (idt_clock). Wakes this CPU's
// sleepers; from ring 3 the running task goes to the back of the
// queue if anything else is ready. Counts busy and idle ticks,
// and pulls a task from a CPU that has two more queued than this one.
void task_tick(struct trap_frame* frame){
    struct cpu* me = this_cpu();
    if(me->current){
        me->ticks_busy++;
    } else {
        me->ticks_idle++;
    }
    task_wake_sleepers();
    if(smp_active()){
        spin_lock(&sched_lock);
        struct cpu* victim = task_busiest(me, me->run_count + 2);
        struct task* t = victim ? rq_pop(victim) : NULL;
        if(t){
            rq_push_back(me, t);
            me->steals++;
        }
        spin_unlock(&sched_lock);
    }
    task_preempt(frame);
}

// The reschedule IPI: a task was started or woken for this
// CPU. Idle, the halt returns and task_next looks; in ring 3 the
// woken task runs now instead of at the next tick.
void task_resched(struct trap_frame* frame){
    task_preempt(frame);
}

static int task_init(struct task* task, struct process* process){
    memset(task, 0, sizeof(struct task));

    // Lecture 113 - the paging descriptor and the e820 mapping
    // are set up by process_load_for_slot before task_new is
    // called. task_init only sees the process's already-live
    // descriptor through process->paging_desc.


    task->registers.rip = SAMOS_PROGRAM_VIRTUAL_ADDRESS;
    if(process->filetype == PROCESS_FILETYPE_ELF){
        // L65 - the ELF64 loader is in. Pull the entry point
        // out of the ELF header.
        task->registers.rip = elf_header(process->elf_file)->e_entry;
    }

    task->registers.ss  = USER_DATA_SEGMENT;
    task->registers.cs  = USER_CODE_SEGMENT;
    task->registers.rsp = SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
    task->process       = process;
    task->state         = TASK_NEW;   // task_start queues it

    // Power-on FPU/SSE state: x87 control word 0x37F
    // and MXCSR 0x1F80 (all exceptions masked), registers zero.
    *(uint16_t*)&task->fpu_state[0]  = 0x037F;
    *(uint32_t*)&task->fpu_state[24] = 0x1F80;

    return 0;
}
