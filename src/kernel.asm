; Lecture 7 - rewrite kernel.asm to drop into 64-bit long mode.
;
; boot.asm loads us at 0x100000 in 32-bit protected mode (CS=0x08,
; DS=0x10 from the bootloader's tiny GDT). We then:
;   1. Reload data segs with our own DS=0x10 (still 32-bit DS).
;   2. Park the stack at 0x200000.
;   3. Install a richer GDT with a 64-bit code segment at 0x18.
;   4. Turn on PAE (CR4 bit 5) - required by 4-level paging that long
;      mode uses.
;   5. Point CR3 at PML4_Table - an identity-mapping skeleton that
;      covers the first 4 MiB via two 2-MiB pages.
;   6. Set IA32_EFER.LME (MSR 0xC000_0080 bit 8) to arm long mode.
;   7. Enable paging in CR0 (bit 31). The instruction AFTER this
;      mov is the moment the CPU latches long-mode-compatible state.
;   8. Far-jump to the 64-bit code segment selector (0x18). That's
;      the actual transition into 64-bit mode; the CPU widens
;      registers and switches to 64-bit instruction decoding.
;   9. [BITS 64] long_mode_entry just spins on `jmp $` for now -
;      Lecture 8 brings kernel.c back online from here.

[BITS 32]
global _start
extern kernel_main

CODE_SEG equ 0x08
DATA_SEG equ 0x10
LONG_MODE_CODE_SEG equ 0x18
LONG_MODE_DATA_SEG equ 0x20

_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov ebp, 0x00200000
    mov esp, ebp

    lgdt [gdt_descriptor]

    ; CR4.PAE - Physical Address Extension. With PAE off, the CPU
    ; will refuse the long-mode transition with a #GP.
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Point CR3 at the PML4 we link below.
    mov eax, PML4_Table
    mov cr3, eax

    ; IA32_EFER MSR - set LME (Long Mode Enable, bit 8). At this
    ; point the CPU is still in protected mode; LME is only ARMED.
    ; Long mode activates when paging gets turned on next.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; CR0.PG = 1 - this is the moment the CPU completes the long-
    ; mode transition (PAE + LME + PG all set).
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Far jump to the 64-bit code segment. The CPU swaps to 64-bit
    ; decoding here.
    jmp LONG_MODE_CODE_SEG:long_mode_entry

[BITS 64]
long_mode_entry:
    ; Reload DS/ES/FS/GS/SS with the 64-bit data segment selector.
    ; In long mode the limit/base of data segments are ignored, but
    ; the L bit / system-segment encoding matters - we need a real
    ; long-mode data descriptor (selector 0x20 in our GDT) rather
    ; than the 32-bit one we used during the transition.
    mov ax, LONG_MODE_DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up a 64-bit stack pointer. The top 32 bits of RSP/RBP
    ; were undefined while we were in 32-bit code; explicitly zero
    ; them by writing the whole 64-bit register.
    mov rsp, 0x00200000
    mov rbp, rsp

    ; Hand off to kernel_main. The System V AMD64 ABI says RDI/RSI/
    ; RDX/RCX/R8/R9 are caller-saved arg registers; kernel_main()
    ; takes no args so we don't need to pre-load anything.
    jmp kernel_main

    ; If kernel_main ever returns we park forever.
    jmp $


; ------------------------------------------------------------------
; Global descriptor table (GDT)
; ------------------------------------------------------------------
align 8
gdt:
    dq 0x0000000000000000           ; Null descriptor

    ; 32-bit code segment (selector 0x08) - we entered protected
    ; mode in this and are still using it for the long-mode setup.
    dw 0xffff                       ; Limit 0-15
    dw 0                            ; Base 0-15
    db 0                            ; Base 16-23
    db 0x9a                         ; Access (P=1 DPL=00 S=1 E=1 RW=1 A=0)
    db 11001111b                    ; G=1 D=1 L=0 AVL=0 + Limit 16-19
    db 0                            ; Base 24-31

    ; 32-bit data segment (selector 0x10) - DS/ES/SS used in the
    ; lgdt and CR* setup above.
    dw 0xffff
    dw 0
    db 0
    db 0x92                         ; Access (P=1 DPL=00 S=1 E=0 RW=1 A=0)
    db 11001111b
    db 0

    ; 64-bit code segment (selector 0x18) - far-jump target.
    ; In long mode the limit and base of code segments are ignored;
    ; what matters is L=1 (the "long mode" bit) in the flags byte.
    dw 0x0000
    dw 0x0000
    db 0x00
    db 0x9A                         ; Access (P=1 DPL=00 S=1 E=1 RW=1)
    db 0x20                         ; L=1 (long mode segment), D=0 mandatory
    db 0x00

    ; 64-bit data segment (selector 0x20) - reload target for the
    ; data segregs once we're in long mode. Long-mode data segments
    ; ignore base/limit; the flag byte stays 0 (no L for data).
    dw 0x0000
    dw 0x0000
    db 0x00
    db 0x92                         ; Access (P=1 DPL=00 S=1 E=0 RW=1)
    db 0x00
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt - 1            ; Limit
    dd gdt                          ; Base (32-bit lidg form)

; ------------------------------------------------------------------
; 4-level page-table skeleton: PML4 -> PDPT -> PD with 2-MiB pages.
; Identity-maps the first 4 MiB of physical memory at virtual
; addresses 0..0x3FFFFF. PS (bit 7) in the PD entries makes them
; 2-MiB leaves; we don't need a per-4-KiB page table yet.
; ------------------------------------------------------------------
align 4096
PML4_Table:
    dq PDPT_TABLE + 0x03            ; Present + RW. Points at PDPT.
    times 511 dq 0

align 4096
PDPT_TABLE:
    dq PD_Table + 0x03              ; Present + RW. Points at PD.
    times 511 dq 0

; Lecture 12: grow the 4-KiB page tables. PD_Table has %rep 30
; entries (60 MiB coverage), each pointing to a distinct 4-KiB
; slice of a single PT_Table holding %rep 512*30 = 15360 entries.
;
; Why 30 and not 100 (PeachOS64's value)? Their L12 still fits in
; 250 sectors because their kernel.bin is smaller at that point.
; Our kernel.bin already has the kheap object files; trying to ship
; %rep 100 (= ~400 KiB of statically-initialised PT entries) blows
; past 250 sectors and the BIOS 8-bit LBA28 sector-count cap. 30
; entries = ~120 KiB of PT_Table, fits in 250 sectors, still covers
; the kheap body at 0x01000000.
;
; The kheap body at 0x01000000 (16 MiB) sits well inside this 60 MiB
; window. L13 brings the C-side mapper online and the static %rep
; goes away entirely.

%define PS_FLAG 0x03                    ; Present | RW (no PageSize at PT level)
%define PAGE_INCREMENT 0x1000           ; 4 KiB stride
%define PD_ENTRY_COUNT 20               ; 20 * 2 MiB = 40 MiB total (covers heap @ 16 MiB)

align 4096
PD_Table:
    %assign addr 0x00000000
    %rep PD_ENTRY_COUNT
        dq PT_Table + addr + 0x03
        %assign addr addr + 0x200000
    %endrep
    times 512 - PD_ENTRY_COUNT dq 0

align 4096
PT_Table:
    %assign addr 0x00000000
    %rep 512 * PD_ENTRY_COUNT
        dq addr + PS_FLAG
        %assign addr addr + PAGE_INCREMENT
    %endrep
