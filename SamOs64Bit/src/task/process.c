#include "process.h"
#include "config.h"
#include "status.h"
#include "task/task.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
#include "string/string.h"
// L48 - FAT16 / VFS came back. L63 - elfloader.h back too now
// that process_load_elf / process_map_elf are un-stubbed.
#include "fs/file.h"
#include "lib/vector/vector.h"   // L105 - process->file_handles
#include "loader/formats/elfloader.h"
#include "kernel.h"
#include <stdbool.h>

// The current process that is running.
struct process* current_process = 0;

static struct process* processes[SAMOS_MAX_PROCESSES] = { 0 };

static int process_close_file_handles(struct process* process);

static void process_init(struct process* process){
    memset(process, 0, sizeof(struct process));
    // L105 - process_fopen pushes onto this; process_free_process
    // walks it and fclose's each fd.
    process->file_handles = vector_new(sizeof(struct process_file_handle*), 4, 0);
}

struct process* process_current(){
    return current_process;
}

struct process* process_get(int process_id){
    if(process_id < 0 || process_id >= SAMOS_MAX_PROCESSES){
        return 0;
    }
    return processes[process_id];
}

// Lecture 48 - un-stubbed. FAT16 / VFS came back in L46-L47;
// fopen / fstat / fread / fclose work now. Restoration mirrors
// the original 32-bit body verbatim except for the SAMOS_ALL_OK
// constant name (vs PEACHOS_ALL_OK).
static int process_load_binary(const char* filename, struct process* process){
    int   res = 0;
    void* program_data_ptr = 0x00;

    int fd = fopen(filename, "r");
    if(!fd){
        res = -EIO;
        goto out;
    }

    struct file_stat stat;
    res = fstat(fd, &stat);
    if(res != SAMOS_ALL_OK){
        goto out;
    }

    program_data_ptr = kzalloc(stat.filesize);
    if(!program_data_ptr){
        res = -ENOMEM;
        goto out;
    }

    if(fread(program_data_ptr, stat.filesize, 1, fd) != 1){
        res = -EIO;
        goto out;
    }

    process->filetype = PROCESS_FILETYPE_BINARY;
    process->ptr      = program_data_ptr;
    process->size     = stat.filesize;

out:
    if(res < 0){
        if(program_data_ptr){
            kfree(program_data_ptr);
        }
    }
    fclose(fd);
    return res;
}

// Lecture 63 - ELF loading re-enabled. The body is the original
// 32-bit logic; elfloader.c was widened to uintptr_t in L63 so
// the elf64_phdr casts no longer warn under x86_64-elf-gcc.
//
// Note the file is still parsed as ELF32 - our blank.elf / etc
// are actually ELF64 (L62 toolchain change), so this load will
// fail on the EI_CLASS check until L65 refactors elfloader to
// ELF64. Until then process_load_data falls through to
// process_load_binary if process_load_elf returns -EINFORMAT.
static int process_load_elf(const char* filename, struct process* process){
    int res = 0;
    struct elf_file* elf_file = 0;
    res = elf_load(filename, &elf_file);
    if(ISERR(res)){
        goto out;
    }
    process->filetype = PROCESS_FILETYPE_ELF;
    process->elf_file = elf_file;
out:
    return res;
}

static int process_load_data(const char* filename, struct process* process){
    int res = 0;
    res = process_load_elf(filename, process);
    if(res == -EINFORMAT){
        res = process_load_binary(filename, process);
    }
    return res;
}

int process_map_binary(struct process* process){
    int res = 0;
    paging_map_to(process->task->paging_desc,
                  (void*)SAMOS_PROGRAM_VIRTUAL_ADDRESS,
                  process->ptr,
                  paging_align_address(process->ptr + process->size),
                  PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL | PAGING_IS_WRITEABLE);
    return res;
}

