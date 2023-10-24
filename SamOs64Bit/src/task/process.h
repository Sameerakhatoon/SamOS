#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "task.h"
#include "fs/file.h"   // L111 - FILE_SEEK_MODE
#include "config.h"
#include "kernel.h"

// L113 - struct process now carries a paging_desc pointer.
// Forward-declared so callers do not need paging.h just to
// take struct process*.
struct paging_desc;

#define PROCESS_FILETYPE_ELF      0
#define PROCESS_FILETYPE_BINARY   1

// Lecture 160 - per-process window-event ring. 1000 is the
// upstream ceiling.
#define PROCESS_MAX_WINDOW_EVENTS_RECORDED 1000

typedef unsigned char PROCESS_FILETYPE;

struct elf_file;
// Lecture 154 - forward decls so embedders that only take a
// struct process* do not need to drag the graphics headers in.
struct window;
struct graphics_info;
struct window_event;
// Lecture 168 - hoisted forward decl so the L166 helper sigs at
// the bottom of this header compile without including graphics.h.
struct framebuffer_pixel;

// What a process_allocation range is.
enum {
    PROCESS_ALLOCATION_HEAP    = 1,  // process_malloc: the process may free it
    PROCESS_ALLOCATION_SEGMENT = 2,  // an ELF PT_LOAD segment
    PROCESS_ALLOCATION_FOREIGN = 3,  // kernel memory owned elsewhere (pixels, flat binary)
};

struct process_allocation {
    void*  ptr;     // The USER address (page aligned)
    // Lecture 108 - exclusive end of the allocation. ptr + size,
    // cached so range-vs-allocation tests do not have to add.
    void*  end;
    size_t size;
    void*  kptr;    // Kernel address of the backing pages
    int    kind;    // PROCESS_ALLOCATION_*
};

struct command_argument {
    char argument[512];
    struct command_argument* next;
};

struct process_arguments {
    int    argc;
    char** argv;
};

// Lecture 105 - per-process file descriptor record. The kernel fd
// returned by fopen plus enough metadata to reopen the same file
// if we ever need to (process snapshot/restore, debugging).
struct process_file_handle {
    int   fd;
    char  file_path[SAMOS_MAX_PATH];
    char  mode[4];   // "r", "w", "w+": room for the NUL (upstream had 2)
};

// Lecture 154 - userspace-visible window descriptor.
// Mirrors the kernel window's title and dimensions so userland
// can read them without resolving a kernel pointer.
struct process_userspace_window {
    char title[WINDOW_MAX_TITLE];
    int  width;
    int  height;
};

// Lecture 154 - kernel-side per-window record. Pairs the
// userspace handle with the real kernel window so the syscall
// layer can translate one to the other.
struct process_window {
    struct process_userspace_window* user_win;
    struct window*                   kernel_win;
};

// Exit status of a process that did not call exit itself:
// 128 + the exception vector for a fault, 128 + 9 for kill (the
// shell's 128 + signal convention).
#define PROCESS_EXIT_FAULT_BASE  128
#define PROCESS_EXIT_KILLED      (128 + 9)

struct process {
    uint16_t           id;        // slot in process_vector (reused)
    char               filename[SAMOS_MAX_PATH];
    struct task*       task;

    // Getpid, waitpid and kill. pids are never reused.
    // parent is NULL for a process the kernel started, or once the
    // parent has exited. A process that exits while its parent
    // lives stays on the zombie list (zombie_next) with only
    // exit_status, until the parent's waitpid frees it.
    int                pid;
    struct process*    parent;
    int                exit_status;
    struct process*    zombie_next;

    // Lecture 113 - paging descriptor moves from task to process.
    // task_paging_desc() and task_switch() now forward through
    // task->process->paging_desc.
    struct paging_desc* paging_desc;

    // Lecture 108 - allocations was a fixed-size array indexed by
    // SAMOS_MAX_PROGRAM_ALLOCATIONS. It is now a vector of
    // struct process_allocation; new allocations grow the vector,
    // freed slots are nulled in place and reused.
    struct vector*     allocations;

    // Lecture 153 - vector<struct userland_ptr*>. Sentinel
    // values the kernel hands to userland.
    struct vector*     kernel_userland_ptrs_vector;

    // L105 - vector<struct process_file_handle*>. Populated by
    // process_fopen, drained at process_free_process time so a
    // process exit closes its open fds.
    struct vector*     file_handles;

    PROCESS_FILETYPE   filetype;
    union {
        void*            ptr;       // Physical pointer to the process binary in memory.
        struct elf_file* elf_file;  // ELF descriptor (post-Ch 125).
    };

    void*              stack;     // Physical pointer to the process stack.
    uint32_t           size;      // Size of the raw binary at ptr (BINARY filetype only).

