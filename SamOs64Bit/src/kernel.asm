; Lecture 74 - UEFI-compatible kernel.asm.
;
; UEFI enters the kernel in long mode with its own GDT, IDT and
; page tables. This file installs ours, remaps the PIC and jumps to
; kernel_main.
;
; SamOS divergences from the book:
;   * rdi holds ONE pointer, to struct samos_boot_info, instead of
;     four framebuffer values in rdi/rsi/rdx/rcx.
;   * The stack is set first, to the top of the loader-reserved
;     kernel window, and .bss is zeroed before anything uses it.
;   * A minimal exception IDT is installed before the GDT swap, so a
;     fault during early boot prints a register dump on COM1
;     (early_fault.c) instead of triple-faulting into a reset.
;
; The kernel runs in the higher half. The loader
; still jumps to physical 0x100000, where the .boot stub below runs
; on the firmware's identity map. It loads boot page tables that map
; the first GiB three times (identity, the direct map at
; 0xFFFF800000000000 and the kernel window at 0xFFFFFFFF80000000)
; and jumps to higher_half_entry. kernel_main replaces the tables
; with the real kernel address space (paging_kernel_desc_new), which
; has no identity map at all.

[BITS 64]

%define SAMOS_BOOT_STACK_TOP 0x200000           ; = SAMOS_KERNEL_WINDOW_END in config.h
%define KERNEL_VMA           0xFFFFFFFF80000000 ; = SAMOS_KERNEL_VMA
%define DIRECT_MAP_BASE      0xFFFF800000000000 ; = SAMOS_DIRECT_MAP_BASE

global _start
global kernel_registers
global div_test
global gdt
global default_graphics_info     ; L87 - root surface struct
global samos_boot_info_ptr
extern kernel_main
extern early_fault
extern _kernel_bss_start
extern _kernel_bss_end

CODE_SEG           equ 0x08
DATA_SEG           equ 0x10
LONG_MODE_CODE_SEG equ 0x18
LONG_MODE_DATA_SEG equ 0x20

; ------------------------------------------------------------------
; Identity-mapped entry stub, linked at its load address.
; ------------------------------------------------------------------
section .boot

_start:
    cli
    cld
    mov r12, rdi                       ; boot_info (physical)
    mov rsp, SAMOS_BOOT_STACK_TOP      ; physical, identity-mapped by the firmware
    mov rax, boot_pml4
    mov cr3, rax
    mov rax, higher_half_entry
    jmp rax

; Boot page tables. One PD maps physical [0, 1 GiB) with 2 MiB
; pages; it is reached from PML4 slot 0 (identity, for the jump
; above), slot 256 (direct map) and slot 511 / PDPT slot 510
; (kernel window). paging_kernel_desc_new replaces all of it.
align 4096
boot_pml4:
    dq boot_pdpt_low + 0x03            ; 0x0000000000000000
    times 255 dq 0
    dq boot_pdpt_low + 0x03            ; 0xFFFF800000000000
    times 254 dq 0
    dq boot_pdpt_high + 0x03           ; 0xFFFFFF8000000000

align 4096
boot_pdpt_low:
    dq boot_pd + 0x03
    times 511 dq 0

align 4096
boot_pdpt_high:
    times 510 dq 0
    dq boot_pd + 0x03                  ; 0xFFFFFFFF80000000
    dq 0

align 4096
boot_pd:
    %assign addr 0x00000000
    %rep 512
        dq addr + 0x83                 ; Present | RW | PS=1
        %assign addr addr + 0x200000
    %endrep

section .text

higher_half_entry:
    mov rax, KERNEL_VMA + SAMOS_BOOT_STACK_TOP
    mov rsp, rax
    xor rbp, rbp

    ; Zero .bss. The loader already zeroed the window, but the kernel
    ; must not depend on that.
    mov rdi, _kernel_bss_start
    mov rcx, _kernel_bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; boot_info through the direct map: the identity map goes away.
    mov rax, DIRECT_MAP_BASE
    add r12, rax
    mov [samos_boot_info_ptr], r12

    ; Early IDT, gates pointing at the firmware's code selector
    ; (still the live CS until the far return below).
    xor edi, edi
    mov di, cs
    call early_idt_install

    jmp long_mode_entry

kernel_registers:
    mov ax, LONG_MODE_DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret

long_mode_entry:
    ; Lecture 74 - install our own GDT. (our page tables are
    ; already live, loaded by _start.)
    lgdt [gdt_descriptor]

    mov ax, LONG_MODE_DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Lecture 74 - far return to reload CS from our new GDT.
    push QWORD LONG_MODE_CODE_SEG
    mov rax, long_mode_new_gdt_complete
    push rax
    retfq

