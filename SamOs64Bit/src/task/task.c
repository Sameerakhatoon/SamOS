// Lecture 40 - task system rebuilt for 64-bit.
//
// What changed vs the 32-bit version:
//   - struct registers fields are now 64-bit (rdi/rsi/.../rsp).
//     The interrupt_frame -> registers copy in task_save_state
//     mirrors the new layout.
//   - struct task uses paging_desc (4-level walk) instead of
//     paging_4gb_chunk (which built a 2-level 4 GiB identity
//     map). task_init calls paging_desc_new(PAGING_MAP_LEVEL_4).
//   - task_free uses paging_desc_free (lands in L43).
//   - copy_string_from_task uses the new paging_get +
//     paging_map API to remap a kernel-side scratch page into
//     the task's address space, copy, then map it back to its
//     original state. References kernel_desc() (lands in L44).
//   - ELF entry-point support remains "panic, not supported"
//     until elfloader is back.
//
// task.c is NOT yet in the build at L40. It references
// kernel_desc, paging_desc_free, process_switch, etc - those
// land in L43 (paging_desc_free), L44 (kernel_desc), and L41
// / L42 (process). Once L40-L42 + L43-L44 are all in, the file
// can be added to FILES.

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

struct task* task_get_next(){
    if(!current_task->next){
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
    task_switch(task_head);
    task_return(&task_head->registers);
}

int task_page(){
    user_registers();
    task_switch(current_task);
    return 0;
}

// Lecture 40 - 64-bit register file mirror.
void task_save_state(struct task* task, struct interrupt_frame* frame){
    task->registers.ip    = frame->ip;
    task->registers.cs    = frame->cs;
    task->registers.flags = frame->flags;
    task->registers.rsp   = frame->rsp;
    task->registers.ss    = frame->ss;
    task->registers.rax   = frame->rax;
    task->registers.rbp   = frame->rbp;
    task->registers.rbx   = frame->rbx;
    task->registers.rcx   = frame->rcx;
    task->registers.rdi   = frame->rdi;
    task->registers.rdx   = frame->rdx;
    task->registers.rsi   = frame->rsi;
}

void task_current_save_state(struct interrupt_frame* frame){
    if(!task_current()){
        panic("No current task to save\n");
    }
    struct task* task = task_current();
    task_save_state(task, frame);
}

int task_page_task(struct task* task){
    user_registers();
    paging_switch(task_paging_desc(task));
    return 0;
}

void* task_virtual_address_to_physical(struct task* task, void* virtual_address){
    // L113 - paging descriptor lives on the process now.
    return paging_get_physical_address(task->process->paging_desc, virtual_address);
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

    task_switch(next_task);
    task_return(&next_task->registers);
}

// Pull a 64-bit value off the task's stack at `index` slots
// from the top. Used by syscall handlers to read arguments the
// user pushed before invoking int 0x80.
void* task_get_stack_item(struct task* task, int index){
    void* result = 0;
    uint64_t* sp_ptr = (uint64_t*)task->registers.rsp;
    task_page_task(task);
    result = (void*)sp_ptr[index];
    kernel_page();
    return result;
}

// Lecture 40 - rewritten under the 4-level paging API.
//
// Allocates a kernel-side scratch page from the heap, then
// reaches into the TASK'S page tables and overrides the entry
// covering that scratch page so the task can see it too.
// Switches CR3 to the task's PML4, strncpy'd the user string
// into the scratch, switches back to the kernel PML4, and
// restores the original PT entry on the task side.
//
// References kernel_desc() (L44) and paging_desc_free's
// related machinery; not buildable until those land.
int copy_string_from_task(struct task* task, void* virtual, void* phys, int max){
    if(max >= PAGING_PAGE_SIZE){
        return -EINVARG;
    }
    int res = 0;
    char* tmp = kzalloc(max);
    if(!tmp){
        res = -ENOMEM;
        goto out;
    }

    // Resolve the kernel-side virtual address of the scratch
    // to a physical address (kernel runs identity-mapped at
    // setup time so this is usually a no-op; the lookup keeps
    // it future-proof against non-identity kernel mappings).
    void* phys_tmp = paging_get_physical_address(kernel_desc(), tmp);
    struct paging_desc* task_desc = task_paging_desc(task);

    // Save the current task-side mapping so we can restore it.
    struct paging_desc_entry old_entry;
    memcpy(&old_entry, paging_get(task_desc, phys_tmp), sizeof(struct paging_desc_entry));
    int old_entry_flags = 0;
    old_entry_flags |= old_entry.read_write | old_entry.present | old_entry.user_supervisor;

    // Install a writeable+present mapping in the task's PML4
    // covering the scratch.
    paging_map(task_desc, phys_tmp, phys_tmp,
               PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);

    // Switch to the task's address space, copy the string,
    // switch back.
    task_page_task(task);
    strncpy(tmp, virtual, max);
    kernel_page();

    // Push the result into the kernel-side caller buffer.
    strncpy(phys, tmp, max);

    // Restore the task's original mapping.
    paging_map(task_desc, phys_tmp,
               (void*)((uint64_t)(old_entry.address) << 12),
               old_entry_flags);
out:
    if(tmp){
        kfree(tmp);
    }
    return res;
}

static int task_init(struct task* task, struct process* process){
    memset(task, 0, sizeof(struct task));

    // Lecture 113 - the paging descriptor and the e820 mapping
    // are set up by process_load_for_slot before task_new is
    // called. task_init only sees the process's already-live
    // descriptor through process->paging_desc.


    task->registers.ip = SAMOS_PROGRAM_VIRTUAL_ADDRESS;
    if(process->filetype == PROCESS_FILETYPE_ELF){
        // L65 - the ELF64 loader is in. Pull the entry point
        // out of the ELF header.
        task->registers.ip = elf_header(process->elf_file)->e_entry;
    }

    task->registers.ss  = USER_DATA_SEGMENT;
    task->registers.cs  = USER_CODE_SEGMENT;
    task->registers.rsp = SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
    task->process       = process;

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
