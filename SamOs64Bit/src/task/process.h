#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "task.h"
#include "config.h"
#include "kernel.h"

#define PROCESS_FILETYPE_ELF      0
#define PROCESS_FILETYPE_BINARY   1

typedef unsigned char PROCESS_FILETYPE;

struct elf_file;

struct process_allocation {
    void*  ptr;
    size_t size;
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
    struct process_allocation allocations[SAMOS_MAX_PROGRAM_ALLOCATIONS];

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

#endif
