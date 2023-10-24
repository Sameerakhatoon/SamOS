; ktest register round trip.
;
; uint64_t ktest_regs_roundtrip(uint64_t* in, uint64_t* out,
;                               uint64_t cmd)
;
; Loads rbx, rcx, rdx, rsi, rdi, rbp, r8-r15 from in[1..14] and
; xmm0-15 from in[15..46], does int 0x80 with rax = cmd, then stores
; rax and the same registers to out[0..46]. Everything except rax
; (the syscall result) must come back unchanged. The syscall
; arguments are the loaded rdi (in[5]) and rsi (in[4]).

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
    ; [rsp] cmd, [rsp+8] out

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
    mov rax, [rsp]                     ; cmd
    int 0x80

    push rax                           ; out is now at [rsp+16]
    mov rax, [rsp + 16]
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

    add rsp, 16
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; Raw syscalls for the isolation probes.
;
; long ktest_syscall(long cmd, long a0, long a1, long a2, long a3)
;   a0..a3 go in rdi, rsi, rdx, r10. Returns rax.
global ktest_syscall
ktest_syscall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    mov r10, r8
    int 0x80
    ret

; long ktest_syscall_rsp(long cmd, unsigned long rsp)
;   int 0x80 with the user rsp set to `rsp` (a kernel address, say).
;   at first the kernel read the arguments from there; since the scheduler work it
;   reads none from the stack, and `rsp` is also the first argument.
global ktest_syscall_rsp
ktest_syscall_rsp:
    push rbx
    mov rbx, rsp
    mov rax, rdi
    mov rdi, rsi
    mov rsp, rsi
    int 0x80
    mov rsp, rbx
    pop rbx
    ret
