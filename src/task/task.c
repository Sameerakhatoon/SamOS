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
// #include "loader/formats/elfloader.h"  // L40 - elf loader not ported yet

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
    // L40 - paging_desc_free lands in L43.
    paging_desc_free(task->paging_desc);
    task_list_remove(task);
    kfree(task);
    return 0;
}

int task_switch(struct task* task){
    current_task = task;
    paging_switch(task->paging_desc);
    return 0;
}

// Lecture 40 - paging_desc accessors.
struct paging_desc* task_paging_desc(struct task* task){
    return task->paging_desc;
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
    return paging_get_physical_address(task->paging_desc, virtual_address);
}

void task_next(){
    struct task* next_task = task_get_next();
    if(!next_task){
        panic("No more tasks!\n");
    }
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

    // Lecture 40 - each task gets its own 4-level page tree.
    task->paging_desc = paging_desc_new(PAGING_MAP_LEVEL_4);
    if(!task->paging_desc){
        return -EIO;
    }

    // Lecture 59 - identity-map every E820-usable region into the
    // task's own PML4. Once we paging_switch to this descriptor
    // (entering ring 3), the kernel-side data the task still
    // reaches (its own GDT entries, the IDT we just lidt'd, the
    // VGA buffer) must stay addressable. Same map the kernel
    // already built into kernel_paging_desc; we replicate it
    // per task.
    paging_map_e820_memory_regions(task->paging_desc);

    task->registers.ip = SAMOS_PROGRAM_VIRTUAL_ADDRESS;
    if(process->filetype == PROCESS_FILETYPE_ELF){
        // L40 deviation: elfloader not yet rebuilt; panic instead
        // of dereferencing a non-existent header.
        panic("ELF files not supported yet\n");
    }

    task->registers.ss  = USER_DATA_SEGMENT;
    task->registers.cs  = USER_CODE_SEGMENT;
    task->registers.rsp = SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
    task->process       = process;

    return 0;
}
