; Lecture 33 - 64-bit IO primitives.
;
; The 32-bit versions used the cdecl stack-args convention
; (push ebp; mov ebp, esp; mov eax, [ebp+8]; ...). In long
; mode we follow the AMD64 System V ABI: first integer arg in
; RDI, second in RSI. No prologue / epilogue needed.
;
; Reads write the byte/word/dword result into RAX (return
; value); we zero RAX first on the input side so the high bits
; are clean.

[BITS 64]
section .asm

global insb
global insw
global insdw
global outb
global outw
global outdw

; uint8_t insb(uint16_t port)   - port in DI, result in AL
insb:
    xor rax, rax
    mov dx, di
    in  al, dx
    ret

; uint16_t insw(uint16_t port)  - port in DI, result in AX
insw:
    xor rax, rax
    mov dx, di
    in  ax, dx
    ret

; uint32_t insdw(uint16_t port) - port in DI, result in EAX
insdw:
    xor rax, rax
    mov dx, di
    in  eax, dx
    ret

; void outb(uint16_t port, uint8_t val)  - port DI, val SI
outb:
    mov ax, si
    mov dx, di
    out dx, al
    ret

; void outw(uint16_t port, uint16_t val) - port DI, val SI
outw:
    mov rax, rsi
    mov rdx, rdi
    out dx, ax
    ret

; void outdw(uint16_t port, uint32_t val) - port DI, val SI
outdw:
    mov rax, rsi
    mov rdx, rdi
    out dx, eax
    ret
