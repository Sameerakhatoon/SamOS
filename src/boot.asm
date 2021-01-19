; src/boot.asm - refined Hello World bootloader.
;
; Improvement over the previous version: instead of trusting BIOS to set the
; segment registers, we set them up ourselves. Origin is now 0 (offsets are
; relative to the segment, not to physical 0x7c00). We point DS and ES at
; 0x07C0 so labels resolve to physical 0x7C00 + offset, and SS at 0 so the
; stack is well-defined.

ORG 0
BITS 16

start:
    cli                 ; disable interrupts during segment register setup

    mov ax, 0x07C0
    mov ds, ax          ; data segment at 0x07C0 (so [label] = 0x7C00+label)
    mov es, ax          ; extra segment same as data segment

    mov ax, 0x0000
    mov ss, ax          ; stack segment at 0

    sti                 ; re-enable interrupts

    mov si, message
    call print
    jmp $

print:
    mov bx, 0
.loop:
    lodsb
    cmp al, 0
    je .done
    call print_char
    jmp .loop
.done:
    ret

print_char:
    mov ah, 0x0E
    int 0x10
    ret

message: db 'Hello, World!', 0

times 510-($-$$) db 0
dw 0xAA55
