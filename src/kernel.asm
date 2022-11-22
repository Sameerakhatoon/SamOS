[BITS 32]
global _start
global kernel_registers
extern kernel_main

CODE_SEG equ 0x08
DATA_SEG equ 0x10

_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ebp, 0x00200000
    mov esp, ebp

    ; Enable the A20 line.
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Remap the master PIC.
    mov al, 00010001b           ; ICW1: init + ICW4 needed
    out 0x20, al                ; master command port

    mov al, 0x20                ; ICW2: master starts at vector 0x20
    out 0x21, al

    mov al, 00000001b           ; ICW4: 8086 mode
    out 0x21, al

    ; Interrupts stay off until kernel_main's enable_interrupts() runs after
    ; the IDT has been loaded. The old `sti` here would let IRQs land on an
    ; uninitialized IDT and triple-fault.

    call kernel_main

    jmp $

; Ch 102: kernel_registers - reload DS/ES/FS/GS with the kernel data
; selector. Mirror of user_registers from Ch 95, called at the top of
; every syscall handler (via kernel_page).
kernel_registers:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov gs, ax
    mov fs, ax
    ret

times 512-($-$$) db 0
