#include "process.h"
#include "config.h"
#include "status.h"
#include "task/task.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
#include "string/string.h"
#include "graphics/graphics.h"   // L166 - struct framebuffer_pixel + graphics_info
// L48 - FAT16 / VFS came back. L63 - elfloader.h back too now
// that process_load_elf / process_map_elf are un-stubbed.
#include "fs/file.h"
#include "lib/vector/vector.h"   // L105 - process->file_handles
#include "loader/formats/elfloader.h"
// Lecture 154 - process_window_create reaches into the window
// system to spawn a kernel window and into graphics for the
// screen dimensions.
#include "graphics/graphics.h"
#include "graphics/window.h"
#include "graphics/terminal.h"   // L158 - process_print routes here
#include "kernel.h"
#include <stdbool.h>

// The current process that is running.
struct process* current_process = 0;

// Lecture 114 - process slot table is now a vector. The
// fixed-size array is retired; process_system_init builds the
// vector at boot, process_get_free_slot extends it on demand,
// and process_unlink overwrites the slot back to NULL.
struct vector* process_vector = NULL;

static int process_close_file_handles(struct process* process);

void process_system_init(void){
    process_vector = vector_new(sizeof(struct process*), 10, 0);
}

// Lecture 108 - allocations are now vector-backed. Initial
// capacity of 10 matches upstream.
static void process_init(struct process* process){
    memset(process, 0, sizeof(struct process));
    process->allocations  = vector_new(sizeof(struct process_allocation), 10, 0);
    process->file_handles = vector_new(sizeof(struct process_file_handle*), 4, 0);
    // Lecture 153 - userland pointer registry.
    process->kernel_userland_ptrs_vector =
        vector_new(sizeof(struct userland_ptr*), 4, 0);
    // Lecture 154 - per-process window list.
    process->windows =
        vector_new(sizeof(struct process_window*), 4, 0);

    // Lecture 160 - pre-allocate the window-events ring at the
    // upstream ceiling. vector_grow advances index without
    // initialising data; producers vector_overwrite later.
    process->window_events.vector =
        vector_new(sizeof(struct window_event), 100, 0);
    vector_grow(process->window_events.vector,
                PROCESS_MAX_WINDOW_EVENTS_RECORDED);
}

// Lecture 154 - does this process own the given kernel window?
bool process_owns_kernel_window(struct process* process, struct window* kernel_window){
    size_t total_windows = vector_count(process->windows);
    for(size_t i = 0; i < total_windows; i++){
        struct process_window* win = NULL;
        vector_at(process->windows, i, &win, sizeof(win));
        if(win && win->kernel_win == kernel_window){
            return true;
        }
    }
    return false;
}

// Lecture 154 - given a kernel window, find the process that
// owns it. Used by the window event loop to deliver events.
struct process* process_get_from_kernel_window(struct window* window){
    size_t total_processes = vector_count(process_vector);
    for(size_t i = 0; i < total_processes; i++){
        struct process* process = NULL;
        vector_at(process_vector, i, &process, sizeof(process));
        if(process){
            if(process_owns_kernel_window(process, window)){
                return process;
            }
        }
    }
    return NULL;
}

// Lecture 154 - resolve the userspace handle to its process_window record.
// Lecture 162 - forward declaration; the body lives further down
// in this file next to process_unlink so the unlink call site
// finds it.
void process_window_closed(struct process* process, struct process_window* proc_win);

// Lecture 162 - title-bar / body coordinates. Click and move
// events arrive in window-root coords; userland wants
// body-relative coords. Reject the event if the click landed
// outside the body region.
int process_window_event_get_relative_window_body_coords(struct window_event* event,
                                                         int* out_x, int* out_y){
    int res = 0;
    int rel_body_x = event->data.click.x - event->window->graphics->relative_x;
    int rel_body_y = event->data.click.y - event->window->graphics->relative_y;
    if(rel_body_x < 0 || rel_body_y < 0 ||
       rel_body_x > (int)event->window->graphics->width ||
       rel_body_y > (int)event->window->graphics->height){
        res = -EINVARG;
        goto out;
    }
    *out_x = rel_body_x;
    *out_y = rel_body_y;
out:
    return res;
}

