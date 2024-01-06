; AP start-up trampoline. Not in the book.
;
; smp.c copies ap_trampoline_start..ap_trampoline_end to a free page
; below 1 MiB, fills in the four fields after the first jump and
; sends INIT-SIPI-SIPI with that page as the start-up vector. The AP
; wakes in real mode at page:0 and goes:
;
;   real mode  -> protected mode (its own small GDT, flat 32-bit)
;              -> PAE, CR3 = tr_cr3, EFER.LME and NXE, paging on
;              -> long mode through a far jump to a 64-bit selector
;              -> rsp = tr_stack, rdi = tr_cpu, jmp tr_entry
;
; Every address is relative to the page, found at run time from CS,
; so the blob runs at whatever page smp.c picked. tr_cr3 must be
; below 4 GiB (it is loaded from 32-bit code) and its tables must
; identity-map the page as well as hold the kernel half (smp.c).
; EFER.NXE goes on before paging: the kernel tables set NX, which is
; a reserved bit while NXE is clear.

[BITS 16]
section .rodata

global ap_trampoline_start
global ap_trampoline_end

%define OFF(x) ((x) - ap_trampoline_start)

ap_trampoline_start:
    jmp short tr_real
    align 8
tr_cr3:     dq 0                       ; offset 8
tr_stack:   dq 0                       ; offset 16
tr_entry:   dq 0                       ; offset 24
tr_cpu:     dq 0                       ; offset 32
tr_gdt:
    dq 0
    dq 0x00CF9A000000FFFF              ; 0x08 32-bit code
    dq 0x00CF92000000FFFF              ; 0x10 data
    dq 0x00209A0000000000              ; 0x18 64-bit code
tr_gdtr:
    dw 4 * 8 - 1
    dd 0                               ; linear base, set below
tr_far32:
    dd 0                               ; linear tr_pm32, set below
    dw 0x08
tr_far64:
    dd 0                               ; linear tr_lm64, set below
    dw 0x18

tr_real:
    cli
    cld
    mov ax, cs
    mov ds, ax
    xor ebx, ebx
    mov bx, ax
    shl ebx, 4                         ; ebx = linear address of the page
    lea eax, [ebx + OFF(tr_gdt)]
    mov [OFF(tr_gdtr) + 2], eax
    lea eax, [ebx + OFF(tr_pm32)]
    mov [OFF(tr_far32)], eax
    lea eax, [ebx + OFF(tr_lm64)]
    mov [OFF(tr_far64)], eax
    lgdt [OFF(tr_gdtr)]
    mov eax, cr0
    or eax, 1                          ; PE
    mov cr0, eax
    jmp far dword [OFF(tr_far32)]

[BITS 32]
tr_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr4
    or eax, 1 << 5                     ; PAE
    mov cr4, eax
    mov eax, [ebx + OFF(tr_cr3)]
    mov cr3, eax
    mov ecx, 0xC0000080                ; EFER
    rdmsr
    or eax, (1 << 8) | (1 << 11)       ; LME, NXE
    wrmsr
    mov eax, cr0
    or eax, 0x80000001                 ; PG, PE
    mov cr0, eax
    jmp far dword [ebx + OFF(tr_far64)]

[BITS 64]
tr_lm64:
    mov ebx, ebx                       ; upper half undefined after the switch
    mov rsp, [rbx + OFF(tr_stack)]
    mov rdi, [rbx + OFF(tr_cpu)]
    mov rax, [rbx + OFF(tr_entry)]
    and rsp, -16
    push qword 0                       ; return address slot, as after a call
    jmp rax
ap_trampoline_end:

; void cpu_load_gdt(void* gdtr): lgdt, reload the data segments and
; CS. GS is left alone: loading it would clear the GS base.
section .text
global cpu_load_gdt
cpu_load_gdt:
    lgdt [rdi]
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov ss, ax
    pop rax
    push qword 0x18
    push rax
    retfq
