#ifndef SAMOS_H
#define SAMOS_H

#include <stddef.h>
#include <stdbool.h>

void* samos_malloc(size_t size);
void  samos_free(void* ptr);
void  samos_putchar(char c);
int   samos_getkey();
int   samos_getkeyblock();
// Blocks in the kernel until a key is there for this process.
int   samos_getkey_block();
void  samos_terminal_readline(char* out, int max, bool output_while_typing);
// Returns the child's pid (< 0 on error) like samos_system.
int   samos_process_load_start(const char* filename);

// Processes and time.
int           samos_getpid(void);
// Waits for child `pid` (any child if pid <= 0); returns its pid and
// stores its exit status (main's return value, 128 + vector for a
// fault, 128 + 9 when killed). Negative when there is no such child.
int           samos_waitpid(int pid, int* status);
int           samos_wait(int* status);
int           samos_kill(int pid);
void          samos_sleep_ms(unsigned long ms);
unsigned long samos_uptime(void);   // milliseconds since boot
long          samos_time(void);     // Seconds since 1970 UTC, -1 on error
void          samos_shutdown(void); // Power off, does not return
void          samos_reboot(void);   // Reset, does not return

struct command_argument {
    char argument[512];
    struct command_argument* next;
};

struct process_arguments {
    int    argc;
    char** argv;
};

// Splits on spaces/tabs with shell-style quoting ("a b",
// 'a b', a\ b). Returns NULL for an empty line or an unterminated
// quote. Free the list with samos_free_command.
struct command_argument* samos_parse_command(const char* command, int max);
void samos_free_command(struct command_argument* command);
void samos_process_get_arguments(struct process_arguments* arguments);
// Both return the child's pid, or a negative status.
int  samos_system(struct command_argument* arguments);
int  samos_system_run(const char* command);
void samos_exit(int status);
void print(const char* msg);

// Lecture 105 - userland fopen syscall trampoline.
int  samos_fopen(const char* filename, const char* mode);
// Lecture 106 - userland fclose trampoline. It now
// really makes the syscall; 0 or a negative status.
long samos_fclose(size_t fd);
// Lecture 107 - userland fread trampoline.
long samos_fread(void* buffer, size_t size, size_t count, long fd);
// Lecture 111 - userland fseek trampoline.
long samos_fseek(long fd, long offset, long whence);
// Lecture 112 - userland fstat trampoline.
struct file_stat;
long samos_fstat(long fd, struct file_stat* file_stat_out);
// Lecture 115 - userland realloc trampoline.
void* samos_realloc(void* old_ptr, size_t new_size);

// Syscalls 40-46. Each returns a negative kernel
// status (src/status.h) on failure; file.h has the friendlier names.
struct file_dirent;
long samos_fwrite(const void* buffer, size_t size, size_t count, long fd);
int  samos_ftruncate(long fd, unsigned long size);
int  samos_unlink(const char* path);
int  samos_mkdir(const char* path);
int  samos_rmdir(const char* path);
int  samos_opendir(const char* path);
int  samos_readdir(long fd, struct file_dirent* out);

// SamOs e2e: write `value` into the kernel's bootmarker slot
// `slot`. Slot must be >= 40 (BM_USER_FEATURE_BASE) and below
// BM_STAGE_MAX, otherwise the syscall returns -1.
int samos_e2e_mark(unsigned int slot, unsigned int value);

// Lecture 198 - userspace udelay. Marks the current task asleep
// for the given microsecond count and yields. Returns when the
// scheduler resumes us.
void samos_udelay(unsigned long microseconds);

// Lecture 154 - userspace window create. Returns an opaque user
// handle (pointer that lives in the user mapping) or NULL.
void* samos_window_create(const char* title, int width, int height,
                          int flags, int id);

// Lecture 158 - divert the process's stdout into the given user
// window handle. No-op if the handle is NULL or stale.
void samos_sysout_to_window(void* user_window);

// Lecture 163 - drain a single event off the per-process window
// event ring into the caller-supplied buffer. Returns 0 if an
// event was delivered, non-zero if the ring was empty.
long samos_get_window_event(void* event_out);

#endif
