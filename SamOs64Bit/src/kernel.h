#ifndef KERNEL_H
#define KERNEL_H

#define VGA_WIDTH  80
#define VGA_HEIGHT 20

#define SAMOS_MAX_PATH  108

// L45 - widen the ERROR macro cast chain to intptr_t so a small
// negative error code -> uintptr_t -> void* round trip survives
// 64-bit pointer width without sign-extending nonsense.
#define ERROR(value)    (void*)((intptr_t)(value))
#define ERROR_I(value)  (int)((intptr_t)(value))
// L46 - cast through int64_t before truncating to int so that
// a 64-bit error code with the high bit set survives the
// negativity check intact. Necessary for ERROR(value) round
// trips when the kernel grows >2 GiB heap pointers.
#define ISERR(value)    ((int)((int64_t)(value)) < 0)

void kernel_main();
void print(const char* str);
void terminal_writechar(char c, char colour);
void panic(const char* msg);
void kernel_page();
void kernel_registers();

// Lecture 44 - opaque-forward the paging descriptor so kernel.h
// doesn't need a paging.h include cycle. kernel_desc() returns
// the kernel's own paging_desc (kernel_paging_desc in kernel.c).
// Callers in task / process code use it to look up the kernel
// address space when they need to walk it (e.g. to find the
// physical backing of a kernel-side scratch buffer).
struct paging_desc;
struct paging_desc* kernel_desc();

#endif