// Lecture 63 - ELF mapping re-enabled. p_vaddr now goes through
// uintptr_t to silence the int-to-pointer-of-different-size
// diagnostic on x86_64.
static int process_map_elf(struct process* process){
    int res = 0;
    struct elf_file*   elf_file = process->elf_file;
    struct elf_header* header   = elf_header(elf_file);
    struct elf64_phdr* phdrs    = elf_pheader(header);

    for(int i = 0; i < header->e_phnum; i++){
        struct elf64_phdr* phdr = &phdrs[i];
        void* phdr_phys_address = elf_phdr_phys_address(elf_file, phdr);
        int flags = PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL;
        if(phdr->p_flags & PF_W){
            flags |= PAGING_IS_WRITEABLE;
        }
        res = paging_map_to(process->task->paging_desc,
                            paging_align_to_lower_page((void*)(uintptr_t)phdr->p_vaddr),
                            paging_align_to_lower_page(phdr_phys_address),
                            paging_align_address(phdr_phys_address + phdr->p_memsz),
                            flags);
        if(ISERR(res)){
            break;
        }
    }
    return res;
}

int process_map_memory(struct process* process){
    int res = 0;
    switch(process->filetype){
        case PROCESS_FILETYPE_ELF:
            res = process_map_elf(process);
            break;
        case PROCESS_FILETYPE_BINARY:
            res = process_map_binary(process);
            break;
        default:
            panic("process_map_memory: Invalid filetype\n");
    }
    if(res < 0){
        goto out;
    }

    // Ch 107: map the user stack into the task's address space so the
    // user code can push/pop without page-faulting. Stack grows down
    // from STACK_ADDRESS_START to STACK_ADDRESS_END, so we map starting
    // from END.
    paging_map_to(process->task->paging_desc,
                  (void*)SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END,
                  process->stack,
                  paging_align_address(process->stack + SAMOS_USER_PROGRAM_STACK_SIZE),
                  PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL | PAGING_IS_WRITEABLE);

out:
    return res;
}

int process_get_free_slot(){
    for(int i = 0; i < SAMOS_MAX_PROCESSES; i++){
        if(processes[i] == 0){
            return i;
        }
    }
    return -EISTKN;
}

int process_load_for_slot(const char* filename, struct process** process, int process_slot){
    int res = 0;
    struct task* task = 0;
    struct process* _process = 0;
    void* program_stack_ptr = 0;

    if(process_get(process_slot) != 0){
        res = -EISTKN;
        goto out;
    }

    _process = kzalloc(sizeof(struct process));
    if(!_process){
        res = -ENOMEM;
        goto out;
    }

    process_init(_process);
    res = process_load_data(filename, _process);
    if(res < 0){
        goto out;
    }

    program_stack_ptr = kzalloc(SAMOS_USER_PROGRAM_STACK_SIZE);
    if(!program_stack_ptr){
        res = -ENOMEM;
        goto out;
    }

    strncpy(_process->filename, filename, sizeof(_process->filename));
    _process->stack = program_stack_ptr;
    _process->id    = process_slot;

    task = task_new(_process);
    if(ERROR_I(task) == 0){
        res = ERROR_I(task);
        goto out;
    }

    _process->task = task;

    res = process_map_memory(_process);
    if(res < 0){
        goto out;
    }

    *process = _process;
    processes[process_slot] = _process;

out:
    if(ISERR(res)){
        if(_process && _process->task){
            task_free(_process->task);
        }
        // TODO(g-fix): leaks _process and _process->stack on error.
    }
    return res;
}

static int process_find_free_allocation_index(struct process* process){
    int res = -ENOMEM;
    for(int i = 0; i < SAMOS_MAX_PROGRAM_ALLOCATIONS; i++){
        if(process->allocations[i].ptr == 0){
            res = i;
            break;
        }
    }
    return res;
}

static struct process_allocation* process_get_allocation_by_addr(struct process* process, void* addr){
    for(int i = 0; i < SAMOS_MAX_PROGRAM_ALLOCATIONS; i++){
        if(process->allocations[i].ptr == addr){
            return &process->allocations[i];
        }
    }
    return 0;
}

void* process_malloc(struct process* process, size_t size){
    void* ptr = kzalloc(size);
    if(!ptr){
        goto out_err;
    }

    int index = process_find_free_allocation_index(process);
    if(index < 0){
        goto out_err;
    }

    int res = paging_map_to(process->task->paging_desc, ptr, ptr,
                            paging_align_address(ptr + size),
                            PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);
    if(res < 0){
        goto out_err;
    }

    process->allocations[index].ptr  = ptr;
    process->allocations[index].size = size;
    return ptr;

out_err:
    if(ptr){
        kfree(ptr);
    }
    return 0;
}

