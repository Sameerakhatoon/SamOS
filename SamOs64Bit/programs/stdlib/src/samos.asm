; Lecture 62 - 64-bit userland stdlib syscall wrappers.
;
; AMD64 SysV: first integer arg arrives in RDI. The kernel's
; isr80h_handler reads syscall arguments via task_get_stack_item,
; which pulls them off the user's stack. So each wrapper:
;   1. push qword rdi    (put arg 1 on the stack where kernel
;                         can find it via task_get_stack_item(0))
;   2. mov rax, <cmd>     (syscall id in RAX; isr80h_handler reads
;                         it from RAX)
;   3. int 0x80           (trap into kernel)
;   4. add rsp, 8         (clean up the push)
;   5. ret
;
; For arg-less syscalls (getkey, exit) we skip the push/pop.

[BITS 64]
section .asm

global print:function
global samos_getkey:function
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

; void print(const char* msg)
print:
    push qword rdi
    mov  rax, 1            ; SYSTEM_COMMAND1_PRINT
    int  0x80
    add  rsp, 8
    ret

; int samos_getkey()
samos_getkey:
    mov  rax, 2            ; SYSTEM_COMMAND2_GETKEY
    int  0x80
    ret

; void* samos_malloc(size_t size)
samos_malloc:
    push qword rdi
    mov  rax, 4            ; SYSTEM_COMMAND4_MALLOC
    int  0x80
    add  rsp, 8
    ret

; void samos_free(void* ptr)
samos_free:
    push qword rdi
    mov  rax, 5            ; SYSTEM_COMMAND5_FREE
    int  0x80
    add  rsp, 8
    ret

; void samos_putchar(char c)
samos_putchar:
    push qword rdi
    mov  rax, 3            ; SYSTEM_COMMAND3_PUTCHAR
    int  0x80
    add  rsp, 8
    ret

; void samos_process_load_start(const char* filename)
samos_process_load_start:
    push qword rdi
    mov  rax, 6            ; SYSTEM_COMMAND6_PROCESS_LOAD_START
    int  0x80
    add  rsp, 8
    ret

; void samos_process_get_arguments(struct process_arguments* arguments)
samos_process_get_arguments:
    push qword rdi
    mov  rax, 8            ; SYSTEM_COMMAND8_GET_PROGRAM_ARGUMENTS
    int  0x80
    add  rsp, 8
    ret

; int samos_system(struct command_argument* arguments)
samos_system:
    push qword rdi
    mov  rax, 7            ; SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND
    int  0x80
    add  rsp, 8
    ret

; void samos_exit()
samos_exit:
    mov  rax, 9            ; SYSTEM_COMMAND9_EXIT
    int  0x80
    ret

; Lecture 105 - int samos_fopen(const char* filename, const char* mode)
;   rdi = filename, rsi = mode
samos_fopen:
    mov  rax, 10           ; SYSTEM_COMMAND10_FOPEN
    push qword rsi
    push qword rdi
    int  0x80
    add  rsp, 16
    ret

; Lecture 106 - void samos_fclose(size_t fd)
;   rdi = fd
;
; Upstream bug preserved verbatim: the upstream peachos_fclose
; pushes rdi, then has add rsp, 8 and ret WITHOUT issuing the
; int 0x80, so the syscall is never actually invoked. The
; kernel-side handler never runs and the per-process file
; record stays around.
;
; We follow the upstream form for diff hygiene. A linear
; one-instruction fix is to insert `int 0x80` between the push
; and the rsp restore; deferred so the L106 commit mirrors
; upstream exactly.
samos_fclose:
    mov  rax, 11           ; SYSTEM_COMMAND11_FCLOSE
    push qword rdi
    add  rsp, 8
    ret

