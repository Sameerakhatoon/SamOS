; Lecture 36 - idt.asm, 64-bit.
;
; SamOS divergence from the book: every vector
; builds one struct trap_frame (idt.h) on the stack, holding all 15
; general registers, the vector number and an error code. The book's
; pushad_macro saved only 7 registers, kept rsp in a global scratch
; slot and never popped the CPU's error code, so r8-r15 leaked
; between user and kernel and #GP/#PF returned to a frame 8 bytes off.
;
; Exceptions that push an error code (8, 10-14, 17, 21, 29, 30) get a
; stub without the dummy push, so every frame has the same layout.

[BITS 64]
section .asm

extern interrupt_handler
extern isr80h_handler

global idt_load
global enable_interrupts
global disable_interrupts
global isr80h_wrapper
global interrupt_pointer_table

enable_interrupts:
    sti
    ret

disable_interrupts:
    cli
    ret

; void idt_load(struct idtr_desc* ptr)
idt_load:
    lidt [rdi]
    ret

; Save the general registers below the vector/error pair, call
; %1(struct trap_frame*), restore and iretq.
%macro TRAP_BODY 1
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    ; Rbx is callee-saved and already in the frame, so it holds
    ; the frame pointer across the call while rsp is aligned for C.
    mov rbx, rsp
    and rsp, -16
    call %1
    mov rsp, rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                        ; vector + error code
    iretq
%endmacro

trap_common:
    TRAP_BODY interrupt_handler

%macro TRAP_STUB 1
int%1:
%if %1 = 8 || %1 = 10 || %1 = 11 || %1 = 12 || %1 = 13 || %1 = 14 || %1 = 17 || %1 = 21 || %1 = 29 || %1 = 30
%else
    push qword 0
%endif
    push qword %1
    jmp trap_common
%endmacro

%assign i 0
%rep 256
    TRAP_STUB i
%assign i i+1
%endrep

; int 0x80. The command number is in rax; isr80h_handler writes the
; result into frame->rax, which the pops below hand back to the user.
isr80h_wrapper:
    push qword 0
    push qword 0x80
    TRAP_BODY isr80h_handler

section .data
align 8

%macro TRAP_ENTRY 1
    dq int%1
%endmacro

interrupt_pointer_table:
%assign i 0
%rep 256
    TRAP_ENTRY i
%assign i i+1
%endrep
