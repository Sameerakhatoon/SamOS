#include "process.h"
#include "config.h"
#include "status.h"
#include "task/task.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "arch/arch.h"
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
#include "io/serial.h"     // Mirror process output to COM1
#include "uaccess.h"       // Checked user access
#include "lib/spinlock/spinlock.h"
#include <stdbool.h>

// The process table: process_vector, pids, parent
// links, the zombie list, the foreground process and the keyboard
// rings. Recursive, so lookups can be made with it held. Never held
// across file I/O or window calls (the graphics lock comes first).
static struct spinlock process_table_lock = SPINLOCK_INIT("process", LOCK_LEVEL_PROCESS, 1);

void process_lock(void){
    spin_lock(&process_table_lock);
}

void process_unlock(void){
    spin_unlock(&process_table_lock);
}

// The process the keyboard falls back to when no process
// window has the focus: the one the kernel or a parent started in
// the foreground (process_load_switch), handed back to the parent
// when it exits.
static struct process* foreground_process = 0;

// Exited processes whose parent has not waited for them yet.
// Everything but the struct itself is already freed.
static struct process* zombie_head = 0;

// Pids are never reused (slots are), so a wait for an old pid
// cannot reap a newer process in the same slot.
static int process_next_pid = 1;

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
    if(!process->windows){
        return false;       // Exiting (process_terminate)
    }
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
    struct process* found = NULL;
    process_lock();
    size_t total_processes = vector_count(process_vector);
    for(size_t i = 0; i < total_processes && !found; i++){
        struct process* process = NULL;
        vector_at(process_vector, i, &process, sizeof(process));
        if(process && process_owns_kernel_window(process, window)){
            found = process;
        }
    }
    process_unlock();
    return found;
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

