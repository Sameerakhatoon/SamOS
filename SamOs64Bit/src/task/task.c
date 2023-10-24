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
//
// task.c is in the build (Makefile FILES) and has been since the
// L43/L44 prerequisites landed.
//
// One kernel stack (TSS rsp0) serves every task: a task never sleeps
// inside the kernel. A syscall that must wait marks its task BLOCKED
// with the saved rip moved back onto its int 0x80, calls task_next,
// and runs the whole syscall again when it is woken.

#include "task.h"
#include "process.h"
#include "kernel.h"
#include "status.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "string/string.h"
#include "idt/idt.h"
#include "loader/formats/elfloader.h"  // L65 - elf loader back

static int  task_init(struct task* task, struct process* process);
static void task_list_remove(struct task* task);
static void task_run_queue_remove(struct task* task);

// Current task that is running. NULL while the CPU idles.
struct task* current_task = 0;

// Doubly linked list of tasks.
struct task* task_tail = 0;
struct task* task_head = 0;

// FIFO of READY tasks.
static struct task* run_queue_head = 0;
static struct task* run_queue_tail = 0;

// int 0x80 is CD 80: moving rip back by its length runs it again.
#define TASK_INT80_LENGTH 2

struct task* task_current(){
    return current_task;
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

    if(task_head == 0){
        task_head    = task;
        task_tail    = task;
    } else {
        task_tail->next = task;
        task->prev      = task_tail;
        task_tail       = task;
    }
    task_ready(task);

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
    if(!current_task || !current_task->next){
        return task_head;
    }
    return current_task->next;
}

static void task_list_remove(struct task* task){
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
    if(task == current_task){
        current_task = 0;
    }
}

int task_free(struct task* task){
    // Lecture 113 - paging_desc lives on the process now, so the
    // free path moves there. task_free only releases the task
    // record itself; the descriptor is torn down when the
    // process exits.
    task_run_queue_remove(task);
    task_list_remove(task);
    kfree(task);
    return 0;
}

int task_switch(struct task* task){
    current_task = task;
    paging_switch(task->process->paging_desc);
    return 0;
}

// Lecture 40 + 113 - paging_desc accessor forwards through the
// owning process.
struct paging_desc* task_paging_desc(struct task* task){
    return task->process->paging_desc;
}

struct paging_desc* task_current_paging_desc(){
    if(!current_task){
        panic("task_current_paging_desc: no current task\n");
    }
    return task_paging_desc(current_task);
}

// Every loaded task is already on the run queue; run the
// first one, or idle if there is none.
void task_run_first_ever_task(){
    task_next();
}

// make `task` current and iretq into it with its own
// FPU/SSE state. Nothing between the fxrstor and the iretq may touch
// an x87 or SSE register; the kernel is built -mgeneral-regs-only.
void task_enter(struct task* task){
    task_run_queue_remove(task);
    task->state        = TASK_RUNNING;
    task->block_reason = TASK_BLOCK_NONE;
    task_switch(task);
    __asm__ volatile("fxrstor64 %0" : : "m"(task->fpu_state));
    task_return(&task->registers);
}

int task_page(){
    user_registers();
    task_switch(current_task);
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
    struct task* task = task_current();
    task_save_state(task, frame);
}

// ------------------------------------------------------------------
// Run queue.
// ------------------------------------------------------------------

static void task_run_queue_remove(struct task* task){
    struct task* prev = NULL;
    for(struct task* t = run_queue_head; t; prev = t, t = t->run_next){
        if(t != task){
            continue;
        }
        if(prev){
            prev->run_next = t->run_next;
        } else {
            run_queue_head = t->run_next;
        }
        if(run_queue_tail == t){
            run_queue_tail = prev;
        }
        t->run_next = NULL;
        return;
    }
}

bool task_run_queue_empty(void){
    return run_queue_head == NULL;
}

// Put the task at the back of the run queue (once).
void task_ready(struct task* task){
    if(task->state == TASK_READY && (task->run_next || run_queue_tail == task)){
        return;
    }
    task->state        = TASK_READY;
    task->block_reason = TASK_BLOCK_NONE;
    task->run_next     = NULL;
    if(run_queue_tail){
        run_queue_tail->run_next = task;
    } else {
        run_queue_head = task;
    }
    run_queue_tail = task;
}

// A task that waited (a key, a child, a deadline) goes to the front:
// the next tick, at most 10 ms away, runs it, however many CPU-bound
// tasks are queued.
void task_wake(struct task* task){
    if(!task || (task->state != TASK_BLOCKED && task->state != TASK_SLEEPING)){
        return;
    }
    task->state        = TASK_READY;
    task->block_reason = TASK_BLOCK_NONE;
    task->run_next     = run_queue_head;
    run_queue_head     = task;
    if(!run_queue_tail){
        run_queue_tail = task;
    }
}

// Called from a syscall on the current task: it leaves the CPU until
// task_wake, then executes the same int 0x80 again (the saved rax is
// still the syscall number, the arguments are still in place).
void task_block(struct task* task, int reason){
    task_run_queue_remove(task);
    task->state             = TASK_BLOCKED;
    task->block_reason      = reason;
    task->registers.rip    -= TASK_INT80_LENGTH;
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
// The task leaves the run queue; task_wake_sleepers puts it
// back.
void task_sleep(struct task* task, TIME_MICROSECONDS microseconds){
    task->sleeping.sleep_until_microseconds = tsc_microseconds() + microseconds;
    task_run_queue_remove(task);
    task->state = TASK_SLEEPING;
}

void task_wake_sleepers(void){
    TIME_MICROSECONDS now = tsc_microseconds();
    for(struct task* t = task_head; t; t = t->next){
        if(t->state == TASK_SLEEPING && t->sleeping.sleep_until_microseconds <= now){
            task_wake(t);
        }
    }
}

// The idle "task": the kernel itself on whatever kernel stack it is
// on, halting with interrupts on until an IRQ makes a task ready. An
// IRQ taken here is a ring-0 trap, so interrupt_handler saves no task
// state and no callback switches tasks; this loop picks the next one.
static void task_idle(void){
    static int announced = 0;
    current_task = 0;
    kernel_page();
    if(!task_head && !announced){
        print("sched: no process left, idling\n");
    }
    announced = task_head == 0;
    while(1){
        task_wake_sleepers();
        if(run_queue_head){
            return;
        }
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
}

// Pick the next READY task and run it; never returns. The caller has
// already put the current task wherever it belongs (re-queued it,
// blocked it, put it to sleep or freed it).
void task_next(){
    while(!run_queue_head){
        task_idle();
    }
    task_enter(run_queue_head);
}

// IRQ0 at 100 Hz (idt_clock). Wakes sleepers on every tick;
// from ring 3 the running task goes to the back of the queue if
// anything else is ready. interrupt_handler has saved its state.
void task_tick(struct trap_frame* frame){
    task_wake_sleepers();
    if((frame->cs & 3) != 3 || !current_task || !run_queue_head){
        return;
    }
    task_ready(current_task);
    task_next();
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
    task->state         = TASK_BLOCKED;   // not queued yet

    // Power-on FPU/SSE state: x87 control word 0x37F
    // and MXCSR 0x1F80 (all exceptions masked), registers zero.
    *(uint16_t*)&task->fpu_state[0]  = 0x037F;
    *(uint32_t*)&task->fpu_state[24] = 0x1F80;

    return 0;
}
