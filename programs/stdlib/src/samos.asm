[BITS 32]

section .asm

global print:function

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