    struct keyboard_buffer {
        char buffer[SAMOS_KEYBOARD_BUFFER_SIZE];
        int  tail;
        int  head;
    } keyboard;

    // Arguments passed in by the spawner (kernel or another process).
    struct process_arguments arguments;

    // Lecture 154 - vector<struct process_window*>. One entry per
    // window this process owns.
    struct vector*     windows;

    // Lecture 158 - if set, isr80h_command1_print and
    // isr80h_command3_putchar route into this window's terminal
    // instead of the kernel console. Userland selects it via
    // peachos_divert_stdout_to_window.
    struct process_window* sysout_win;

    // Lecture 160 - bounded ring of window_events delivered to
    // this process. `vector` is pre-grown to
    // PROCESS_MAX_WINDOW_EVENTS_RECORDED. `index` is the read
    // head; producers vector_overwrite at index + total_unpopped,
    // consumers read at index and advance it.
    struct {
        struct vector* vector;
        size_t         index;
        size_t         total_unpopped;
    } window_events;
};

int              process_load(const char* filename, struct process** process);
int              process_load_for_slot(const char* filename, struct process** process, int process_slot);
int              process_switch(struct process* process);
int              process_load_switch(const char* filename, struct process** process);
void*            process_malloc(struct process* process, size_t size);
// As process_malloc, and *kptr_out is the kernel view of the
// same memory, for kernel code that fills it in.
void*            process_malloc_kernel(struct process* process, size_t size, void** kptr_out);
// Lecture 115 - userland-facing realloc. Reuses the existing
// allocation record when present, calls krealloc for the
// physical reshape, and rewires the slot's ptr/end/size.
void*            process_realloc(struct process* process, void* old_virt_ptr, size_t new_size);
void             process_free(struct process* process, void* ptr);
void             process_get_arguments(struct process* process, int* argc, char*** argv);
int              process_inject_arguments(struct process* process, struct command_argument* root_argument);
int              process_terminate(struct process* process);
struct process*  process_current();
struct process*  process_get(int process_id);

// Exit status, wait, pids, keyboard fallback.
int              process_exit(struct process* process, int status);
int              process_reap_child(struct process* parent, int pid, int* status_out);
struct process*  process_get_by_pid(int pid);
struct process*  process_foreground(void);

// Lecture 105 - per-process fopen wrapper + lookup.
int                            process_fopen(struct process* process,
                                             const char* path, const char* mode);
struct process_file_handle*    process_file_handle_get(struct process* process, int fd);
int                            process_fclose(struct process* process, int fd);
int                            process_fread(struct process* process, void* virt_ptr,
                                             uint64_t size, uint64_t nmemb, int fd);
int                            process_fseek(struct process* process, int fd,
                                             int offset, FILE_SEEK_MODE whence);
int                            process_fstat(struct process* process, int fd,
                                             struct file_stat* virt_filestat_addr);

// Lecture 108 - vector-backed allocation table accessors.
int                            process_find_free_allocation_index(struct process* process);
int                            process_get_allocation_by_start_addr(struct process* process,
                                                                    void* addr,
                                                                    struct process_allocation* allocation_out);

// The L109 range query, stack test and
// process_validate_memory_or_terminate are gone: syscalls check user
// memory with uaccess.h.

// Lecture 114 - allocate the process slot vector. Must be
// called before any process_load.
void                           process_system_init(void);

// Lecture 154 - userspace window create + lookup helpers.
struct process_window*  process_window_create(struct process* process,
                                              char* title,
                                              int width, int height,
                                              int flags, int id);
bool                    process_owns_kernel_window(struct process* process,
                                                   struct window* kernel_window);
struct process*         process_get_from_kernel_window(struct window* window);
struct process_window*  process_window_get_from_user_window(struct process* process,
                                                            struct process_userspace_window* user_win);
void                    process_close_windows(struct process* process);

// Lecture 158 - sysout-window helpers.
void                    process_print_char(struct process* process, char c);
void                    process_print(struct process* process, const char* message);
void                    process_set_sysout_window(struct process* process,
                                                  struct process_window* win);

// Lecture 161 - per-process window-event push and pop.
int                     process_push_window_event(struct process* process,
                                                  struct window_event* event);
int                     process_pop_window_event(struct process* process,
                                                 struct window_event* event_out);

// Lecture 166 - userspace mapping helpers. Hand a kernel buffer
// to the process address space (must be page-aligned).
int                     process_map_into_userspace(struct process* process,
                                                   void* kernel_ptr, size_t t_size,
                                                   int map_flags, void** virt_addr_out);
int                     process_map_graphics_framebuffer_pixels_into_userspace(struct process* process,
                                                                               struct graphics_info* graphics_in,
                                                                               struct framebuffer_pixel** virt_addr_out,
                                                                               size_t* size_out);

#endif
