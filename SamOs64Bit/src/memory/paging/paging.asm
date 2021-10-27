; Lecture 13 - paging asm helpers in long mode.
;
; Two leaf operations the C side calls into:
;   paging_load_directory(uintptr_t*)   - mov cr3, arg
;   paging_invalidate_tlb_entry(void*)  - invlpg [arg]
;
; AMD64 SysV ABI: first integer/pointer arg is RDI.

[BITS 64]

section .asm

global paging_load_directory
global paging_invalidate_tlb_entry

; void paging_load_directory(uintptr_t* directory)
paging_load_directory:
    mov rax, rdi                ; arg 0 -> RAX
    mov cr3, rax                ; install as new CR3
    ret

; void paging_invalidate_tlb_entry(void* addr)
paging_invalidate_tlb_entry:
    invlpg [rdi]                ; flush the TLB entry that covers *rdi
    ret
