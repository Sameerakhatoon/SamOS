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

typedef unsigned char PROCESS_FILETYPE;

struct elf_file;

struct process_allocation {
    void*  ptr;
    // Lecture 108 - exclusive end of the allocation. ptr + size,
    // cached so range-vs-allocation tests do not have to add.
    void*  end;
    size_t size;
};

// Lecture 108 - request descriptor used by the upcoming
// allocation-walk helpers (process_get_allocation_by_addr in
// L109). `allocation` is the full record; `peek` carries the
// run-length info for a "validate range" query.
enum {
    PROCESS_ALLOCATION_REQUEST_IS_STACK_MEMORY = 0b00000001,
};

struct process_allocation_request {
    struct process_allocation  allocation;
    int                        flags;
    struct {
        void*  addr;
        void*  end;
        size_t total_bytes_left;
    } peek;
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
    char  mode[2];   // "r", "w", "w+" - max 2 chars per upstream
};

struct process {
    uint16_t           id;
    char               filename[SAMOS_MAX_PATH];
    struct task*       task;

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
};

int              process_load(const char* filename, struct process** process);
int              process_load_for_slot(const char* filename, struct process** process, int process_slot);
int              process_switch(struct process* process);
int              process_load_switch(const char* filename, struct process** process);
void*            process_malloc(struct process* process, size_t size);
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
// Lecture 112 - L107 stub for buffer translation became its own
// helper now that fread/fstat both need it.
void*                          process_virtual_address_to_physical(struct process* process,
                                                                   void* virt_addr);

// Lecture 108 - vector-backed allocation table accessors.
int                            process_find_free_allocation_index(struct process* process);
int                            process_allocation_set_map(struct process* process,
                                                          int allocation_entry_index,
                                                          void* ptr, size_t size);
int                            process_get_allocation_by_start_addr(struct process* process,
                                                                    void* addr,
                                                                    struct process_allocation* allocation_out);

// Lecture 109 - range query + stack-region test.
int                            process_get_allocation_by_addr(struct process* process,
                                                              void* addr,
                                                              struct process_allocation_request* allocation_request_out);
bool                           process_is_stack_memory(struct process* process, void* addr);
int                            process_validate_memory_or_terminate(struct process* process,
                                                                    void* virt_addr,
                                                                    size_t space_needed);

// Lecture 114 - allocate the process slot vector. Must be
// called before any process_load.
void                           process_system_init(void);

#endif
