[BITS 32]

section .asm

global _start

_start:
    ; Ch 108: print a message via syscall command 1.
    push message
    mov  eax, 1            ; SYSTEM_COMMAND1_PRINT
    int  0x80
    add  esp, 4

    ; Ch 107 sum: still here so test 38 can assert EAX=50 after it.
    push 20
    push 30
    mov  eax, 0            ; SYSTEM_COMMAND0_SUM
    int  0x80              ; -> EAX = 50
    add  esp, 8

label:
    jmp label

section .data

message: db 'I can talk with the kernel!', 0