static bool process_is_process_pointer(struct process* process, void* ptr){
    for(int i = 0; i < SAMOS_MAX_PROGRAM_ALLOCATIONS; i++){
        if(process->allocations[i].ptr == ptr){
            return true;
        }
    }
    return false;
}

static void process_allocation_unjoin(struct process* process, void* ptr){
    for(int i = 0; i < SAMOS_MAX_PROGRAM_ALLOCATIONS; i++){
        if(process->allocations[i].ptr == ptr){
            process->allocations[i].ptr  = 0x00;
            process->allocations[i].size = 0;
        }
    }
}

int process_terminate_allocations(struct process* process){
    for(int i = 0; i < SAMOS_MAX_PROGRAM_ALLOCATIONS; i++){
        process_free(process, process->allocations[i].ptr);
    }
    return 0;
}

int process_free_binary_data(struct process* process){
    kfree(process->ptr);
    return 0;
}

// Lecture 65 - elf_close un-stubbed now that the L65 ELF64
// loader is real.
int process_free_elf_data(struct process* process){
    if(process->elf_file){
        elf_close(process->elf_file);
    }
    return 0;
}

int process_free_program_data(struct process* process){
    int res = 0;
    switch(process->filetype){
        case PROCESS_FILETYPE_BINARY:
            res = process_free_binary_data(process);
            break;
        case PROCESS_FILETYPE_ELF:
            res = process_free_elf_data(process);
            break;
        default:
            res = -EINVARG;
    }
    return res;
}

void process_switch_to_any(){
    for(int i = 0; i < SAMOS_MAX_PROCESSES; i++){
        if(processes[i]){
            process_switch(processes[i]);
            return;
        }
    }
    panic("No processes to switch too\n");
}

static void process_unlink(struct process* process){
    processes[process->id] = 0x00;
    if(current_process == process){
        process_switch_to_any();
    }
}

int process_terminate(struct process* process){
    int res = 0;
    res = process_terminate_allocations(process);
    if(res < 0){
        goto out;
    }

    res = process_free_program_data(process);
    if(res < 0){
        goto out;
    }

    // Lecture 105 - close every file the process opened. Safe
    // before the rest of the teardown because process_close_file_handles
    // does not touch task/stack memory.
    process_close_file_handles(process);

    kfree(process->stack);
    task_free(process->task);
    process_unlink(process);

out:
    return res;
}

// Lecture 105 - per-process fopen wrapper. Calls the kernel
// fopen and records the descriptor on the process's file_handles
// vector so process exit can fclose it.
//
// Upstream bug preserved verbatim:
//   kzalloc(sizeof(struct process_file_handler*))
// allocates the size of a POINTER, not the struct, AND uses the
// typo `_handler` instead of `_handle`. Reading or writing any
// field beyond the first 8 bytes (fd) lands in the next heap
// block. The typo and the size mistake are documented inline;
// callers must not depend on the file_path/mode being readable
// until upstream fixes both.
int process_fopen(struct process* process, const char* path, const char* mode){
    int res = 0;
    int fd  = fopen(path, mode);
    if(fd <= 0){
        res = -EIO;
        goto out;
    }
    res = fd;

    // sic - "_handler" and pointer-sized allocation per upstream.
    struct process_file_handle* handle = kzalloc(sizeof(struct process_file_handler*));
    if(!handle){
        res = -ENOMEM;
        goto out;
    }
    handle->fd = fd;
    strncpy(handle->file_path, path, sizeof(handle->file_path));
    strncpy(handle->mode,      mode, sizeof(handle->mode));

    vector_push(process->file_handles, &handle);
out:
    if(res < 0){
        fclose(fd);
    }
    return res;
}

struct process_file_handle* process_file_handle_get(struct process* process, int fd){
    size_t total_handles = vector_count(process->file_handles);
    for(size_t i = 0; i < total_handles; i++){
        struct process_file_handle* handle = NULL;
        vector_at(process->file_handles, i, &handle, sizeof(handle));
        if(handle && handle->fd == fd){
            return handle;
        }
    }
    return NULL;
}

