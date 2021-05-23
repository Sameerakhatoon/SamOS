#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "task.h"
#include "config.h"
#include "kernel.h"

struct process {
    uint16_t       id;
    char           filename[SAMOS_MAX_PATH];
    struct task*   task;
    void*          allocations[SAMOS_MAX_PROGRAM_ALLOCATIONS];
    void*          ptr;       // Physical pointer to the process binary in memory.
    void*          stack;     // Physical pointer to the process stack.
    uint32_t       size;      // Size of the binary at ptr.

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
struct process*  process_current();
struct process*  process_get(int process_id);

#endif
