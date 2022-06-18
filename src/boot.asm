; src/boot.asm - Hello World bootloader.
;
; BIOS loads the first 512 bytes of the boot disk at physical 0x7c00 and
; jumps there. We print "Hello, World!" using BIOS interrupt 0x10 then
; spin forever.

ORG 0x7c00          ; tell NASM where in memory our code will run
BITS 16             ; we're writing 16-bit code

start:
    mov si, message ; address of `message` into SI
    call print
    jmp $           ; spin here forever

print:
    mov bx, 0       ; (unused; reserved by book listing)
.loop:
    lodsb           ; load byte at DS:SI into AL, increment SI
    cmp al, 0
    je .done        ; end of string → return
    call print_char
    jmp .loop
.done:
    ret

print_char:
    mov ah, 0eh     ; BIOS teletype output service
    int 0x10        ; print AL to screen
    ret

message: db 'Hello, World!', 0

times 510-($-$$) db 0   ; pad to 510 bytes
dw 0xAA55               ; boot signature in the last two bytes
