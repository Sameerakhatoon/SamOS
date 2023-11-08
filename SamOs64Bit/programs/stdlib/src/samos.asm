; Lecture 62 - 64-bit userland stdlib syscall wrappers.
;
; SamOS divergence: arguments go in registers, System V
; style. The book's wrappers pushed each argument on the user stack
; and the kernel read it back with task_get_stack_item. Now:
;
;   rax        syscall number
;   rdi, rsi, rdx, r10, r8, r9    arguments 1-6
;   rax        result
;
; C calls these wrappers with the arguments already in rdi, rsi, rdx,
; rcx, r8, r9, so a wrapper only loads rax (and moves rcx to r10 for
; a fourth argument, as the Linux syscall ABI does: SYSCALL would
; clobber rcx). The kernel preserves every register except rax.

[BITS 64]
section .asm

global print:function
global samos_getkey:function
global samos_getkey_block:function
global samos_malloc:function
global samos_free:function
global samos_putchar:function
global samos_process_load_start:function
global samos_process_get_arguments:function
global samos_system:function
global samos_exit:function
global samos_fopen:function   ; L105
global samos_fclose:function  ; L106
global samos_fread:function    ; L107
global samos_fseek:function    ; L111
global samos_fstat:function    ; L112
global samos_realloc:function  ; L115
global samos_e2e_mark:function ; SamOs e2e: marker write
global samos_udelay:function   ; L198 - userspace udelay / task sleep
global samos_window_create:function  ; L154
global samos_sysout_to_window:function ; L158
global samos_get_window_event:function ; L163
global samos_fwrite:function
global samos_ftruncate:function
global samos_unlink:function
global samos_mkdir:function
global samos_rmdir:function
global samos_opendir:function
global samos_readdir:function
global samos_getpid:function
global samos_waitpid:function
global samos_kill:function
global samos_sleep_ms:function
global samos_uptime:function

; One wrapper per syscall: rax = number, int 0x80, return rax.
%macro SYSCALL_WRAPPER 2
%1:
    mov  rax, %2
    int  0x80
    ret
%endmacro

; Four or more arguments: the fourth comes in rcx, goes in r10.
%macro SYSCALL_WRAPPER4 2
%1:
    mov  r10, rcx
    mov  rax, %2
    int  0x80
    ret
%endmacro

SYSCALL_WRAPPER  print,                       1   ; void print(const char* msg)
SYSCALL_WRAPPER  samos_getkey,                2   ; int samos_getkey()
SYSCALL_WRAPPER  samos_putchar,               3   ; void samos_putchar(char c)
SYSCALL_WRAPPER  samos_malloc,                4   ; void* samos_malloc(size_t size)
SYSCALL_WRAPPER  samos_free,                  5   ; void samos_free(void* ptr)
SYSCALL_WRAPPER  samos_process_load_start,    6   ; int (const char* filename)
SYSCALL_WRAPPER  samos_system,                7   ; int (struct command_argument*)
SYSCALL_WRAPPER  samos_process_get_arguments, 8   ; void (struct process_arguments*)
SYSCALL_WRAPPER  samos_exit,                  9   ; void samos_exit(int status)
SYSCALL_WRAPPER  samos_fopen,                 10  ; int (const char* filename, const char* mode)

; Lecture 106 - long samos_fclose(size_t fd)
;
; (files): the upstream peachos_fclose loaded
; rax and returned without the int 0x80, so nothing was ever closed
; until the process exited. A shell that opens a file per command
; ran out of the 512 kernel descriptors. It is a real syscall now.
SYSCALL_WRAPPER  samos_fclose,                11  ; long (size_t fd)

SYSCALL_WRAPPER4 samos_fread,                 12  ; long (void* buf, size_t size, size_t count, long fd)
SYSCALL_WRAPPER  samos_fseek,                 13  ; long (long fd, long offset, long whence)
SYSCALL_WRAPPER  samos_fstat,                 14  ; long (long fd, struct file_stat* out)
SYSCALL_WRAPPER  samos_realloc,               15  ; void* (void* old_ptr, size_t new_size)
SYSCALL_WRAPPER4 samos_window_create,         16  ; void* (title, w, h, flags, id)
SYSCALL_WRAPPER  samos_sysout_to_window,      17  ; void (void* user_window)
SYSCALL_WRAPPER  samos_get_window_event,      18  ; long (void* event_out)
SYSCALL_WRAPPER  samos_udelay,                25  ; void (uint64_t microseconds)
SYSCALL_WRAPPER  samos_e2e_mark,              26  ; int (uint32_t slot, uint32_t value)
SYSCALL_WRAPPER  samos_getpid,                27  ; int samos_getpid(void)
SYSCALL_WRAPPER  samos_waitpid,               28  ; int samos_waitpid(int pid, int* status)
SYSCALL_WRAPPER  samos_kill,                  29  ; int samos_kill(int pid)
SYSCALL_WRAPPER  samos_sleep_ms,              30  ; void samos_sleep_ms(unsigned long ms)
SYSCALL_WRAPPER  samos_uptime,                31  ; unsigned long samos_uptime(void)
SYSCALL_WRAPPER  samos_getkey_block,          32  ; int samos_getkey_block(void)
SYSCALL_WRAPPER4 samos_fwrite,                40  ; long (const void* buf, size_t size, size_t count, long fd)
SYSCALL_WRAPPER  samos_ftruncate,             41  ; int (long fd, unsigned long size)
SYSCALL_WRAPPER  samos_unlink,                42  ; int (const char* path)
SYSCALL_WRAPPER  samos_mkdir,                 43  ; int (const char* path)
SYSCALL_WRAPPER  samos_rmdir,                 44  ; int (const char* path)
SYSCALL_WRAPPER  samos_opendir,               45  ; int (const char* path)
SYSCALL_WRAPPER  samos_readdir,               46  ; int (long fd, struct file_dirent* out)
