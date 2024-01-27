; Lecture 129 - tsc_microseconds.
;
; C does not have 128-bit integers by default, but the math
;   microseconds = (current_tsc * 1_000_000) / tsc_frequency
; needs them: a 64-bit current_tsc times 1_000_000 overflows
; 64 bits when current_tsc > ~1.8e13 (a few hours of runtime).
; We use x86_64 MUL (RDX:RAX = RAX * source) for the 64x64
; multiply and DIV (RAX = RDX:RAX / source, RDX = remainder)
; for the 128/64 divide.

[BITS 64]
extern read_tsc
extern tsc_frequency

global tsc_microseconds

tsc_microseconds:
    push rbp
    mov  rbp, rsp
    sub  rsp, 32

    ; tsc_frequency() -> rax
    call tsc_frequency
    mov  qword [rbp-8], rax

    ; read_tsc() -> rax
    call read_tsc
    mov  qword [rbp-16], rax

    ; current_tsc * 1_000_000 -> RDX:RAX
    mov  rax, qword [rbp-16]
    mov  rcx, 1000000
    mul  rcx

    ; (RDX:RAX) / tsc_frequency -> RAX
    mov  rcx, qword [rbp-8]
    div  rcx

    add  rsp, 32
    pop  rbp
    ret
