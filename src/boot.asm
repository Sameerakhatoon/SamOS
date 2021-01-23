; src/boot.asm - register an exception handler for divide-by-zero (#DE).
;
; Same idea as the previous chapter, but for vector 0 instead of 0x80.
; Then we deliberately `div ax` with ax = 0 to trigger #DE and watch the
; handler print "Divide by zero error!".

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

    ; Install vector 0 (divide-by-zero) in the IVT.
    mov ax, 0x07C0
    mov word [ss:0x02], ax              ; segment half
    mov ax, div_zero_handler
    mov word [ss:0x00], ax              ; offset half

    ; Deliberately divide by zero.
    mov ax, 0
    div ax

    jmp $

div_zero_handler:
    mov si, div_zero_message
    call print
    iret

print:
    mov bx, 0
.loop:
    lodsb
    cmp al, 0
    je .done
    call print_char
    jmp .loop
.done:
    ret                                 ; plain ret (called from div_zero_handler, not from int)

print_char:
    mov ah, 0x0E
    int 0x10
    ret

div_zero_message: db 'Divide by zero error!', 0

times 510-($-$$) db 0
dw 0xAA55
