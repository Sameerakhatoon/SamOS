[BITS 32]

section .asm

global print:function
global getkey:function
global samos_malloc:function

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

; void* samos_malloc(size_t size)
samos_malloc:
    push ebp
    mov  ebp, esp
    mov  eax, 4            ; SYSTEM_COMMAND4_MALLOC
    push dword [ebp+8]
    int  0x80
    add  esp, 4
    pop  ebp
    ret
