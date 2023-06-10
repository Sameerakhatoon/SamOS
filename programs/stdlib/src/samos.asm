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
