[BITS 32]

section .asm

global _start

_start:
    ; Ch 116: block until a key is pressed, then print.
    call getkey

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

getkey:
    mov eax, 2             ; SYSTEM_COMMAND2_GETKEY
    int 0x80
    cmp eax, 0x00
    je  getkey
    ret

section .data

message: db 'I can talk with the kernel!', 0
