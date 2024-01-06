; Lecture 41 - task.asm 64-bit rewrite.
;
;   task_return(struct trap_frame* regs)
;     Build an iretq frame from the saved user state and drop to
;     ring 3. Args in RDI per SysV.
;
;   user_registers()
;     Load the user data selector (USER_DATA_SEGMENT = 0x33)
;     into ds/es/fs/gs. CS and SS come from the iretq frame.
;
; SamOS divergence: the saved state is a struct
; trap_frame (idt/idt.h) and all 15 general registers come back,
; not just the 7 the book's struct registers had.

[BITS 64]
section .asm

global task_return
global user_registers

; struct trap_frame offsets (idt/idt.h)
%define TF_R15     0
%define TF_R14     8
%define TF_R13     16
%define TF_R12     24
%define TF_R11     32
%define TF_R10     40
%define TF_R9      48
%define TF_R8      56
%define TF_RBP     64
%define TF_RDI     72
%define TF_RSI     80
%define TF_RDX     88
%define TF_RCX     96
%define TF_RBX     104
%define TF_RAX     112
%define TF_RIP     136
%define TF_RFLAGS  152
%define TF_RSP     160

; void task_return(struct trap_frame* regs)
task_return:
    cli                                ; until iretq reloads RFLAGS
    push qword 0x33                    ; SS = USER_DATA_SEGMENT
    push qword [rdi + TF_RSP]
    mov  rax, [rdi + TF_RFLAGS]
    or   rax, 0x200                    ; IF=1 in user mode
    push rax
    push qword 0x2B                    ; CS = USER_CODE_SEGMENT
    push qword [rdi + TF_RIP]
    mov r15, [rdi + TF_R15]
    mov r14, [rdi + TF_R14]
    mov r13, [rdi + TF_R13]
    mov r12, [rdi + TF_R12]
    mov r11, [rdi + TF_R11]
    mov r10, [rdi + TF_R10]
    mov r9,  [rdi + TF_R9]
    mov r8,  [rdi + TF_R8]
    mov rbp, [rdi + TF_RBP]
    mov rsi, [rdi + TF_RSI]
    mov rdx, [rdi + TF_RDX]
    mov rcx, [rdi + TF_RCX]
    mov rbx, [rdi + TF_RBX]
    mov rax, [rdi + TF_RAX]
    mov rdi, [rdi + TF_RDI]
    swapgs                             ; User GS base for ring 3
    iretq

; void user_registers()
; gs is not loaded; a selector load clears the GS base,
; which holds this CPU's struct cpu while in the kernel.
user_registers:
    mov ax, 0x33                       ; USER_DATA_SEGMENT
    mov ds, ax
    mov es, ax
    mov fs, ax
    ret
