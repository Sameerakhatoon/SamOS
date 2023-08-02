; Lecture 74 - UEFI-compatible kernel.asm.
;
; The 32-bit -> 64-bit transition prologue is GONE. UEFI loads
; kernel.bin into long mode already; the previous code (PAE/CR3/
; EFER.LME/CR0.PG dance) is exactly what UEFI did on our behalf
; before ExitBootServices.
;
; What stays: kernel_registers helper, long_mode_entry main body
; (now does its OWN CR3+lgdt because UEFI hands us a UEFI-flavoured
; GDT we want to replace), PIC remap, jmp kernel_main, the GDT
; itself, and the static PML4/PDPT/PD identity-mapping skeleton.
;
; SamOs DEVIATION: this breaks the BIOS boot path. Our boot.asm
; transitions to 32-bit protected mode then jumps to 0x100000
; expecting 32-bit code. After L74, byte 0 at 0x100000 is `cli`
; (0xFA, same in both modes) but byte 1 begins a `jmp` to
; long_mode_entry whose body is 64-bit-encoded instructions
; (REX-prefixed mov rax, ...) which decode as garbage in 32-bit
; mode. The kernel will not run from the BIOS path until either
; boot.asm grows the long-mode transition itself or a separate
; BIOS entry point gets added back. SamOs accepts this divergence
; per the user's directive to follow upstream verbatim.

[BITS 64]

global _start
global kernel_registers
global div_test
global gdt
global default_graphics_info     ; L87 - root surface struct in bss
extern kernel_main

CODE_SEG           equ 0x08
DATA_SEG           equ 0x10
LONG_MODE_CODE_SEG equ 0x18
LONG_MODE_DATA_SEG equ 0x20

_start:
    cli
    jmp long_mode_entry

; Lecture 15: kernel_registers() - reload ds/es/fs/gs with the
; 64-bit data selector. See L34-L35 doc for rationale.
kernel_registers:
    mov ax, LONG_MODE_DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret

long_mode_entry:
    ; Lecture 74 - install our own page tables. UEFI was using its
    ; own page tables that mapped UEFI structures we don't care
    ; about; ours covers the first GiB identity-mapped via
    ; 2-MiB PS=1 leaves.
    mov rax, PML4_Table
    mov cr3, rax

    ; Lecture 74 - install our own GDT. UEFI's GDT had different
    ; selectors than what kernel.c expects (slot 3 = our long-mode
    ; code seg).
    lgdt [gdt_descriptor]

    mov ax, LONG_MODE_DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, 0x00200000
    mov rbp, rsp

    ; Lecture 74 - far retfq to reload CS from our new GDT.
    ; Just installing a new GDT doesn't change the live CS; UEFI's
    ; original CS is still cached in the shadow register. A far
    ; return is the simplest way to atomically swap CS to slot 3
    ; (our long-mode code seg). Push the target SS slot
    ; (LONG_MODE_CODE_SEG = 0x18) and target RIP, then retfq pops
    ; both into CS:RIP.
    push QWORD 0x18                         ; target CS = slot 3
    push QWORD long_mode_new_gdt_complete
    retfq

