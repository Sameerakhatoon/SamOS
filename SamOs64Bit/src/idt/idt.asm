; Lecture 36 - idt.asm fully migrated to 64-bit.
;
; All the 32-bit constructs got widened:
;   - [BITS 64]
;   - idt_load: arg in RDI per AMD64 SysV (not [ebp+8])
;   - iret -> iretq everywhere
;   - struct interrupt_frame fields are uint64_t (CPU pushes 8-byte
;     ip/cs/flags/sp/ss in long mode interrupts)
;   - the interrupt macro passes the interrupt number + frame
;     pointer via rdi/rsi (SysV), not via stack pushes
;   - tmp_res slot widened dd -> dq
;   - interrupt_pointer_table entries widened dd -> dq (function
;     pointers are 8 bytes now)
;
; pushad_macro / popad_macro from L35 stay - they emulate the
; deleted 1-byte pushad / popad at 64-bit width.

[BITS 64]
section .asm

extern int21h_handler                ; reserved for the keyboard IRQ
extern no_interrupt_handler
extern isr80h_handler
extern interrupt_handler

global idt_load
global no_interrupt
global enable_interrupts
global disable_interrupts
global isr80h_wrapper
global interrupt_pointer_table

; Lecture 35 - pushad_macro / popad_macro emulating 32-bit pushad
; / popad at 64-bit width. See L34-L35 doc for rationale.
temp_rsp_storage: dq 0x00

%macro pushad_macro 0
    mov qword [temp_rsp_storage], rsp
    push rax
    push rcx
    push rdx
    push rbx
    push qword [temp_rsp_storage]
    push rbp
    push rsi
    push rdi
%endmacro

%macro popad_macro 0
    pop rdi
    pop rsi
    pop rbp
    pop qword [temp_rsp_storage]
    pop rbx
    pop rdx
    pop rcx
    pop rax
    mov rsp, [temp_rsp_storage]
%endmacro

enable_interrupts:
    sti
    ret

disable_interrupts:
    cli
    ret

; void idt_load(struct idt_pointer* ptr)
; AMD64 SysV: first arg in RDI.
idt_load:
    mov rbx, rdi
    lidt [rbx]
    ret

no_interrupt:
    pushad_macro
    call no_interrupt_handler
    popad_macro
    iretq

; Per-vector stub. The CPU has already pushed:
;   uint64_t ip
;   uint64_t cs
;   uint64_t flags
;   uint64_t sp
;   uint64_t ss
; (long-mode interrupt frame; all fields 8 bytes now.)
%macro interrupt 1
    global int%1
    int%1:
        pushad_macro
        ; AMD64 SysV: pass interrupt number in RDI, frame ptr in RSI.
        mov rdi, %1
        mov rsi, rsp
        call interrupt_handler
        popad_macro
        iretq
%endmacro

%assign i 0
%rep 512
    interrupt i
%assign i i+1
%endrep

; isr80h_wrapper - the int 0x80 entry point. User code put the
; command number in rax (it was pre-trap). The CPU has pushed
; ip/cs/flags/sp/ss already.
isr80h_wrapper:
    pushad_macro

    ; Second arg = interrupt frame pointer
    mov rsi, rsp
    ; First arg = command number (user's pre-trap rax)
    mov rdi, rax

    call isr80h_handler

    ; Stash handler's return value before popad_macro clobbers rax
    mov qword [tmp_res], rax

    popad_macro
    mov rax, [tmp_res]
    iretq

section .data
tmp_res: dq 0

; 64-bit pointer table: 8 bytes per function pointer.
%macro interrupt_array_entry 1
    dq int%1
%endmacro

interrupt_pointer_table:
%assign i 0
%rep 512
    interrupt_array_entry i
%assign i i+1
%endrep
