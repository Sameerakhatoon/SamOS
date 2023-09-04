; ktest register round trip.
;
; uint64_t ktest_regs_roundtrip(uint64_t* in, uint64_t* out,
;                               uint64_t cmd, uint64_t arg)
;
; Loads rbx, rcx, rdx, rsi, rdi, rbp, r8-r15 from in[1..14] and
; xmm0-15 from in[15..46], pushes `arg` twice (stack items 0 and 1),
; does int 0x80 with rax = cmd, then stores rax and the same
; registers to out[0..46]. Everything except rax (the syscall result)
; must come back unchanged. No .bss: the loader does not give a
; program fresh BSS pages yet.

[BITS 64]
section .text
global ktest_regs_roundtrip

%define XMM_BASE 120

ktest_regs_roundtrip:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    push rsi                           ; out
    push rdx                           ; cmd
    push rcx                           ; stack item 1
    push rcx                           ; stack item 0
    ; [rsp] arg, [rsp+8] arg, [rsp+16] cmd, [rsp+24] out

    movdqu xmm0,  [rdi + XMM_BASE + 0*16]
    movdqu xmm1,  [rdi + XMM_BASE + 1*16]
    movdqu xmm2,  [rdi + XMM_BASE + 2*16]
    movdqu xmm3,  [rdi + XMM_BASE + 3*16]
    movdqu xmm4,  [rdi + XMM_BASE + 4*16]
    movdqu xmm5,  [rdi + XMM_BASE + 5*16]
    movdqu xmm6,  [rdi + XMM_BASE + 6*16]
    movdqu xmm7,  [rdi + XMM_BASE + 7*16]
    movdqu xmm8,  [rdi + XMM_BASE + 8*16]
    movdqu xmm9,  [rdi + XMM_BASE + 9*16]
    movdqu xmm10, [rdi + XMM_BASE + 10*16]
    movdqu xmm11, [rdi + XMM_BASE + 11*16]
    movdqu xmm12, [rdi + XMM_BASE + 12*16]
    movdqu xmm13, [rdi + XMM_BASE + 13*16]
    movdqu xmm14, [rdi + XMM_BASE + 14*16]
    movdqu xmm15, [rdi + XMM_BASE + 15*16]

    mov rax, rdi
    mov rbx, [rax + 8]
    mov rcx, [rax + 16]
    mov rdx, [rax + 24]
    mov rsi, [rax + 32]
    mov rdi, [rax + 40]
    mov rbp, [rax + 48]
    mov r8,  [rax + 56]
    mov r9,  [rax + 64]
    mov r10, [rax + 72]
    mov r11, [rax + 80]
    mov r12, [rax + 88]
    mov r13, [rax + 96]
    mov r14, [rax + 104]
    mov r15, [rax + 112]
    mov rax, [rsp + 16]                ; cmd
    int 0x80

    push rax                           ; out is now at [rsp+32]
    mov rax, [rsp + 32]
    pop qword [rax + 0]
    mov [rax + 8],   rbx
    mov [rax + 16],  rcx
    mov [rax + 24],  rdx
    mov [rax + 32],  rsi
    mov [rax + 40],  rdi
    mov [rax + 48],  rbp
    mov [rax + 56],  r8
    mov [rax + 64],  r9
    mov [rax + 72],  r10
    mov [rax + 80],  r11
    mov [rax + 88],  r12
    mov [rax + 96],  r13
    mov [rax + 104], r14
    mov [rax + 112], r15
    movdqu [rax + XMM_BASE + 0*16],  xmm0
    movdqu [rax + XMM_BASE + 1*16],  xmm1
    movdqu [rax + XMM_BASE + 2*16],  xmm2
    movdqu [rax + XMM_BASE + 3*16],  xmm3
    movdqu [rax + XMM_BASE + 4*16],  xmm4
    movdqu [rax + XMM_BASE + 5*16],  xmm5
    movdqu [rax + XMM_BASE + 6*16],  xmm6
    movdqu [rax + XMM_BASE + 7*16],  xmm7
    movdqu [rax + XMM_BASE + 8*16],  xmm8
    movdqu [rax + XMM_BASE + 9*16],  xmm9
    movdqu [rax + XMM_BASE + 10*16], xmm10
    movdqu [rax + XMM_BASE + 11*16], xmm11
    movdqu [rax + XMM_BASE + 12*16], xmm12
    movdqu [rax + XMM_BASE + 13*16], xmm13
    movdqu [rax + XMM_BASE + 14*16], xmm14
    movdqu [rax + XMM_BASE + 15*16], xmm15

    add rsp, 32
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
