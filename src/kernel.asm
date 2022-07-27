; src/kernel.asm - first sectors of the kernel image, loaded at 0x100000.
;
; Just reloads segment selectors and spins for now. Later chapters add
; a C entry point and call it from here.

[BITS 32]
global _start

CODE_SEG equ 0x08
DATA_SEG equ 0x10

_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ebp, 0x00200000
    mov esp, ebp
    jmp $
