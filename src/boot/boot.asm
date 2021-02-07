; src/boot/boot.asm - switch CPU into 32-bit Protected Mode, then enable A20.
;
; Real-mode setup, then load a GDT with null/code/data entries, flip CR0.PE,
; far-jump to 32-bit code, reload segment registers, set up the kernel stack,
; enable the A20 line, spin.

ORG 0x7C00
BITS 16

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

_start:
    jmp short start
    nop

    ; 33 bytes reserved so BIOS does not clobber our code if it tries to
    ; treat the boot sector as a BPB. We are not filling in a real BPB
    ; right now.
    times 33 db 0

start:
    jmp 0:step2

step2:
    cli
    mov ax, 0x00
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

.load_protected:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp CODE_SEG:load32

; ---- GDT --------------------------------------------------------------------
gdt_start:
gdt_null:
    dd 0x0
    dd 0x0

; offset 0x08: kernel code segment, base 0, limit 0xFFFFF (with 4 KiB granularity = 4 GiB).
gdt_code:
    dw 0xFFFF              ; segment limit, bits 0..15
    dw 0                   ; base bits 0..15
    db 0                   ; base bits 16..23
    db 0x9A                ; access byte: present, ring 0, code, executable, readable
    db 11001111b           ; flags (4-bit) + limit bits 16..19: G=1, D=1, limit=0xF
    db 0                   ; base bits 24..31

; offset 0x10: kernel data segment, same shape as code but access = 0x92 (writable data).
gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 0x92
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---- 32-bit code ------------------------------------------------------------
[BITS 32]
load32:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ebp, 0x00200000
    mov esp, ebp

    ; Enable the A20 line so addresses above 1 MiB no longer wrap.
    in al, 0x92
    or al, 2
    out 0x92, al

    jmp $

times 510-($-$$) db 0
dw 0xAA55
