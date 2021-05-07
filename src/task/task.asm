[BITS 32]
section .asm

global user_registers
global restore_general_purpose_registers
global task_return

; void user_registers()
; Reload DS/ES/FS/GS with the user data selector (0x23 = 0x20 | 3).
user_registers:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret

; void restore_general_purpose_registers(struct registers* regs)
;
; struct registers layout (sizes are 4 bytes each):
;   0   edi
;   4   esi
;   8   ebp
;  12   ebx
;  16   edx
;  20   ecx
;  24   eax
;  28   ip
;  32   cs
;  36   flags
;  40   esp
;  44   ss
;
; Note: Ch 97 fixes the final pop here - the book initially had
; `pop esp` which would clobber the stack pointer with the saved
; base pointer. We pop into ebp.
restore_general_purpose_registers:
    push ebp
    mov ebp, esp
    mov ebx, [ebp+8]
    mov edi, [ebx]
    mov esi, [ebx+4]
    mov ebp, [ebx+8]
    mov edx, [ebx+16]
    mov ecx, [ebx+20]
    mov eax, [ebx+24]
    mov ebx, [ebx+12]
    pop ebp
    ret

; void task_return(struct registers* regs)
; Build a synthetic interrupt frame on the stack and iret into user
; mode. The frame must match what the CPU pushes on a privilege-level
; trap: SS, ESP, EFLAGS, CS, EIP (in that push order).
;
; Note: Ch 97 fixes the final argument push here - the book initially
; pushed `[ebx+4]` (which would be the esi field, not the regs pointer
; itself). The correct value is `[ebp+4]`, the regs argument.
task_return:
    mov ebp, esp
    mov ebx, [ebp+4]            ; struct registers*

    push dword [ebx+44]         ; SS
    push dword [ebx+40]         ; ESP
    pushf                       ; EFLAGS
    pop eax
    or eax, 0x200               ; set IF so interrupts return on in user mode
    push eax
    push dword [ebx+32]         ; CS
    push dword [ebx+28]         ; EIP

    ; Load user data selectors into segment registers.
    mov ax, [ebx+44]
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword [ebp+4]          ; pass struct registers* to restore_gpr
    call restore_general_purpose_registers
    add esp, 4

    iretd
