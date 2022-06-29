; src/boot.asm - register our own ISR at vector 0x80.
;
; Sets up segment registers (same as Ch 8), then writes a new IVT entry at
; offset 0x80 * 4 = 0x200, pointing to our `print` routine. After that we
; load SI with the address of `message` and `int 0x80` triggers the handler.

ORG 0
BITS 16

start:
    cli
    mov ax, 0x07C0
    mov ds, ax
    mov es, ax
    mov ax, 0x0000
    mov ss, ax
    sti

    ; Install vector 0x80 in the IVT. IVT entry layout: offset (2B) then
    ; segment (2B). SS = 0, so [ss:0x200] points at physical 0x200.
    mov ax, 0x07C0
    mov word [ss:0x202], ax     ; segment half

    lea ax, [print]
    mov word [ss:0x200], ax     ; offset half

    lea si, [message]
    int 0x80
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
    iret                        ; interrupt-return (NOT plain ret)

print_char:
    mov ah, 0x0E
    int 0x10
    ret

message:     db 'Hello, World!', 0
isr_message: db 'Interrupt Happened!', 0

times 510-($-$$) db 0
dw 0xAA55
