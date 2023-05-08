; Lecture 41 - task.asm 64-bit rewrite.
;
; Three asm helpers used by task.c:
;
;   task_return(struct registers* regs)
;     Build an iretq frame from the saved register file and
;     jump to user mode. Args in RDI per SysV.
;
;   restore_general_purpose_registers(struct registers* regs)
;     Reload rsi/rbp/rbx/rdx/rcx/rax/rdi from the file.
;     Subset of pushad_macro's restore - we leave rdi for last
;     because we are still using it as the file pointer.
;
;   user_registers()
;     Load the user data selector (USER_DATA_SEGMENT = 0x33)
;     into ds/es/fs/gs. CS gets set by iretq; SS is loaded
;     from the iretq frame.

[BITS 64]
section .asm

global restore_general_purpose_registers
global task_return
global user_registers

; The struct registers layout (see task.h):
;   off  field
;     0  rdi
;     8  rsi
;    16  rbp
;    24  rbx
;    32  rdx
;    40  rcx
;    48  rax
;    56  ip
;    64  cs
;    72  flags
;    80  rsp
;    88  ss

; void task_return(struct registers* regs)
;
; Manually constructs the iretq frame in reverse-push order:
;   SS, RSP, RFLAGS, CS, RIP. The push order below pushes
;   bottom-of-frame first.
task_return:
    push qword [rdi + 88]          ; SS
    push qword [rdi + 80]          ; RSP
    mov   rax, [rdi + 72]          ; RFLAGS
    or    rax, 0x200               ; force IF=1 so the user can be preempted
    push  rax
    push  qword 0x2B               ; CS = USER_CODE_SEGMENT (0x28|3)
    push  qword [rdi + 56]         ; RIP
    call  restore_general_purpose_registers
    iretq

; void restore_general_purpose_registers(struct registers* regs)
;
; Reloads everything except rdi from the file, then reloads
; rdi last (we are using it as the file base until that point).
restore_general_purpose_registers:
    mov rsi, [rdi + 8]
    mov rbp, [rdi + 16]
    mov rbx, [rdi + 24]
    mov rdx, [rdi + 32]
    mov rcx, [rdi + 40]
    mov rax, [rdi + 48]
    mov rdi, [rdi + 0]
    ret

; void user_registers()
;
; ds/es/fs/gs must be a DATA selector with DPL=3, so we use
; USER_DATA_SEGMENT (0x33). SS gets set by the iretq frame
; from task_return; CS is set by iretq itself.
user_registers:
    mov ax, 0x33                   ; USER_DATA_SEGMENT
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret
