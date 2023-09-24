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
//   - ELF entry points are supported again since the L65
//     elfloader.
//
// task.c is in the build (Makefile FILES) and has been since the
// L43/L44 prerequisites landed.

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
#include "uaccess.h"                   // Checked user access

static int  task_init(struct task* task, struct process* process);
static void task_list_remove(struct task* task);

// Current task that is running.
struct task* current_task = 0;

// Doubly linked list of tasks.
struct task* task_tail = 0;
struct task* task_head = 0;

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
        current_task = task;
        goto out;
    }

    task_tail->next = task;
    task->prev      = task_tail;
    task_tail       = task;

out:
    if(ISERR(res)){
        task_free(task);
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
        current_task = task_get_next();
    }
}

int task_free(struct task* task){
    // Lecture 113 - paging_desc lives on the process now, so the
    // free path moves there. task_free only releases the task
    // record itself; the descriptor is torn down when the
    // process exits.
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

void task_run_first_ever_task(){
    if(!current_task){
        panic("task_run_first_ever_task(): No current task exists!\n");
    }
    task_enter(task_head);
}

// make `task` current and iretq into it with its own
// FPU/SSE state. Nothing between the fxrstor and the iretq may touch
// an x87 or SSE register; the kernel is built -mgeneral-regs-only.
void task_enter(struct task* task){
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

void task_next(){
    // Lecture 197 - rotate through the ring skipping sleepers.
    // If everyone is asleep, udelay 100us and try again.
    struct task* next_task = NULL;
    do {
        int res = task_get_next_non_sleeping_task(&next_task);
        if(res < 0){
            panic("No more tasks or task error\n");
        }
        if(!next_task){
            udelay(100);
        }
    } while(!next_task);

    task_enter(next_task);
}

// Pull a 64-bit value off the task's stack at `index` slots
// from the top. Used by syscall handlers to read arguments the
// user pushed before invoking int 0x80.
//
// Read with copy_from_user instead of switching to the
// task's CR3 and dereferencing its rsp, so a user rsp that points
// at the kernel or at nothing gives 0 instead of a kernel read or a
// kernel #PF.
void* task_get_stack_item(struct task* task, int index){
    uint64_t value = 0;
    uint64_t addr = task->registers.rsp + (uint64_t)index * sizeof(uint64_t);
    if(copy_from_user(task->process, &value, (void*)addr, sizeof(value)) < 0){
        return 0;
    }
    return (void*)value;
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

    // Power-on FPU/SSE state: x87 control word 0x37F
    // and MXCSR 0x1F80 (all exceptions masked), registers zero.
    *(uint16_t*)&task->fpu_state[0]  = 0x037F;
    *(uint32_t*)&task->fpu_state[24] = 0x1F80;

    return 0;
}

// Lecture 197 - sleeping if the deadline TSC hasn't been reached.
bool task_asleep(struct task* task){
    return task->sleeping.sleep_until_microseconds > tsc_microseconds();
}

// Lecture 197 / 198 - park the task until tsc_microseconds()
// exceeds the deadline. L197 shipped the deadline as
// `tsc_microseconds() * microseconds` (multiplication, bug).
// L198 part 2 fixes it to `+ microseconds`. SamOs follows.
void task_sleep(struct task* task, TIME_MICROSECONDS microseconds){
    task->sleeping.sleep_until_microseconds = tsc_microseconds() + microseconds;
}

// Lecture 197 - rotate from the current task forward in the
// ring, skipping anyone whose deadline is in the future. Wraps
// back to task_head; returns -EIO if the ring is empty.
int task_get_next_non_sleeping_task(struct task** task_out){
    int res = 0;
    struct task* _current_task = current_task ? current_task->next : task_head;
    do {
        if(!_current_task){
            _current_task = task_head;
            if(!_current_task){
                res = -EIO;
                break;
            }
        }
        if(task_asleep(_current_task)){
            _current_task = _current_task->next;
            continue;
        }
        res = 0;
        break;
    } while(_current_task);

    *task_out = _current_task;
    return res;
}
