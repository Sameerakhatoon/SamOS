[BITS 32]

section .asm

global print:function
global samos_getkey:function
global samos_malloc:function
global samos_free:function
global samos_putchar:function
global samos_process_load_start:function
global samos_process_get_arguments:function

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

; int samos_getkey()
samos_getkey:
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

; void samos_free(void* ptr)
samos_free:
    push ebp
    mov  ebp, esp
    mov  eax, 5            ; SYSTEM_COMMAND5_FREE
    push dword [ebp+8]
    int  0x80
    add  esp, 4
    pop  ebp
    ret

; void samos_putchar(char c)
samos_putchar:
    push ebp
    mov  ebp, esp
    mov  eax, 3            ; SYSTEM_COMMAND3_PUTCHAR
    push dword [ebp+8]
    int  0x80
    add  esp, 4
    pop  ebp
    ret

; void samos_process_load_start(const char* filename)
samos_process_load_start:
    push ebp
    mov  ebp, esp
    mov  eax, 6            ; SYSTEM_COMMAND6_PROCESS_LOAD_START
    push dword [ebp+8]
    int  0x80
    add  esp, 4
    pop  ebp
    ret

; void samos_process_get_arguments(struct process_arguments* arguments)
samos_process_get_arguments:
    push ebp
    mov  ebp, esp
    mov  eax, 8            ; SYSTEM_COMMAND8_GET_PROGRAM_ARGUMENTS
    push dword [ebp+8]
    int  0x80
    add  esp, 4
    pop  ebp
    ret