; Lecture 107 - long samos_fread(void* buffer, size_t size, size_t count, long fd)
;   rdi = buffer, rsi = size, rdx = count, rcx = fd
; Pushes are in stack-item order (item 0 is the LAST push), so the
; kernel sees buffer at item 0, size at item 1, count at item 2,
; fd at item 3.
samos_fread:
    mov  rax, 12           ; SYSTEM_COMMAND12_FREAD
    push qword rcx         ; fd       (stack item 3)
    push qword rdx         ; count    (stack item 2)
    push qword rsi         ; size     (stack item 1)
    push qword rdi         ; buffer   (stack item 0)
    int  0x80
    add  rsp, 32
    ret

; Lecture 111 - long samos_fseek(long fd, long offset, long whence)
;   rdi = fd, rsi = offset, rdx = whence
samos_fseek:
    mov  rax, 13           ; SYSTEM_COMMAND13_FSEEK
    push qword rdx         ; whence (item 2)
    push qword rsi         ; offset (item 1)
    push qword rdi         ; fd     (item 0)
    int  0x80
    add  rsp, 24
    ret

; Lecture 112 - long samos_fstat(long fd, struct file_stat* out)
;   rdi = fd, rsi = out
samos_fstat:
    mov  rax, 14           ; SYSTEM_COMMAND14_FSTAT
    push qword rsi         ; out (item 1)
    push qword rdi         ; fd  (item 0)
    int  0x80
    add  rsp, 16
    ret

; Lecture 115 - void* samos_realloc(void* old_ptr, size_t new_size)
;   rdi = old_ptr, rsi = new_size
; rax returns the new (user-virtual) pointer.
samos_realloc:
    mov  rax, 15           ; SYSTEM_COMMAND15_REALLOC
    push qword rsi         ; new_size (item 1)
    push qword rdi         ; old_ptr  (item 0)
    int  0x80
    add  rsp, 16
    ret

; SamOs e2e: int samos_e2e_mark(uint32_t slot, uint32_t value)
;   rdi = slot, rsi = value
; Stack item 0 = value, item 1 = slot.
samos_e2e_mark:
    mov  rax, 26           ; SYSTEM_COMMAND26_E2E_MARK
    push qword rdi         ; slot  (item 1)
    push qword rsi         ; value (item 0)
    int  0x80
    add  rsp, 16
    ret

; Lecture 198 - void samos_udelay(uint64_t microseconds)
;   rdi = microseconds
; Marks the current task asleep for `microseconds` and yields.
; The kernel's task_next never returns here directly; the IRQ
; timer wakes us up later and execution resumes after int 0x80.
samos_udelay:
    mov  rax, 25           ; SYSTEM_COMMAND25_UDELAY
    push qword rdi         ; microseconds (item 0)
    int  0x80
    add  rsp, 8
    ret

; Lecture 154 - void* samos_window_create(const char* title,
;                                         int w, int h, int flags, int id)
;   rdi = title, rsi = w, rdx = h, rcx = flags, r8 = id
; Stack item 0 = title, item 1 = w, item 2 = h, item 3 = flags,
; item 4 = id.
samos_window_create:
    mov  rax, 16           ; SYSTEM_COMMAND16_WINDOW_CREATE
    push qword r8          ; id     (item 4)
    push qword rcx         ; flags  (item 3)
    push qword rdx         ; height (item 2)
    push qword rsi         ; width  (item 1)
    push qword rdi         ; title  (item 0)
    int  0x80
    add  rsp, 40
    ret

; Lecture 158 - void samos_sysout_to_window(void* user_window)
;   rdi = user_window pointer (returned from samos_window_create)
samos_sysout_to_window:
    mov  rax, 17           ; SYSTEM_COMMAND17_SYSOUT_TO_WINDOW
    push qword rdi
    int  0x80
    add  rsp, 8
    ret

; Lecture 163 - long samos_get_window_event(void* event_out)
;   rdi = pointer to struct window_event_userland to populate
samos_get_window_event:
    mov  rax, 18           ; SYSTEM_COMMAND18_GET_WINDOW_EVENT
    push qword rdi
    int  0x80
    add  rsp, 8
    ret
