section .asm

global tss_load

; void tss_load(int tss_segment)
; Load the Task Register with the GDT offset of the TSS descriptor.
tss_load:
    push ebp
    mov ebp, esp

    mov ax, [ebp+8]    ; TSS segment selector
    ltr ax

    pop ebp
    ret
