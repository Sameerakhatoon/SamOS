#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "task.h"
#include "config.h"
#include "kernel.h"

#define PROCESS_FILETYPE_ELF      0
#define PROCESS_FILETYPE_BINARY   1

typedef unsigned char PROCESS_FILETYPE;

struct elf_file;

struct process {
    uint16_t           id;
    char               filename[SAMOS_MAX_PATH];
    struct task*       task;
    void*              allocations[SAMOS_MAX_PROGRAM_ALLOCATIONS];

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
};

int              process_load(const char* filename, struct process** process);
int              process_load_for_slot(const char* filename, struct process** process, int process_slot);
int              process_switch(struct process* process);
int              process_load_switch(const char* filename, struct process** process);
void*            process_malloc(struct process* process, size_t size);
struct process*  process_current();
struct process*  process_get(int process_id);

#endif
