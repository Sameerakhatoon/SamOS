// Lecture 8 - kernel.c is back in the build, but only as a stub.
//
// The 32-bit body of kernel_main (VGA driver, GDT/TSS setup, kheap
// init, paging, IDT, FAT16 init, process loading, scheduler) is
// dropped here. Every line of that came back lecture by lecture in
// the original SamOs work and will come back lecture by lecture
// on this branch as we re-port subsystems:
//
//   Lecture 9   - simple terminal restored
//   Lecture 10  - heap restored
//   Lecture 11+ - paging restored (2 MiB -> 4 KiB pages, multi-heap)
//   Lecture 33+ - IO, IDT in 64-bit
//   Lecture 49+ - GDT + TSS C code
//   Lecture 53+ - keyboard
//   Lecture 54+ - isr80h
//   Lecture 58+ - simple 64-bit user program
//   Lecture 65+ - ELF64 loader
//
// The 32-bit history of every helper is in the main branch.
//
// For now we just need to prove the long-mode entry path (Lecture
// 7) successfully transferred to a 64-bit C function. RIP being
// in this file at runtime is the test (tests64/L08-kernel-c.sh).

#include "kernel.h"

void kernel_main(void)
{
    while (1) {
    }
}
