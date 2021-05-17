[BITS 32]

section .asm

global _start

_start:
    push 20
    push 30
    mov  eax, 0           ; SYSTEM_COMMAND0_SUM
    int  0x80             ; -> EAX = 50
    add  esp, 8           ; clean up our two pushed args

label:
    jmp label
