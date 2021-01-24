; src/boot.asm - read sector 2 via BIOS INT 0x13 then print its contents.
;
; Boot sector itself (sector 1) is loaded by BIOS at 0x7C00. We then:
;   1. Set segment registers (DS, ES = 0x07C0; SS = 0).
;   2. Issue INT 0x13 to read sector 2 into ES:0x0200 = physical 0x7E00.
;   3. Point SI at offset 0x0200 (DS-relative -> physical 0x7E00).
;   4. Call print which walks the NUL-terminated string at SI.

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

    ; Read sector 2 from the first HDD into ES:0x0200 = 0x7E00.
    mov bx, 0x0200
    mov ah, 0x02            ; read sectors
    mov al, 0x01            ; 1 sector
    mov ch, 0x00            ; cylinder 0
    mov cl, 0x02            ; sector 2 (1-based)
    mov dh, 0x00            ; head 0
    mov dl, 0x80            ; drive 0x80 (first HDD)
    int 0x13

    mov si, 0x0200
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

times 510-($-$$) db 0
dw 0xAA55
