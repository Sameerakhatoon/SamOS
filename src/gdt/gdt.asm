section .text

global gdt_load

; void gdt_load(struct gdt* gdt, int size)
; Stash the size + start address into the GDTR descriptor and `lgdt` it.
gdt_load:
    mov eax, [esp+4]
    mov [gdt_descriptor + 2], eax
    mov ax, [esp+8]
    mov [gdt_descriptor], ax
    lgdt [gdt_descriptor]
    ret

section .data
gdt_descriptor:
    dw 0x00    ; size (low 2 bytes)
    dd 0x00    ; GDT start address (4 bytes)