// Lecture 162 - kernel-side reaction to a focus event. The book made
// the focused process "current" (process_current_set), which was
// also what the keyboard pushed to, whichever task was running.
// keyboard_push asks window_focused() for the owner instead,
// so there is nothing to record here.
int process_window_event_handler_event_focus(struct window* window,
                                             struct process* process,
                                             struct window_event* event){
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
// success.
//
// SamOS divergence: the
// book read slot 0 every time and the producer wrote at `index`,
// which nothing ever advanced, so every event landed in slot 0 and
// a reader got the newest event once per event pushed. `index` is
// now the read head; events sit at index .. index + total_unpopped.
int process_pop_window_event(struct process* process, struct window_event* event_out){
    int res = -EOUTOFRANGE;
    if(!process || !event_out){
        res = -EINVARG;
        goto out;
    }
    if(process->window_events.total_unpopped > 0){
        vector_at(process->window_events.vector, process->window_events.index,
                  event_out, sizeof(struct window_event));
        process->window_events.index =
            (process->window_events.index + 1) % PROCESS_MAX_WINDOW_EVENTS_RECORDED;
        process->window_events.total_unpopped--;
        res = 0;
    }
out:
    return res;
}

// Lecture 161 - enqueue a window event into the ring. We null
// the kernel-only window pointer before storing because the
// event might outlive that pointer's mapping. A full ring
// drops its oldest event.
int process_push_window_event(struct process* process, struct window_event* event){
    if(process->window_events.total_unpopped == PROCESS_MAX_WINDOW_EVENTS_RECORDED){
        process->window_events.index =
            (process->window_events.index + 1) % PROCESS_MAX_WINDOW_EVENTS_RECORDED;
        process->window_events.total_unpopped--;
    }
    size_t element_index = (process->window_events.index + process->window_events.total_unpopped) %
                           PROCESS_MAX_WINDOW_EVENTS_RECORDED;
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

    // User memory, filled in through its kernel view.
    struct process_userspace_window* user_win_k = NULL;
    proc_win->user_win = process_malloc_kernel(process, sizeof(struct process_userspace_window),
                                               (void**)&user_win_k);
    if(!proc_win->user_win){
        res = -ENOMEM;
        goto out;
    }

    user_win_k->width  = width;
    user_win_k->height = height;
    strncpy(user_win_k->title, title, sizeof(user_win_k->title) - 1);

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
// the sysout window's terminal.
//
// SamOs divergence: every byte a process prints is also
// mirrored to the COM1 serial console, and a process that never
// redirected its output (sysout_win == NULL) falls back to the
// kernel console instead of dereferencing NULL as upstream does.
// The screen side under the graphics lock.
void process_print_char(struct process* process, char c){
    serial_putc(c);
    gfx_lock();
    struct process_window* printing_process_win = process->sysout_win;
    if(!printing_process_win){
        terminal_writechar(c, 15);
    } else {
        struct terminal* win_term = window_terminal(printing_process_win->kernel_win);
        if(win_term){
            terminal_write(win_term, c);
        }
    }
    gfx_unlock();
}

void process_print(struct process* process, const char* message){
    serial_write(message);
    gfx_lock();
    struct process_window* printing_process_win = process->sysout_win;
    if(!printing_process_win){
        for(const char* p = message; *p; p++){
            terminal_writechar(*p, 15);
        }
    } else {
        struct terminal* win_term = window_terminal(printing_process_win->kernel_win);
        if(win_term){
            terminal_print(win_term, message);
        }
    }
    gfx_unlock();
}

void process_set_sysout_window(struct process* process, struct process_window* win){
    process->sysout_win = win;
}

// The process of the running task. The book kept a separate
// current_process that focus events and process_switch moved, so it
// could name a process that was not running.
struct process* process_current(){
    struct task* task = task_current();
    return task ? task->process : NULL;
}

struct process* process_foreground(void){
    return foreground_process;
}

// The live process with this pid, or NULL.
struct process* process_get_by_pid(int pid){
    size_t total = vector_count(process_vector);
    for(size_t i = 0; i < total; i++){
        struct process* process = NULL;
        vector_at(process_vector, i, &process, sizeof(process));
        if(process && process->pid == pid){
            return process;
        }
    }
    return NULL;
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
    // The flat image sits between 0x400000 and the heap.
    if(stat.filesize == 0 ||
       stat.filesize > SAMOS_USER_HEAP_START - SAMOS_PROGRAM_VIRTUAL_ADDRESS){
        res = -EBADEXEC;
        goto out;
    }

    program_data_ptr = kpage_zalloc(stat.filesize);   // Mapped at 0x400000
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

// Only a file without the ELF signature falls back to a flat
// binary. A file that claims to be ELF and fails the checks is
// -EBADEXEC and is not run at all.
static int process_load_data(const char* filename, struct process* process){
    int res = 0;
    res = process_load_elf(filename, process);
    if(res == -EINFORMAT){
        res = process_load_binary(filename, process);
    }
    return res;
}

// ------------------------------------------------------------------
// The process address space.
//
// Every user page is a kernel page (kpage_zalloc, so whole
// frames) mapped at a user virtual address that has nothing to do
// with its kernel address. process->allocations records each range:
// the user address, the size, the kernel address of the backing
// pages and what kind of range it is.
// ------------------------------------------------------------------

// Map `bytes` of kernel memory at `kptr` to user address `uva`.
static int process_map_pages(struct process* process, uintptr_t uva, void* kptr,
                             size_t bytes, int flags){
    for(size_t off = 0; off < bytes; off += PAGING_PAGE_SIZE){
        uint64_t phys = paging_kernel_virt_to_phys((char*)kptr + off);
        if(!phys){
            return -EFAULT;
        }
        int res = paging_map(process->paging_desc, (void*)(uva + off), (void*)phys,
                             flags | PAGING_ACCESS_FROM_ALL | PAGING_IS_PRESENT);
        if(res < 0){
            return res;
        }
    }
    return 0;
}

static void process_unmap_pages(struct process* process, uintptr_t uva, size_t bytes){
    for(size_t off = 0; off < bytes; off += PAGING_PAGE_SIZE){
        paging_map(process->paging_desc, (void*)(uva + off), NULL, 0);
    }
}

static size_t process_allocation_span(struct process_allocation* a){
    return paging_align_value_to_upper_page(a->size);
}

// Lecture 108 - vector-walk variants of the allocation
// bookkeeping. process_find_free_allocation_index now appends a
// zeroed slot when no free entry exists.
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

// Lowest page-aligned user address from SAMOS_USER_HEAP_START where
// `bytes` fit, with an unmapped guard page after every existing
// range. 0 when the user half is full.
static uintptr_t process_va_reserve(struct process* process, size_t bytes){
    uintptr_t cand = SAMOS_USER_HEAP_START;
    size_t total = vector_count(process->allocations);
    bool moved = true;
    while(moved){
        moved = false;
        if(bytes > SAMOS_USER_SPACE_END - cand){
            return 0;
        }
        for(size_t i = 0; i < total; i++){
            struct process_allocation a;
            if(vector_at(process->allocations, i, &a, sizeof(a)) < 0 || !a.ptr){
                continue;
            }
            uintptr_t s = (uintptr_t)a.ptr;
            uintptr_t e = s + process_allocation_span(&a) + PAGING_PAGE_SIZE;
            if(cand < e && s < cand + bytes + PAGING_PAGE_SIZE){
                cand  = e;
                moved = true;
            }
        }
    }
    return cand;
}

static int process_allocation_record(struct process* process, uintptr_t uva, size_t size,
                                     void* kptr, int kind){
    int index = process_find_free_allocation_index(process);
    if(index < 0){
        return -ENOMEM;
    }
    struct process_allocation allocation = {
        .ptr = (void*)uva, .end = (void*)(uva + size), .size = size,
        .kptr = kptr, .kind = kind,
    };
    return vector_overwrite(process->allocations, index, &allocation, sizeof(allocation));
}

// Index of the allocation that starts at `uva`, or -ENOTFOUND.
static int process_allocation_index(struct process* process, void* uva,
                                    struct process_allocation* out){
    if(!uva){
        return -ENOTFOUND;
    }
    size_t total = vector_count(process->allocations);
    for(size_t i = 0; i < total; i++){
        struct process_allocation a;
        if(vector_at(process->allocations, i, &a, sizeof(a)) < 0){
            break;
        }
        if(a.ptr == uva){
            if(out){
                *out = a;
            }
            return (int)i;
        }
    }
    return -ENOTFOUND;
}

// Unmap the range, free its pages if the process owns them, clear
// the record.
static void process_allocation_release(struct process* process, int index){
    struct process_allocation a;
    if(vector_at(process->allocations, index, &a, sizeof(a)) < 0 || !a.ptr){
        return;
    }
    process_unmap_pages(process, (uintptr_t)a.ptr, process_allocation_span(&a));
    if(a.kptr && a.kind != PROCESS_ALLOCATION_FOREIGN){
        kfree(a.kptr);
    }
    struct process_allocation empty = {0};
    vector_overwrite(process->allocations, index, &empty, sizeof(empty));
}

// Lecture 108 - by-start-addr lookup. Fills `allocation_out`
// with a COPY of the matched record and returns 0; returns
// -EIO when the address is not the start of any allocation.
int process_get_allocation_by_start_addr(struct process* process, void* addr,
                                         struct process_allocation* allocation_out){
    return process_allocation_index(process, addr, allocation_out) < 0 ? -EIO : 0;
}

// Lecture 115 - look up an allocation by ptr and report its
// index. Returns -ENOTFOUND on miss; 0 on hit with the index
// written to *index_out if non-NULL.
int process_allocation_exists(struct process* process, void* ptr, size_t* index_out){
    int index = process_allocation_index(process, ptr, NULL);
    if(index < 0){
        return index;
    }
    if(index_out){
        *index_out = (size_t)index;
    }
    return 0;
}

// User memory for the process, and its kernel view for code
// that fills it in (window records, argv). The book returned the
// kernel pointer and identity-mapped it into the process, which is
// how every process came to see the kernel heap.
void* process_malloc_kernel(struct process* process, size_t size, void** kptr_out){
    if(size == 0 || size > SAMOS_USER_SPACE_END - SAMOS_USER_HEAP_START){
        return NULL;
    }
    size_t bytes = paging_align_value_to_upper_page(size);
    void* kptr = kpage_zalloc(bytes);
    if(!kptr){
        return NULL;
    }
    uintptr_t uva = process_va_reserve(process, bytes);
    if(!uva || process_map_pages(process, uva, kptr, bytes,
                                 PAGING_IS_WRITEABLE | PAGING_NO_EXECUTE) < 0 ||
       process_allocation_record(process, uva, size, kptr, PROCESS_ALLOCATION_HEAP) < 0){
        if(uva){
            process_unmap_pages(process, uva, bytes);
        }
        kfree(kptr);
        return NULL;
    }
    if(kptr_out){
        *kptr_out = kptr;
    }
    return (void*)uva;
}

void* process_malloc(struct process* process, size_t size){
    return process_malloc_kernel(process, size, NULL);
}

// Lecture 115 - userland realloc.
//
// Allocate, copy, free. The book translated the user
// pointer and kreallocated the kernel block in place, which only
// worked because user and kernel addresses were the same.
void* process_realloc(struct process* process, void* old_virt_ptr, size_t new_size){
    // Lecture 201 - realloc(NULL, n) is malloc(n).
    if(old_virt_ptr == NULL){
        return process_malloc(process, new_size);
    }
    struct process_allocation old;
    int index = process_allocation_index(process, old_virt_ptr, &old);
    if(index < 0 || old.kind != PROCESS_ALLOCATION_HEAP){
        return NULL;
    }
    if(new_size == 0){
        process_allocation_release(process, index);
        return NULL;
    }
    void* new_kptr = NULL;
    void* new_ptr = process_malloc_kernel(process, new_size, &new_kptr);
    if(!new_ptr){
        return NULL;
    }
    memcpy(new_kptr, old.kptr, (int)(old.size < new_size ? old.size : new_size));
    process_allocation_release(process, index);
    return new_ptr;
}

int process_terminate_allocations(struct process* process){
    size_t total_allocations = vector_count(process->allocations);
    for(size_t i = 0; i < total_allocations; i++){
        process_allocation_release(process, (int)i);
    }
    return 0;
}

// Free a heap allocation or drop a foreign mapping. ELF
// segments are not the user's to free.
void process_free(struct process* process, void* ptr){
    struct process_allocation allocation;
    int index = process_allocation_index(process, ptr, &allocation);
    if(index < 0 || allocation.kind == PROCESS_ALLOCATION_SEGMENT){
        return;
    }
    process_allocation_release(process, index);
}

// Flat binary: one RWX range at 0x400000, since there are no
// segment flags to go by.
int process_map_binary(struct process* process){
    size_t bytes = paging_align_value_to_upper_page(process->size);
    int res = process_map_pages(process, SAMOS_PROGRAM_VIRTUAL_ADDRESS, process->ptr, bytes,
                                PAGING_IS_WRITEABLE);
    if(res < 0){
        return res;
    }
    // The pages stay owned by process->ptr (process_free_binary_data).
    return process_allocation_record(process, SAMOS_PROGRAM_VIRTUAL_ADDRESS, bytes,
                                     process->ptr, PROCESS_ALLOCATION_FOREIGN);
}

// Lecture 63 - ELF mapping re-enabled.
//
// Each PT_LOAD gets fresh zeroed pages.
// p_filesz bytes are copied from the file at p_vaddr; the rest,
// .bss included, stays zero. The book mapped the file buffer itself,
// so .bss was whatever followed the segment in the file, and the
// zero-fill wrote over the wrong segment. Permissions come from
// p_flags: W only with PF_W, NX unless PF_X. elf_load has already
// rejected W+X segments and overlapping pages.
static int process_map_elf(struct process* process){
    struct elf_file*   elf_file = process->elf_file;
    struct elf_header* header   = elf_header(elf_file);

    for(int i = 0; i < header->e_phnum; i++){
        struct elf64_phdr* phdr = elf_program_header(header, i);
        if(phdr->p_type != PT_LOAD || phdr->p_memsz == 0){
            continue;
        }
        uintptr_t start = (uintptr_t)paging_align_to_lower_page((void*)phdr->p_vaddr);
        size_t    bytes = paging_align_value_to_upper_page(phdr->p_vaddr + phdr->p_memsz) - start;
        char* kptr = kpage_zalloc(bytes);
        if(!kptr){
            return -ENOMEM;
        }
        memcpy(kptr + (phdr->p_vaddr - start), (char*)elf_memory(elf_file) + phdr->p_offset,
               (int)phdr->p_filesz);

        int flags = 0;
        if(phdr->p_flags & PF_W){
            flags |= PAGING_IS_WRITEABLE;
        }
        if(!(phdr->p_flags & PF_X)){
            flags |= PAGING_NO_EXECUTE;
        }
        int res = process_map_pages(process, start, kptr, bytes, flags);
        if(res >= 0){
            res = process_allocation_record(process, start, bytes, kptr, PROCESS_ALLOCATION_SEGMENT);
        }
        if(res < 0){
            process_unmap_pages(process, start, bytes);
            kfree(kptr);
            return res;
        }
    }
    return 0;
}

int process_map_memory(struct process* process){
    int res = 0;

    // Lecture 113 - identity-mapped every E820-usable region into
    // the process's PML4, user-writable. The
    // process gets its segments, its stack and its heap, and shares
    // the supervisor-only kernel half (paging_desc_new).
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
    // from END. NX, and the page below it
    // (SAMOS_USER_STACK_GUARD) is never mapped, so an overflow faults
    // instead of running into whatever was there.
    res = process_map_pages(process, SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END, process->stack,
                            SAMOS_USER_PROGRAM_STACK_SIZE,
                            PAGING_IS_WRITEABLE | PAGING_NO_EXECUTE);

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

static int process_free_program_data(struct process* process);

// Undo a process_load_for_slot that failed before the
// process was linked in (a failed load used to leak the
// process). A malformed ELF goes through here every time.
static void process_load_cleanup(struct process* process){
    if(process->paging_desc){
        process_terminate_allocations(process);
        paging_desc_free(process->paging_desc);
    }
    // ptr and elf_file are one union (process.h), so an ELF that a
    // later step refused is closed here too.
    if(process->ptr){
        process_free_program_data(process);
    }
    kfree(process->stack);
    process_close_file_handles(process);
    struct vector* vectors[] = { process->allocations, process->kernel_userland_ptrs_vector,
                                 process->windows, process->window_events.vector };
    for(size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++){
        if(vectors[i]){
            vector_free(vectors[i]);
        }
    }
    kfree(process);
}

int process_load_for_slot(const char* filename, struct process** process, int process_slot){
    int res = 0;
    struct task* task = 0;
    struct process* _process = 0;
    void* program_stack_ptr = 0;

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

    program_stack_ptr = kpage_zalloc(SAMOS_USER_PROGRAM_STACK_SIZE);  // Mapped, whole pages
    if(!program_stack_ptr){
        res = -ENOMEM;
        goto out;
    }

    strncpy(_process->filename, filename, sizeof(_process->filename));
    _process->stack = program_stack_ptr;

    // Lecture 113 - the paging descriptor moves from task to
    // process. Create it BEFORE task_new so process_map_memory
    // (which now hangs off process->paging_desc, and which is
    // also called BEFORE task_new) sees a live descriptor.
    _process->paging_desc = paging_desc_new();
    if(!_process->paging_desc){
        res = -ENOMEM;
        goto out;
    }

    res = process_map_memory(_process);
    if(res < 0){
        goto out;
    }

    task = task_new(_process);
    if(ISERR(task)){
        res = ERROR_I(task);
        task = NULL;
        goto out;
    }
    _process->task = task;

    // Slot and pid are taken here, under the process lock,
    // after the slow part of the load: two CPUs loading at once got
    // the same free slot when it was looked up first. A slot < 0
    // means any free one (process_load).
    process_lock();
    if(process_slot < 0){
        process_slot = process_get_free_slot();
    } else if(process_get(process_slot) != 0){
        process_slot = -EISTKN;
    }
    if(process_slot >= 0){
        _process->id  = process_slot;
        _process->pid = process_next_pid++;
        // Lecture 114 - take the slot by overwriting it with our
        // pointer.
        vector_overwrite(process_vector, process_slot, &_process, sizeof(&_process));
    }
    process_unlock();
    if(process_slot < 0){
        res = process_slot;
        goto out;
    }
    *process = _process;

out:
    if(ISERR(res) && _process){
        if(task){
            task_free(task);
            _process->task = NULL;
        }
        process_load_cleanup(_process);
    }
    return res;
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

static int process_free_program_data(struct process* process){
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

// Lecture 114 - null the slot via vector_overwrite rather than
// poking the array.
// The book then switched current_process to any other process
// and panicked with "No processes to switch too" when none was left
// (a dead end). The scheduler idles instead.
static void process_unlink(struct process* process){
    struct process* null_process = NULL;
    vector_overwrite(process_vector, process->id, &null_process, sizeof(&null_process));
}

// An exiting parent: its live children lose their parent
// (nobody will wait for them), its unreaped zombies are freed.
static void process_orphan_children(struct process* parent){
    size_t total = vector_count(process_vector);
    for(size_t i = 0; i < total; i++){
        struct process* process = NULL;
        vector_at(process_vector, i, &process, sizeof(process));
        if(process && process->parent == parent){
            process->parent = NULL;
        }
    }
    struct process** link = &zombie_head;
    while(*link){
        struct process* zombie = *link;
        if(zombie->parent == parent){
            *link = zombie->zombie_next;
            kfree(zombie);
        } else {
            link = &zombie->zombie_next;
        }
    }
}

// Waitpid for `parent`: pid > 0 is that child, anything else
// any child. Returns the child's pid and its status in *status_out,
// 0 if a matching child is still running, -ECHILD if there is none.
int process_reap_child(struct process* parent, int pid, int* status_out){
    for(struct process** link = &zombie_head; *link; link = &(*link)->zombie_next){
        struct process* zombie = *link;
        if(zombie->parent != parent || (pid > 0 && zombie->pid != pid)){
            continue;
        }
        int child_pid = zombie->pid;
        *status_out = zombie->exit_status;
        *link = zombie->zombie_next;
        kfree(zombie);
        return child_pid;
    }
    size_t total = vector_count(process_vector);
    for(size_t i = 0; i < total; i++){
        struct process* process = NULL;
        vector_at(process_vector, i, &process, sizeof(process));
        if(process && process->parent == parent && (pid <= 0 || process->pid == pid)){
            return 0;
        }
    }
    return -ECHILD;
}

// Lecture 162 - the close-event handler calls here once a window
// has gone away. Drop it from the process's owned-windows
// vector, return its userspace mirror to the process heap,
// kfree the kernel record.
void process_window_closed(struct process* process, struct process_window* proc_win){
    // Print must not go on writing to a closed window's
    // terminal.
    if(process->sysout_win == proc_win){
        process->sysout_win = NULL;
    }
    vector_pop_element(process->windows, &proc_win, sizeof(proc_win));
    process_free(process, proc_win->user_win);
    kfree(proc_win);
}

// Every way a process ends: exit(status), a fault (128 +
// vector), kill (128 + 9). The parent's waitpid gets the status.
int process_exit(struct process* process, int status){
    process->exit_status = status;
    return process_terminate(process);
}

// Frees the process completely. The book never freed
// struct process, and bailed out half way on the first error. A
// process with a live parent stays as a zombie (only the struct)
// until the parent waits for it or exits; the others are freed here.
// `process` must not be used after this call.
int process_terminate(struct process* process){
    int res = 0;
    // Lecture 154 - tear down owned windows first; their kernel
    // backing buffers live in kheap and need to come down before
    // process_terminate_allocations walks the per-process arena.
    gfx_lock();
    process_close_windows(process);
    gfx_unlock();
    process_terminate_allocations(process);
    res = process_free_program_data(process);

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
    // Taken out under the process lock: another CPU's
    // process_get_from_kernel_window (a key or a click while this
    // process exits) walks every process's windows with it held.
    if(process->windows){
        process_lock();
        struct vector* windows = process->windows;
        process->windows = NULL;
        process_unlock();
        vector_free(windows);
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
    process->stack = NULL;
    task_free(process->task);
    process->task = NULL;

    // Lecture 113 - the paging descriptor moved off the task,
    // so its free now lives here.
    if(process->paging_desc){
        paging_desc_free(process->paging_desc);
        process->paging_desc = NULL;
    }

    // The table part in one hold of the process lock, so a
    // waiting parent either finds the zombie or is blocked already
    // when it is woken.
    process_lock();
    process_unlink(process);
    process_orphan_children(process);
    if(foreground_process == process){
        foreground_process = process->parent;
    }

    struct process* parent = process->parent;
    if(parent){
        process->zombie_next = zombie_head;
        zombie_head = process;
        if(parent->task && parent->task->state == TASK_BLOCKED &&
           parent->task->block_reason == TASK_BLOCK_WAIT){
            task_wake(parent->task);
        }
    } else {
        kfree(process);
    }
    process_unlock();
    return res;
}

// Lecture 105 - per-process fopen wrapper. Calls the kernel
// fopen and records the descriptor on the process's file_handles
// vector so process exit can fclose it.
//
// The book allocated sizeof(struct process_file_handler*),
// a pointer's worth (and with the typo `_handler`), so the path and
// mode were written past the end of the block. Now the full struct.
//
// A failed open returns the kernel's status (-ENOENT,
// -EISDIR, ...) instead of -EIO for everything. Directory
// descriptors from process_fopendir are recorded the same way (mode
// "d"), so process exit closes them too.
static int process_file_handle_add(struct process* process, int fd,
                                   const char* path, const char* mode){
    if(fd <= 0){
        return fd < 0 ? fd : -EIO;
    }
    struct process_file_handle* handle = kzalloc(sizeof(struct process_file_handle));
    if(!handle || vector_push(process->file_handles, &handle) < 0){
        kfree(handle);
        fclose(fd);
        return -ENOMEM;
    }
    handle->fd = fd;
    strncpy(handle->file_path, path, sizeof(handle->file_path) - 1);
    strncpy(handle->mode,      mode, sizeof(handle->mode) - 1);
    return fd;
}

int process_fopen(struct process* process, const char* path, const char* mode){
    return process_file_handle_add(process, fopen_status(path, mode), path, mode);
}

int process_fopendir(struct process* process, const char* path){
    return process_file_handle_add(process, fopendir(path), path, "d");
}

// Every file syscall goes through this, so a process can only
// use descriptors it opened.
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

// Lecture 107 - userland fread.
//
// The book checked the buffer against its allocation
// with broken arithmetic (it subtracted an offset from an absolute
// address), translated the first byte and let the kernel fread write
// through it, so a read could run off the page into the kernel heap.
// Now the whole user range must be writable, and the data goes
// through a kernel bounce page and copy_to_user.
#define PROCESS_FREAD_CHUNK 4096

int process_fread(struct process* process, void* virt_ptr,
                  uint64_t size, uint64_t nmemb, int fd){
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        return -EINVARG;
    }
    if(size == 0 || nmemb == 0){
        return 0;
    }
    if(nmemb > SAMOS_USER_SPACE_END / size){
        return -EINVARG;
    }
    uint64_t total = size * nmemb;
    if(!user_range_ok(process, virt_ptr, total, true)){
        return -EFAULT;
    }

    char* bounce = kmalloc(PROCESS_FREAD_CHUNK);
    if(!bounce){
        return -ENOMEM;
    }
    int res = 0;
    uint64_t done = 0;
    while(done < total){
        uint32_t chunk = (uint32_t)((total - done) < PROCESS_FREAD_CHUNK ? (total - done) : PROCESS_FREAD_CHUNK);
        int got = fread(bounce, 1, chunk, handle->fd);
        if(got < 0){
            res = got;
            break;
        }
        if(got > 0 && copy_to_user(process, (char*)virt_ptr + done, bounce, (size_t)got) < 0){
            res = -EFAULT;
            break;
        }
        done += (uint64_t)got;
        if((uint32_t)got < chunk){
            break;
        }
    }
    kfree(bounce);
    return res < 0 ? res : (int)(done / size);
}

// Userland fwrite, the mirror of process_fread: the
// whole user range must be readable, and each chunk is copied into a
// kernel bounce page with copy_from_user, which translates every
// page on its own (a buffer may cross pages that are not
// contiguous physically, or not mapped at all).
int process_fwrite(struct process* process, const void* virt_ptr,
                   uint64_t size, uint64_t nmemb, int fd){
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        return -EINVARG;
    }
    if(size == 0 || nmemb == 0){
        return 0;
    }
    if(nmemb > SAMOS_USER_SPACE_END / size){
        return -EINVARG;
    }
    uint64_t total = size * nmemb;
    if(!user_range_ok(process, virt_ptr, total, false)){
        return -EFAULT;
    }

    char* bounce = kmalloc(PROCESS_FREAD_CHUNK);
    if(!bounce){
        return -ENOMEM;
    }
    int res = 0;
    uint64_t done = 0;
    while(done < total){
        uint32_t chunk = (uint32_t)((total - done) < PROCESS_FREAD_CHUNK ? (total - done) : PROCESS_FREAD_CHUNK);
        if(copy_from_user(process, bounce, (const char*)virt_ptr + done, chunk) < 0){
            res = -EFAULT;
            break;
        }
        int put = fwrite(bounce, 1, chunk, handle->fd);
        if(put < 0){
            res = put;
            break;
        }
        done += (uint64_t)put;
        if((uint32_t)put < chunk){
            break;
        }
    }
    kfree(bounce);
    return res < 0 ? res : (int)(done / size);
}

int process_ftruncate(struct process* process, int fd, uint32_t size){
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        return -EINVARG;
    }
    return ftruncate(handle->fd, size);
}

// Returns 1 with the entry copied out, 0 at the end of the
// directory, or a negative status.
int process_freaddir(struct process* process, int fd, struct file_dirent* virt_entry){
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        return -EINVARG;
    }
    if(!user_range_ok(process, virt_entry, sizeof(struct file_dirent), true)){
        return -EFAULT;
    }
    struct file_dirent* entry = kzalloc(sizeof(struct file_dirent));
    if(!entry){
        return -ENOMEM;
    }
    int res = freaddir(handle->fd, entry);
    if(res == 1 && copy_to_user(process, virt_entry, entry, sizeof(*entry)) < 0){
        res = -EFAULT;
    }
    kfree(entry);
    return res;
}

// Lecture 112 - userland fstat.
//
// Checks that the process owns fd (the book did not) and
// writes the result with copy_to_user.
int process_fstat(struct process* process, int fd, struct file_stat* virt_filestat_addr){
    struct process_file_handle* handle = process_file_handle_get(process, fd);
    if(!handle){
        return -EINVARG;
    }
    struct file_stat stat;
    int res = fstat(handle->fd, &stat);
    if(res < 0){
        return res;
    }
    return copy_to_user(process, virt_filestat_addr, &stat, sizeof(stat));
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
    res = fclose(handle->fd);
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

// `root_argument` is a kernel list (syscall 7 copies the
// user's list first). argv and the strings are user memory written
// through their kernel view; argv holds user addresses.
int process_inject_arguments(struct process* process, struct command_argument* root_argument){
    int res = 0;
    struct command_argument* current = root_argument;
    int i = 0;
    int argc = process_count_command_arguments(root_argument);
    if(argc == 0){
        res = -EIO;
        goto out;
    }

    char** kargv = NULL;
    char** argv = process_malloc_kernel(process, sizeof(const char*) * argc, (void**)&kargv);
    if(!argv){
        res = -ENOMEM;
        goto out;
    }

    while(current){
        char* kstr = NULL;
        char* argument_str = process_malloc_kernel(process, sizeof(current->argument), (void**)&kstr);
        if(!argument_str){
            res = -ENOMEM;
            goto out;
        }
        strncpy(kstr, current->argument, sizeof(current->argument) - 1);
        kargv[i] = argument_str;
        current = current->next;
        i++;
    }

    process->arguments.argc = argc;
    process->arguments.argv = argv;

out:
    return res;
}

int process_load(const char* filename, struct process** process){
    return process_load_for_slot(filename, process, -1);
}

// A loaded process runs only from here on.
void process_start(struct process* process){
    task_start(process->task);
}

// "switch" now means: this process takes the keyboard when
// no process window has the focus. It runs when the scheduler
// picks it.
int process_switch(struct process* process){
    process_lock();
    foreground_process = process;
    process_unlock();
    return 0;
}

int process_load_switch(const char* filename, struct process** process){
    int res = process_load(filename, process);
    if(res == 0){
        process_switch(*process);
    }
    return res;
}

// Lecture 166 - map a kernel buffer into the process address
// space. Must be page-aligned at both ends. Hands back the virt
// pointer.
//
// `kernel_ptr` is a kernel virtual address; each page is
// translated to its frame. The range is recorded as FOREIGN: the
// process may unmap it but never frees the memory behind it.
int process_map_into_userspace(struct process* process, void* kernel_ptr,
                               size_t t_size, int map_flags,
                               void** virt_addr_out){
    if(!paging_is_aligned(kernel_ptr) || !paging_is_aligned((char*)kernel_ptr + t_size) ||
       t_size == 0){
        return -EINVARG;
    }
    uintptr_t uva = process_va_reserve(process, t_size);
    if(!uva){
        return -ENOMEM;
    }
    int res = process_map_pages(process, uva, kernel_ptr, t_size,
                                (map_flags & ~PAGING_IS_PRESENT) | PAGING_NO_EXECUTE);
    if(res >= 0){
        res = process_allocation_record(process, uva, t_size, kernel_ptr, PROCESS_ALLOCATION_FOREIGN);
    }
    if(res < 0){
        process_unmap_pages(process, uva, t_size);
        return res;
    }
    *virt_addr_out = (void*)uva;
    return 0;
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

    int map_flags = PAGING_CACHE_DISABLED | PAGING_IS_WRITEABLE;

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