long_mode_new_gdt_complete:

    ; Lecture 87 - stash the framebuffer parameters UEFI passed in
    ; rdi/edx/ecx/esi into the default_graphics_info struct. Done
    ; HERE because long-mode jumps clobbered nothing and we are
    ; still before kernel_main's C prologue. After this we never
    ; need rdi/etc. again until kernel_main runs.
    ;   rdi = framebuffer base                  -> +0  qword
    ;   edx = horizontal resolution             -> +8  dword
    ;   ecx = vertical resolution               -> +12 dword
    ;   esi = pixels per scan line              -> +16 dword
    mov [default_graphics_info + 0],  rdi
    mov [default_graphics_info + 8],  edx
    mov [default_graphics_info + 12], ecx
    mov [default_graphics_info + 16], esi

    ; Lecture 61 - remap the 8259 PIC.
    ; By default IRQ0..7 land at vectors 0x08..0x0F which collide
    ; with CPU exceptions (#DF in particular). Move them to
    ; 0x20..0x27 (master) and 0x28..0x2F (slave).

    ; Master PIC ICW1..ICW4
    mov al, 0x11
    out 0x20, al
    mov al, 0x20
    out 0x21, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x01
    out 0x21, al

    ; Slave PIC ICW1..ICW4
    mov al, 0x11
    out 0xA0, al
    mov al, 0x28
    out 0xA1, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0xA1, al

    ; IRQ masks. Master 0xFB = all-except-cascade masked; IRQ0
    ; (timer) STAYS MASKED so we reach user mode quietly. Slave
    ; 0xFF = all masked.
    mov al, 0xFB
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al

    ; EOI any in-flight PIC interrupts.
    mov al, 0x20
    out 0x20, al
    out 0xA0, al

    jmp kernel_main
    jmp $

; Lecture 38 - deliberate #DE for IDT testing.
div_test:
    mov rax, 0
    idiv rax
    ret

; ------------------------------------------------------------------
; Global descriptor table (GDT)
; ------------------------------------------------------------------
align 8
gdt:
    dq 0x0000000000000000           ; Null descriptor

    ; 32-bit code segment (selector 0x08) - legacy slot kept for
    ; backwards compatibility.
    dw 0xffff
    dw 0
    db 0
    db 0x9a
    db 11001111b
    db 0

    ; 32-bit data segment (selector 0x10)
    dw 0xffff
    dw 0
    db 0
    db 0x92
    db 11001111b
    db 0

    ; 64-bit code segment (selector 0x18) - L=1
    dw 0x0000
    dw 0x0000
    db 0x00
    db 0x9A
    db 0x20
    db 0x00

    ; 64-bit data segment (selector 0x20)
    dw 0x0000
    dw 0x0000
    db 0x00
    db 0x92
    db 0x00
    db 0x00

    ; 64-bit user code segment (selector 0x28)
    dw 0x0000
    dw 0x0000
    db 0x00
    db 0xFA
    db 0x20
    db 0x00

    ; 64-bit user data segment (selector 0x30)
    dw 0x0000
    dw 0x0000
    db 0x00
    db 0xF2
    db 0x00
    db 0x00

    ; Lecture 50 - two reserved slots for the 64-bit TSS
    ; descriptor (gdt[7] and gdt[8]).
    dq 0x0000000000000000
    dq 0x0000000000000000
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt - 1            ; Limit
    ; Lecture 74 - base widened to 8 bytes. In long mode the GDTR
    ; layout is { uint16 limit; uint64 base }. Using `dd` (4 bytes)
    ; left half of the base zero on real hardware where the GDT
    ; lives above 4 GiB.
    dq gdt

; ------------------------------------------------------------------
; 4-level page-table skeleton: PML4 -> PDPT -> PD with 2-MiB pages.
; Identity-maps the first 1 GiB via 2-MiB PS=1 leaves.
; ------------------------------------------------------------------
align 4096
PML4_Table:
    dq PDPT_TABLE + 0x03            ; Present + RW
    times 511 dq 0

align 4096
PDPT_TABLE:
    dq PD_Table + 0x03
    times 511 dq 0

align 4096
PD_Table:
    %assign addr 0x00000000
    %rep 512
        dq addr + 0x83              ; Present | RW | PS=1
        %assign addr addr + 0x200000
    %endrep


; Lecture 87 - root graphics_info instance. Stored here so the
; long-mode entry can fill the framebuffer fields straight from
; the UEFI handoff registers before any C code runs. The layout
; below MUST mirror struct graphics_info in src/graphics/graphics.h.
;
; offset 0   qword framebuffer pointer        (rdi from UEFI)
; offset 8   dword horizontal_resolution      (edx from UEFI)
; offset 12  dword vertical_resolution        (ecx from UEFI)
; offset 16  dword pixels_per_scanline        (esi from UEFI)
; offset 20  dword <padding to align pixels>
; offset 24  qword pixels back buffer pointer
; offset 32  dword width
; offset 36  dword height
; offset 40  dword starting_x
; offset 44  dword starting_y
; offset 48  dword relative_x
; offset 52  dword relative_y
; offset 56  qword parent
; offset 64  qword children
; offset 72  dword flags
; offset 76  dword z_index
; offset 80  dword ignore_color
; offset 84  dword transparency_key
; offset 88  qword mouse_click handler
; offset 96  qword mouse_move  handler
align 8
default_graphics_info:
    dq 0    ; +0   framebuffer
    dd 0    ; +8   horizontal_resolution
    dd 0    ; +12  vertical_resolution
    dd 0    ; +16  pixels_per_scanline
    dd 0    ; +20  padding
    dq 0    ; +24  pixels
    dd 0    ; +32  width
    dd 0    ; +36  height
    dd 0    ; +40  starting_x
    dd 0    ; +44  starting_y
    dd 0    ; +48  relative_x
    dd 0    ; +52  relative_y
    dq 0    ; +56  parent
    dq 0    ; +64  children
    dd 0    ; +72  flags
    dd 0    ; +76  z_index
    dd 0    ; +80  ignore_color
    dd 0    ; +84  transparency_key
    dq 0    ; +88  mouse_click
    dq 0    ; +96  mouse_move