int process_window_event_modify_for_userspace_mouse_click(struct window_event* event){
    return process_window_event_get_relative_window_body_coords(event,
                                                                &event->data.click.x,
                                                                &event->data.click.y);
}

int process_window_event_modify_for_userspace_mouse_move(struct window_event* event){
    return process_window_event_get_relative_window_body_coords(event,
                                                                &event->data.move.x,
                                                                &event->data.move.y);
}

// Lecture 162 - dispatch the coord-translation by event type.
int process_window_event_modify_for_userspace(struct window_event* event){
    int res = 0;
    switch(event->type){
        case WINDOW_EVENT_TYPE_MOUSE_CLICK:
            res = process_window_event_modify_for_userspace_mouse_click(event);
            break;
        case WINDOW_EVENT_TYPE_MOUSE_MOVE:
            res = process_window_event_modify_for_userspace_mouse_move(event);
            break;
    }
    return res;
}

// Lecture 162 - exposed so the event handler below can swap the
// active process on focus.
void process_current_set(struct process* process){
    current_process = process;
}

// Lecture 162 - kernel-side reaction to a focus event: make this
// process current so the next scheduler tick lands in it.
int process_window_event_handler_event_focus(struct window* window,
                                             struct process* process,
                                             struct window_event* event){
    process_current_set(process);
    return 0;
}

// Lecture 162 - same shape as process_get_from_kernel_window but
// scoped to a single process.
struct process_window* process_window_get_from_kernel_window(struct process* process,
                                                             struct window* kern_win){
    size_t total_windows = vector_count(process->windows);
    for(size_t i = 0; i < total_windows; i++){
        struct process_window* proc_win = NULL;
        vector_at(process->windows, i, &proc_win, sizeof(proc_win));
        if(proc_win){
            if(proc_win->kernel_win == kern_win){
                return proc_win;
            }
        }
    }
    return NULL;
}

// Lecture 162 - kernel-side reaction to a close event. Drop the
// process_window record.
int process_window_event_handler_event_close(struct window* window,
                                             struct process* process,
                                             struct window_event* event){
    int res = 0;
    struct process_window* proc_win =
        process_window_get_from_kernel_window(process, window);
    if(proc_win){
        process_window_closed(process, proc_win);
    }
    return res;
}

int process_window_event_handler_kernel_handle_event(struct window* window,
                                                     struct process* process,
                                                     struct window_event* event){
    int res = 0;
    switch(event->type){
        case WINDOW_EVENT_TYPE_FOCUS:
            res = process_window_event_handler_event_focus(window, process, event);
            break;
        case WINDOW_EVENT_TYPE_WINDOW_CLOSE:
            res = process_window_event_handler_event_close(window, process, event);
            break;
    }
    return res;
}

// Lecture 162 - the handler registered with the window subsystem
// at window-create time. Translates coords + pushes events into
// the per-process ring (skipping moves for now), then runs the
// kernel-side reactions.
int process_window_event_handler(struct window* window, struct window_event* event){
    int res = 0;
    struct process* process = process_get_from_kernel_window(window);
    if(!process){
        return 0;
    }
    // Move events are disabled for now (upstream comment).
    if(event->type != WINDOW_EVENT_TYPE_MOUSE_MOVE){
        struct window_event cloned_event = *event;
        res = process_window_event_modify_for_userspace(&cloned_event);
        if(res >= 0){
            process_push_window_event(process, &cloned_event);
        }
        res = 0;
    }
    res = process_window_event_handler_kernel_handle_event(window, process, event);
    return res;
}

// Lecture 161 - drain one window event from the head of the ring.
// Returns -EOUTOFRANGE when empty, -EINVARG on bad args, 0 on
// success. Note: upstream reads index 0 unconditionally and
// decrements total_unpopped without advancing a read head. The
// producer side overwrites at `index % MAX`; the consumer always
// reads slot 0. Preserved verbatim - L162+ wires the loop in
// userland and will reveal whether this is intended.
int process_pop_window_event(struct process* process, struct window_event* event_out){
    int res = -EOUTOFRANGE;
    if(!process || !event_out){
        res = -EINVARG;
        goto out;
    }
    if(process->window_events.total_unpopped > 0){
        vector_at(process->window_events.vector, 0, event_out, sizeof(struct window_event));
        process->window_events.total_unpopped--;
        res = 0;
    }
out:
    return res;
}

// Lecture 161 - enqueue a window event into the ring. We null
// the kernel-only window pointer before storing because the
// event might outlive that pointer's mapping.
int process_push_window_event(struct process* process, struct window_event* event){
    int element_index = process->window_events.index % PROCESS_MAX_WINDOW_EVENTS_RECORDED;
    struct window_event event_copy = *event;
    event_copy.window = NULL;
    vector_overwrite(process->window_events.vector, element_index, &event_copy, sizeof(event_copy));
    process->window_events.total_unpopped++;
    return 0;
}

struct process_window* process_window_get_from_user_window(struct process* process,
                                                           struct process_userspace_window* user_win){
    size_t total_windows = vector_count(process->windows);
    for(size_t i = 0; i < total_windows; i++){
        struct process_window* win = NULL;
        vector_at(process->windows, i, &win, sizeof(win));
        if(win && win->user_win == user_win){
            return win;
        }
    }
    return NULL;
}

// Lecture 154 - close every kernel window this process owns.
// Called from process_free_process before the heap is torn down.
void process_close_windows(struct process* process){
    size_t total_windows = vector_count(process->windows);
    for(size_t i = 0; i < total_windows; i++){
        struct process_window* window = NULL;
        vector_at(process->windows, i, &window, sizeof(window));
        if(window && window->kernel_win){
            window_close(window->kernel_win);
        }
    }
}

// Lecture 154 - create a window on behalf of the process. The
// kernel window is centered on the screen; the userspace mirror
// lives in the process's address space so userland can read its
// title and dimensions back.
struct process_window* process_window_create(struct process* process, char* title,
                                             int width, int height, int flags, int id){
    int res = 0;
    struct process_window* proc_win = kzalloc(sizeof(struct process_window));
    if(!proc_win){
        res = -ENOMEM;
        goto out;
    }

    struct graphics_info* screen_graphics = graphics_screen_info();
    size_t abs_x = (screen_graphics->width  / 2) - (width  / 2);
    size_t abs_y = (screen_graphics->height / 2) - (height / 2);
    proc_win->kernel_win = window_create(screen_graphics, NULL, title,
                                         abs_x, abs_y, width, height, flags, id);
    if(!proc_win->kernel_win){
        res = -ENOMEM;
        goto out;
    }

    proc_win->user_win = process_malloc(process, sizeof(struct process_userspace_window));
    if(!proc_win->user_win){
        res = -ENOMEM;
        goto out;
    }

    proc_win->user_win->width  = width;
    proc_win->user_win->height = height;
    strncpy(proc_win->user_win->title, title, sizeof(proc_win->user_win->title));

    // Lecture 162 - register the per-process event handler now
    // that the body has landed.
    window_event_handler_register(proc_win->kernel_win, process_window_event_handler);

    vector_push(process->windows, &proc_win);
out:
    if(res < 0){
        if(proc_win){
            if(proc_win->kernel_win){
                window_close(proc_win->kernel_win);
                proc_win->kernel_win = NULL;
            }
            if(proc_win->user_win){
                process_free(process, proc_win->user_win);
                proc_win->user_win = NULL;
            }
            kfree(proc_win);
            proc_win = NULL;
        }
    }
    return proc_win;
}

// Lecture 158 - process_print_char / process_print write into
// the sysout window's terminal. Upstream does not null-check
// process->sysout_win; we keep that behaviour so the first
// caller (peachos_divert_stdout_to_window in blank.c) wires
// it up before any printf fires.
void process_print_char(struct process* process, char c){
    struct process_window* printing_process_win = process->sysout_win;
    struct terminal* win_term = window_terminal(printing_process_win->kernel_win);
    if(win_term){
        terminal_write(win_term, c);
    }
}

void process_print(struct process* process, const char* message){
    struct process_window* printing_process_win = process->sysout_win;
    struct terminal* win_term = window_terminal(printing_process_win->kernel_win);
    if(win_term){
        terminal_print(win_term, message);
    }
}

void process_set_sysout_window(struct process* process, struct process_window* win){
    process->sysout_win = win;
}

struct process* process_current(){
    return current_process;
}

struct process* process_get(int process_id){
    int res = 0;
    struct process* process_out = NULL;
    res = vector_at(process_vector, process_id, &process_out, sizeof(process_out));
    if(res < 0){
        return ERROR(EINVARG);
    }
    return process_out;
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
    paging_map_to(process->paging_desc,
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
        res = paging_map_to(process->paging_desc,
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

    // Lecture 113 - identity-map every E820-usable region into
    // the process's PML4. Was task_init's responsibility before;
    // moved here so the descriptor's owner does the wiring.
    paging_map_e820_memory_regions(process->paging_desc);

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
    paging_map_to(process->paging_desc,
                  (void*)SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END,
                  process->stack,
                  paging_align_address(process->stack + SAMOS_USER_PROGRAM_STACK_SIZE),
                  PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL | PAGING_IS_WRITEABLE);

out:
    return res;
}

// Lecture 114 - scan the vector for a NULL slot; if none is
// found, push a fresh NULL and return the new index.
int process_get_free_slot(void){
    int    res                 = 0;
    bool   found               = false;
    size_t total_process_slots = vector_count(process_vector);
    for(size_t i = 0; i < total_process_slots; i++){
        struct process* process_out = NULL;
        res = vector_at(process_vector, i, &process_out, sizeof(process_out));
        if(res < 0){
            break;
        }
        if(!process_out){
            found = true;
            res   = i;
            break;
        }
    }
    if(res < 0){
        goto out;
    }
    if(!found){
        struct process* null_process = NULL;
        int process_index = vector_push(process_vector, &null_process);
        res = process_index;
    }
out:
    return res;
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

    // Lecture 113 - the paging descriptor moves from task to
    // process. Create it BEFORE task_new so process_map_memory
    // (which now hangs off process->paging_desc, and which is
    // also called BEFORE task_new) sees a live descriptor.
    _process->paging_desc = paging_desc_new(PAGING_MAP_LEVEL_4);
    if(!_process->paging_desc){
        res = -EIO;
        goto out;
    }

    res = process_map_memory(_process);
    if(res < 0){
        goto out;
    }

    task = task_new(_process);
    if(ERROR_I(task) == 0){
        res = ERROR_I(task);
        goto out;
    }
    _process->task = task;

    *process = _process;
    // Lecture 114 - the vector slot was reserved by
    // process_get_free_slot (either an existing NULL or a fresh
    // push); take ownership by overwriting it with our pointer.
    vector_overwrite(process_vector, process_slot, &_process, sizeof(&_process));

out:
    if(ISERR(res)){
        if(_process && _process->task){
            task_free(_process->task);
        }
        // TODO(g-fix): leaks _process and _process->stack on error.
    }
    return res;
}

// Lecture 108 - vector-walk variants of the allocation
// bookkeeping. process_find_free_allocation_index now appends a
// zeroed slot when no free entry exists; the new
// process_allocation_set_map factors out the paging + slot
// update.
int process_find_free_allocation_index(struct process* process){
    int    res             = 0;
    bool   found           = false;
    size_t allocation_size = vector_count(process->allocations);
    for(size_t i = 0; i < allocation_size; i++){
        struct process_allocation allocation;
        res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        if(allocation.ptr == NULL){
            res   = i;
            found = true;
            break;
        }
    }
    if(!found){
        struct process_allocation allocation = {0};
        res = vector_push(process->allocations, &allocation);
    }
    return res;
}

int process_allocation_set_map(struct process* process, int allocation_entry_index,
                               void* ptr, size_t size){
    int res = paging_map_to(process->paging_desc, ptr, ptr,
                            paging_align_address(ptr + size),
                            PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);
    if(res < 0){
        goto out;
    }

    struct process_allocation allocation;
    res = vector_at(process->allocations, allocation_entry_index,
                    &allocation, sizeof(allocation));
    if(res < 0){
        goto out;
    }
    allocation.ptr  = ptr;
    allocation.end  = ptr + size;
    allocation.size = size;
    vector_overwrite(process->allocations, allocation_entry_index,
                     &allocation, sizeof(allocation));
out:
    return res;
}

// Lecture 108 - by-start-addr lookup. Fills `allocation_out`
// with a COPY of the matched record and returns 0; returns
// -EIO when the address is not the start of any allocation.
// L109 will add process_get_allocation_by_addr for range
// queries; for L108 only the strict-start matcher exists.
int process_get_allocation_by_start_addr(struct process* process, void* addr,
                                         struct process_allocation* allocation_out){
    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        struct process_allocation allocation;
        int res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        if(allocation.ptr == addr){
            *allocation_out = allocation;
            return 0;
        }
    }
    return -EIO;
}

// Lecture 115 - look up an allocation by ptr and report its
// index. Returns -ENOTFOUND on miss; 0 on hit with the index
// written to *index_out if non-NULL.
int process_allocation_exists(struct process* process, void* ptr, size_t* index_out){
    int res = -ENOTFOUND;
    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        struct process_allocation allocation;
        res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        if(allocation.ptr == ptr){
            if(index_out){
                *index_out = i;
            }
            res = 0;
            break;
        }
    }
    return res;
}

// Lecture 115 - userland realloc. Translate the user virtual
// pointer, call krealloc, then update the existing
// process_allocation slot through process_allocation_set_map.
//
// Upstream bug preserved verbatim: the assignment
//   old_phys_ptr = old_virt_ptr;
// is overwritten by task_virtual_address_to_physical on the
// next line. The `if(old_phys_ptr)` guard is therefore
// effectively `if(old_virt_ptr != NULL)`. Documented inline
// to keep the per-commit diff against upstream linear.
void* process_realloc(struct process* process, void* old_virt_ptr, size_t new_size){
    int    res                  = 0;
    void*  new_ptr              = NULL;
    void*  old_phys_ptr         = NULL;
    size_t old_allocation_index = 0;

    res = process_allocation_exists(process, old_virt_ptr, &old_allocation_index);
    if(res < 0){
        goto out;
    }

    old_phys_ptr = old_virt_ptr;
    if(old_phys_ptr){
        old_phys_ptr = task_virtual_address_to_physical(process->task, old_virt_ptr);
        if(!old_phys_ptr){
            res = -ENOMEM;
            goto out;
        }
    }

    new_ptr = krealloc(old_phys_ptr, new_size);
    if(!new_ptr){
        res = -ENOMEM;
        goto out;
    }

    res = process_allocation_set_map(process, (int)old_allocation_index,
                                     new_ptr, new_size);
    if(res < 0){
        goto out;
    }

out:
    return new_ptr;
}

void* process_malloc(struct process* process, size_t size){
    int   res = 0;
    void* ptr = kzalloc(size);
    if(!ptr){
        res = -ENOMEM;
        goto out_err;
    }

    int index = process_find_free_allocation_index(process);
    if(index < 0){
        res = -ENOMEM;
        goto out_err;
    }

    res = process_allocation_set_map(process, index, ptr, size);
    if(res < 0){
        goto out_err;
    }
    return ptr;

out_err:
    if(ptr){
        kfree(ptr);
    }
    return 0;
}

static bool process_is_process_pointer(struct process* process, void* ptr){
    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        struct process_allocation allocation;
        int res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        if(allocation.ptr == ptr){
            return true;
        }
    }
    return false;
}

