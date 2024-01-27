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
// L46 - the book's version truncated to int, so any pointer whose
// bit 31 is set (a heap above 2 GiB, a higher-half address) read
// as an error. Only the top 4095 values are errors, the
// same rule as Linux IS_ERR_VALUE. An int argument sign-extends,
// so ISERR(-EIO) still holds.
#define ISERR(value)    ((uint64_t)(int64_t)(value) >= (uint64_t)-4095)

void kernel_main();
void print(const char* str);
void terminal_writechar(char c, char colour);
void panic(const char* msg);
void kernel_page();

// Lecture 44 - opaque-forward the paging descriptor so kernel.h
// doesn't need a paging.h include cycle. kernel_desc() returns
// the kernel's own paging_desc (kernel_paging_desc in kernel.c).
// Callers in task / process code use it to look up the kernel
// address space when they need to walk it (e.g. to find the
// physical backing of a kernel-side scratch buffer).
struct paging_desc;
struct paging_desc* kernel_desc();

// Lecture 93 - cheap MIN/MAX used by graphics_redraw_region's
// intersection math. Header-only; expand the args twice.
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif
