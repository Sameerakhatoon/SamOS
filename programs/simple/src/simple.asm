; Lecture 58 - the simplest possible 64-bit user program.
;
; jmp $ is a 2-byte unconditional jump to itself - an infinite
; loop. When the kernel loads this at the standard user
; entry virtual address (SAMOS_PROGRAM_VIRTUAL_ADDRESS =
; 0x400000) and iretq's into it, the user task will run this
; one instruction forever. That's enough to verify the
; load + iretq pipeline without needing a working stdlib.

[BITS 64]
jmp $