// Lecture 108 - vector-walk variant. Upstream's L108 commit
// accidentally left this function array-indexing through
// `process->allocations[i]`, which would index into the vector
// pointer instead of the elements. The upstream binary still
// happens to compile because struct vector has a `.ptr` field
// the array-index syntax silently lands on. We deviate from
// upstream verbatim and walk the vector properly; the upstream
// shape would never actually clear a freed slot.
static void process_allocation_unjoin(struct process* process, void* ptr){
    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        struct process_allocation allocation;
        int res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        if(allocation.ptr == ptr){
            allocation.ptr  = NULL;
            allocation.end  = NULL;
            allocation.size = 0;
            vector_overwrite(process->allocations, i, &allocation, sizeof(allocation));
        }
    }
}

int process_terminate_allocations(struct process* process){
    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        struct process_allocation allocation;
        int res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        if(allocation.ptr){
            process_free(process, allocation.ptr);
        }
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

// Lecture 114 - vector walk; same effect as the old array
// loop, plus an early break when vector_at fails (it should
// not, but we follow upstream's defensive shape).
void process_switch_to_any(void){
    size_t total_process_slots = vector_count(process_vector);
    for(size_t i = 0; i < total_process_slots; i++){
        struct process* process = NULL;
        int res = vector_at(process_vector, i, &process, sizeof(&process));
        if(res < 0){
            break;
        }
        if(process){
            process_switch(process);
            return;
        }
    }
    panic("No processes to switch too\n");
}

// Lecture 114 - null the slot via vector_overwrite rather than
// poking the array.
static void process_unlink(struct process* process){
    struct process* null_process = NULL;
    vector_overwrite(process_vector, process->id, &null_process, sizeof(&null_process));
    if(current_process == process){
        process_switch_to_any();
    }
}

// Lecture 162 - the close-event handler calls here once a window
// has gone away. Drop it from the process's owned-windows
// vector, return its userspace mirror to the process heap,
// kfree the kernel record.
void process_window_closed(struct process* process, struct process_window* proc_win){
    vector_pop_element(process->windows, &proc_win, sizeof(proc_win));
    process_free(process, proc_win->user_win);
    kfree(proc_win);
}

int process_terminate(struct process* process){
    int res = 0;
    // Lecture 154 - tear down owned windows first; their kernel
    // backing buffers live in kheap and need to come down before
    // process_terminate_allocations walks the per-process arena.
    process_close_windows(process);
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

    // Lecture 109 - free the allocations vector after the
    // contents have been torn down by process_terminate_allocations
    // above. Mirrors the upstream process_free_process clean-up.
    if(process->allocations){
        vector_free(process->allocations);
        process->allocations = NULL;
    }

    // Lecture 153 - drop the userland pointer registry.
    if(process->kernel_userland_ptrs_vector){
        vector_free(process->kernel_userland_ptrs_vector);
        process->kernel_userland_ptrs_vector = NULL;
    }

    // Lecture 154 - free the windows vector. The kernel windows
    // themselves were closed up front by process_close_windows.
    if(process->windows){
        vector_free(process->windows);
        process->windows = NULL;
    }

    // Lecture 161 - drop the per-process window-events ring.
    // Upstream wrote `vector_Free` (capital F) here, which would
    // not compile; we use the correct identifier and note the
    // upstream typo here for the audit trail.
    if(process->window_events.vector){
        vector_free(process->window_events.vector);
        process->window_events.vector = NULL;
    }

    kfree(process->stack);
    task_free(process->task);

    // Lecture 113 - the paging descriptor moved off the task,
    // so its free now lives here.
    if(process->paging_desc){
        paging_desc_free(process->paging_desc);
        process->paging_desc = NULL;
    }

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

// Lecture 109 - is the supplied virtual address inside the user
// program stack region? The stack grows downward, so START is
// the high address and END is the low one.
bool process_is_stack_memory(struct process* process, void* addr){
    (void)process;
    return (uintptr_t)addr >= SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END
        && (uintptr_t)addr <= SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
}

// Lecture 109 - range query against the per-process allocation
// table + the user stack region. Fills `allocation_request_out`
// with the matching record plus a `peek` block describing how
// many bytes remain past `addr`. Returns 0 on hit, -EIO when no
// allocation covers the address.
int process_get_allocation_by_addr(struct process* process, void* addr,
                                   struct process_allocation_request* allocation_request_out){
    memset(allocation_request_out, 0, sizeof(struct process_allocation_request));

    if(process_is_stack_memory(process, addr)){
        uint64_t addr_int         = (uint64_t)addr;
        uint64_t stack_size       = SAMOS_USER_PROGRAM_STACK_SIZE;
        // Stack grows down: total bytes left = bytes between addr
        // and the high end of the stack region.
        uint64_t total_bytes_left = SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START - addr_int;
        allocation_request_out->allocation.ptr  = (void*)SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END;
        allocation_request_out->allocation.end  = (void*)SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
        allocation_request_out->allocation.size = stack_size;
        allocation_request_out->flags |= PROCESS_ALLOCATION_REQUEST_IS_STACK_MEMORY;
        allocation_request_out->peek.addr             = addr;
        allocation_request_out->peek.end              = (void*)SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
        allocation_request_out->peek.total_bytes_left = total_bytes_left;
        return 0;
    }

    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        struct process_allocation allocation;
        int res = vector_at(process->allocations, i, &allocation, sizeof(allocation));
        if(res < 0){
            break;
        }
        uint64_t allocation_addr     = (uint64_t)allocation.ptr;
        uint64_t allocation_addr_end = (uint64_t)allocation.end;
        if((uint64_t)addr >= allocation_addr
           && (uint64_t)addr <= allocation_addr_end){
            // Upstream bug preserved verbatim: bytes_left is
            // computed as `allocation_addr_end - bytes_used`
            // instead of `allocation_addr_end - (uint64_t)addr`.
            // Since `bytes_used = addr - allocation_addr`, the
            // subtraction yields the SUM of end + addr - 2*start
            // which only matches the correct value at the
            // allocation start. Carrying the upstream form for
            // diff hygiene; the validate path overstates the
            // available space which makes -EINVARG return less
            // often than it should. Inert at L110 because
            // bytes_used is always 0 there (fread hits the start
            // of a freshly allocated buffer).
            size_t bytes_used = (uint64_t)addr - allocation_addr;
            size_t bytes_left = allocation_addr_end - bytes_used;
            allocation_request_out->allocation            = allocation;
            allocation_request_out->peek.addr             = addr;
            allocation_request_out->peek.end              = (void*)allocation_addr_end;
            allocation_request_out->peek.total_bytes_left = bytes_left;
            return 0;
        }
    }
    return -EIO;
}

// Lecture 109 - real body. Walks the allocation table for the
// region containing virt_addr; if `space_needed` would run off
// the end of that region, terminate the process. The L107 stub
// (returning 0 unconditionally) is replaced here.
int process_validate_memory_or_terminate(struct process* process,
                                         void* virt_addr,
                                         size_t space_needed){
    int res = 0;
    struct process_allocation_request allocation_request;
    res = process_get_allocation_by_addr(process, virt_addr, &allocation_request);
    if(res < 0){
        goto out;
    }
    if(allocation_request.peek.total_bytes_left < space_needed){
        res = -EINVARG;
        goto out;
    }
out:
    if(res < 0){
        process_terminate(process);
    }
    return res;
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

// Lecture 112 - shorthand. Walks the process's paging descriptor
// to translate a virtual address to its physical backing.
void* process_virtual_address_to_physical(struct process* process, void* virt_addr){
    return paging_get_physical_address(process->paging_desc, virt_addr);
}

// Lecture 112 - userland fstat. Validate the user buffer fits in
// one allocation, translate it, forward to the kernel fstat.
int process_fstat(struct process* process, int fd, struct file_stat* virt_filestat_addr){
    int res = 0;
    res = process_validate_memory_or_terminate(process, virt_filestat_addr,
                                               sizeof(*virt_filestat_addr));
    if(res < 0){
        goto out;
    }

    struct file_stat* phys_filestat_addr =
        process_virtual_address_to_physical(process, virt_filestat_addr);
    if(!phys_filestat_addr){
        res = -EINVARG;
        goto out;
    }

    res = fstat(fd, phys_filestat_addr);
    if(res < 0){
        goto out;
    }
out:
    return res;
}

// Lecture 111 - userland fseek. Look up the handle on the
// per-process vector, then call the kernel fseek with the
// underlying fd.
int process_fseek(struct process* process, int fd, int offset, FILE_SEEK_MODE whence){
    int res = 0;
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        res = -EIO;
        goto out;
    }
    res = fseek(handle->fd, offset, whence);
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
    // Lecture 108 - by-start-addr lookup fills a COPY of the
    // record; we read .ptr / .size off the copy.
    struct process_allocation allocation;
    int res = process_get_allocation_by_start_addr(process, ptr, &allocation);
    if(res < 0){
        return;
    }

    res = paging_map_to(process->paging_desc,
                        allocation.ptr,
                        allocation.ptr,
                        paging_align_address(allocation.ptr + allocation.size),
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

// Lecture 166 - map a kernel-side physical range into the
// process address space. Caller's phys range MUST be
// page-aligned at both ends. Hands back the virt pointer.
int process_map_into_userspace(struct process* process, void* phys_ptr,
                               size_t t_size, int map_flags,
                               void** virt_addr_out){
    int res = 0;
    void* virt_ptr = NULL;
    void* end_phys_addr = phys_ptr + t_size;
    if(!paging_is_aligned(phys_ptr)){
        res = -EINVARG;
        goto out;
    }
    if(!paging_is_aligned(end_phys_addr)){
        res = -EINVARG;
        goto out;
    }

    virt_ptr = process_malloc(process, t_size);
    if(!virt_ptr){
        res = -ENOMEM;
        goto out;
    }

    map_flags |= PAGING_ACCESS_FROM_ALL;
    // SamOs paging_map_range takes a PAGE count, not a byte
    // count. Upstream passes bytes; we convert here so the
    // existing paging API stays consistent.
    size_t total_pages = t_size / PAGING_PAGE_SIZE;
    res = paging_map_range(process->paging_desc, virt_ptr, phys_ptr, total_pages, map_flags);
    if(res < 0){
        goto out;
    }
    *virt_addr_out = virt_ptr;
out:
    return res;
}

// Lecture 166 - map a graphics_info's pixel buffer into
// userspace. Rounds the size up to the next page boundary and
// hands back the virtual base. Optional size_out reports the
// rounded size.
int process_map_graphics_framebuffer_pixels_into_userspace(struct process* process,
                                                           struct graphics_info* graphics_in,
                                                           struct framebuffer_pixel** virt_addr_out,
                                                           size_t* size_out){
    int res = 0;
    struct framebuffer_pixel* pixels = NULL;
    size_t graphics_width  = 0;
    size_t graphics_height = 0;

    if(!graphics_in){
        res = -EINVARG;
        goto out;
    }
    pixels = graphics_in->pixels;
    if(!pixels){
        res = -EINVARG;
        goto out;
    }
    graphics_width  = graphics_in->width;
    graphics_height = graphics_in->height;

    size_t memory_size =
        paging_align_value_to_upper_page(graphics_width * graphics_height *
                                         sizeof(struct framebuffer_pixel));

    int map_flags = PAGING_ACCESS_FROM_ALL | PAGING_CACHE_DISABLED |
                    PAGING_IS_PRESENT | PAGING_IS_WRITEABLE;

    res = process_map_into_userspace(process, pixels, memory_size, map_flags,
                                     (void**)virt_addr_out);
    if(res < 0){
        goto out;
    }
    if(size_out){
        *size_out = memory_size;
    }
out:
    return res;
}
