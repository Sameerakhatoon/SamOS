#ifndef SAMOS_H
#define SAMOS_H

#include <stddef.h>
#include <stdbool.h>

void* samos_malloc(size_t size);
void  samos_free(void* ptr);
void  samos_putchar(char c);
int   samos_getkey();
int   samos_getkeyblock();
void  samos_terminal_readline(char* out, int max, bool output_while_typing);
void  samos_process_load_start(const char* filename);

struct command_argument {
    char argument[512];
    struct command_argument* next;
};

struct process_arguments {
    int    argc;
    char** argv;
};

struct command_argument* samos_parse_command(const char* command, int max);
void samos_process_get_arguments(struct process_arguments* arguments);
int  samos_system(struct command_argument* arguments);
int  samos_system_run(const char* command);
void samos_exit();
void print(const char* msg);

// Lecture 105 - userland fopen syscall trampoline.
int  samos_fopen(const char* filename, const char* mode);
// Lecture 106 - userland fclose trampoline.
void samos_fclose(size_t fd);
// Lecture 107 - userland fread trampoline.
long samos_fread(void* buffer, size_t size, size_t count, long fd);
// Lecture 111 - userland fseek trampoline.
long samos_fseek(long fd, long offset, long whence);

#endif