long_mode_new_gdt_complete:
    ; Re-point the early IDT gates at our own code selector.
    mov edi, LONG_MODE_CODE_SEG
    call early_idt_install

    ; Lecture 87 - copy the framebuffer parameters into the
    ; default_graphics_info struct (layout below). They now come from
    ; boot_info (see src/boot/boot_info.h for the offsets).
    mov rax, [r12 + 16]                ; fb_base
    mov [default_graphics_info + 0], rax
    mov eax, [r12 + 32]                ; fb_width
    mov [default_graphics_info + 8], eax
    mov eax, [r12 + 36]                ; fb_height
    mov [default_graphics_info + 12], eax
    mov eax, [r12 + 40]                ; fb_pitch
    mov [default_graphics_info + 16], eax

    ; Lecture 61 - remap the 8259 PIC: IRQ0..7 -> 0x20..0x27,
    ; IRQ8..15 -> 0x28..0x2F, away from the CPU exception vectors.
    mov al, 0x11
    out 0x20, al
    mov al, 0x20
    out 0x21, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x01
    out 0x21, al

    mov al, 0x11
    out 0xA0, al
    mov al, 0x28
    out 0xA1, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0xA1, al

    ; IRQ masks. Master 0xFB = all-except-cascade masked; IRQ0
    ; (timer) stays masked here. kernel_main programs the PIT
    ; and unmasks IRQ0 after idt_init. Slave 0xFF = all masked.
    mov al, 0xFB
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al

    ; EOI any in-flight PIC interrupts.
    mov al, 0x20
    out 0x20, al
    out 0xA0, al

    ; SamOs divergence: `call`, not the book's `jmp`.
    ; The SysV ABI wants rsp = 8 (mod 16) at function entry; a jmp
    ; with rsp = 0x200000 enters kernel_main misaligned, and the
    ; first `movaps` gcc emits for a zero-initialised struct #GPs.
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

; Lecture 38 - deliberate #DE for IDT testing.
div_test:
    mov rax, 0
    idiv rax
    ret

; ------------------------------------------------------------------
; Early exception IDT
; ------------------------------------------------------------------
; early_idt_install(rdi = code selector): fill gates 0..31 and lidt.
early_idt_install:
    lea rsi, [early_idt]
    lea rdx, [early_stub_table]
    xor ecx, ecx
.next_gate:
    mov rax, [rdx + rcx * 8]           ; stub address
    mov [rsi + 0], ax                  ; offset 15..0
    mov [rsi + 2], di                  ; selector
    mov byte [rsi + 4], 0              ; IST
    mov byte [rsi + 5], 0x8E           ; present, DPL 0, 64-bit interrupt gate
    shr rax, 16
    mov [rsi + 6], ax                  ; offset 31..16
    shr rax, 16
    mov [rsi + 8], eax                 ; offset 63..32
    mov dword [rsi + 12], 0
    add rsi, 16
    inc ecx
    cmp ecx, 32
    jb .next_gate
    lidt [early_idtr]
    ret

; One stub per vector. Vectors with a CPU-pushed error code skip the
; dummy push so every frame has the same layout.
%macro EARLY_STUB 1
early_stub_%1:
%if %1 = 8 || %1 = 10 || %1 = 11 || %1 = 12 || %1 = 13 || %1 = 14 || %1 = 17 || %1 = 21 || %1 = 29 || %1 = 30
%else
    push qword 0
%endif
    push qword %1
    jmp early_fault_common
%endmacro

%assign v 0
%rep 32
EARLY_STUB v
%assign v v + 1
%endrep

early_fault_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp                       ; struct early_fault_frame*
    and rsp, -16
    call early_fault                   ; never returns
.hang:
    cli
    hlt
    jmp .hang

align 8
early_stub_table:
%assign v 0
%rep 32
    dq early_stub_ %+ v
%assign v v + 1
%endrep

early_idtr:
    dw 32 * 16 - 1
    dq early_idt

; Written at run time (TSS slot, framebuffer values), so
; .data: .text is mapped read-only.
section .data

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
    dq gdt                          ; L74 - 8-byte base in long mode

; Lecture 87 - root graphics_info instance. The layout below MUST
; mirror struct graphics_info in src/graphics/graphics.h.
;
; offset 0   qword framebuffer pointer
; offset 8   dword horizontal_resolution
; offset 12  dword vertical_resolution
; offset 16  dword pixels_per_scanline
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

section .bss
align 16
early_idt:
    resb 32 * 16
samos_boot_info_ptr:
    resq 1