// Lecture 107-stub: process_validate_memory_or_terminate.
//
// The full body (walk the per-process allocation table to verify
// virt_addr + space_needed lands inside one allocation, otherwise
// terminate the process) requires process_get_allocation_by_addr
// and process_allocation_request, both of which L108/L109
// introduce. Until then this stub returns success so the L107
// fread path can compile and link.
//
// L109 will replace the body with the real validation; the
// stub stays in place for one lecture only.
int process_validate_memory_or_terminate(struct process* process,
                                         void* virt_addr,
                                         size_t space_needed){
    (void)process; (void)virt_addr; (void)space_needed;
    return 0;
}

// Lecture 107 - userland fread. Validate the buffer, translate
// it to a kernel-physical pointer, then forward to the kernel
// fread for the cached fd.
int process_fread(struct process* process, void* virt_ptr,
                  uint64_t size, uint64_t nmemb, int fd){
    int res = 0;

    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        res = -EIO;
        goto out;
    }

    size_t true_size = size * nmemb;
    res = process_validate_memory_or_terminate(process, virt_ptr, true_size);
    if(res < 0){
        goto out;
    }

    void* phys_ptr = task_virtual_address_to_physical(process->task, virt_ptr);
    if(!phys_ptr){
        goto out;
    }

    res = fread(phys_ptr, size, nmemb, handle->fd);
    if(res < 0){
        goto out;
    }
out:
    return res;
}

// Lecture 106 - userland fclose path. Drop the matching
// process_file_handle from the per-process vector and call the
// kernel fclose.
int process_fclose(struct process* process, int fd){
    int res = 0;
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        return -EINVARG;
    }
    fclose(handle->fd);
    vector_pop_element(process->file_handles, &handle, sizeof(handle));
    kfree(handle);
    return res;
}

static int process_close_file_handles(struct process* process){
    if(!process->file_handles){
        return 0;
    }
    size_t total_handles = vector_count(process->file_handles);
    for(size_t i = 0; i < total_handles; i++){
        struct process_file_handle* handle = NULL;
        vector_at(process->file_handles, i, &handle, sizeof(handle));
        if(handle){
            fclose(handle->fd);
            kfree(handle);
        }
    }
    vector_free(process->file_handles);
    process->file_handles = NULL;
    return 0;
}

void process_get_arguments(struct process* process, int* argc, char*** argv){
    *argc = process->arguments.argc;
    *argv = process->arguments.argv;
}

static int process_count_command_arguments(struct command_argument* root_argument){
    struct command_argument* current = root_argument;
    int i = 0;
    while(current){
        i++;
        current = current->next;
    }
    return i;
}

int process_inject_arguments(struct process* process, struct command_argument* root_argument){
    int res = 0;
    struct command_argument* current = root_argument;
    int i = 0;
    int argc = process_count_command_arguments(root_argument);
    if(argc == 0){
        res = -EIO;
        goto out;
    }

    char** argv = process_malloc(process, sizeof(const char*) * argc);
    if(!argv){
        res = -ENOMEM;
        goto out;
    }

    while(current){
        char* argument_str = process_malloc(process, sizeof(current->argument));
        if(!argument_str){
            res = -ENOMEM;
            goto out;
        }
        strncpy(argument_str, current->argument, sizeof(current->argument));
        argv[i] = argument_str;
        current = current->next;
        i++;
    }

    process->arguments.argc = argc;
    process->arguments.argv = argv;

out:
    return res;
}

void process_free(struct process* process, void* ptr){
    struct process_allocation* allocation = process_get_allocation_by_addr(process, ptr);
    if(!allocation){
        return;
    }

    int res = paging_map_to(process->task->paging_desc,
                            allocation->ptr,
                            allocation->ptr,
                            paging_align_address(allocation->ptr + allocation->size),
                            0x00);
    if(res < 0){
        return;
    }

    process_allocation_unjoin(process, ptr);
    kfree(ptr);
}

int process_load(const char* filename, struct process** process){
    int res = 0;
    int process_slot = process_get_free_slot();
    if(process_slot < 0){
        res = -EISTKN;
        goto out;
    }

    res = process_load_for_slot(filename, process, process_slot);

out:
    return res;
}

int process_switch(struct process* process){
    current_process = process;
    return 0;
}

int process_load_switch(const char* filename, struct process** process){
    int res = process_load(filename, process);
    if(res == 0){
        process_switch(*process);
    }
    return res;
}
