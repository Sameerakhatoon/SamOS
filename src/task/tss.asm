; Lecture 50 - 64-bit tss_load.
;
; void tss_load(int tss_segment)
;
; AMD64 SysV: the int arg arrives in EDI (low 32 bits of RDI);
; we read the low 16 bits via DI directly. ltr loads the Task
; Register from a 16-bit selector.

[BITS 64]
section .asm

global tss_load

tss_load:
    xor rax, rax
    mov ax, di
    ltr ax
    ret
