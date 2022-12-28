[BITS 32]

section .asm

global print:function
global getkey:function

; void print(const char* msg)
print:
    push ebp
    mov  ebp, esp
    push dword [ebp+8]
    mov  eax, 1            ; SYSTEM_COMMAND1_PRINT
    int  0x80
    add  esp, 4
    pop  ebp
    ret

; int getkey()
getkey:
    push ebp
    mov  ebp, esp
    mov  eax, 2            ; SYSTEM_COMMAND2_GETKEY
    int  0x80
    pop  ebp
    ret
