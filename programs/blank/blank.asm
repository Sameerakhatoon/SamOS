[BITS 32]

section .asm

global _start

_start:
    ; Fire syscall command 0 (SYSTEM_COMMAND0_SUM). The kernel stub
    ; returns 0; we then spin so the CPU stays in ring 3 with EAX = 0.
    mov eax, 0
    int 0x80

label:
    jmp label
