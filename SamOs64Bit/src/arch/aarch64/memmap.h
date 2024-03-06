#ifndef ARCH_AARCH64_MEMMAP_H
#define ARCH_AARCH64_MEMMAP_H

#include <stdint.h>
#include "firmware/fdt.h"
#include "memory/memregion.h"

// The physical memory map from the device tree, as the
// arch-neutral struct mem_region list the memory manager defined for the x86 E820
// map: every /memory reg entry USABLE, then the reservations, which
// win where they overlap (pfa.c):
//   the kernel image (text, data, bss, boot stack)   KERNEL
//   the DTB blob itself                              KERNEL
//   each /memreserve/ entry                          RESERVED
//   each /reserved-memory reg entry                  RESERVED (| MEMMAP_NOMAP)
//   The initrd (/chosen linux,initrd-start/end) KERNEL
// A dynamic /reserved-memory node (size only) is listed for the
// report but not placed: nothing here allocates for a driver yet.
// Built with the MMU off: it stores no pointers, only addresses and
// DT node offsets, and names nothing until memmap_print.

#define MEMMAP_MAX      32
#define MEMMAP_NOMAP    0x100   // or'ed into type: "no-map", kept out of the direct map

enum memmap_source { SRC_MEMORY, SRC_KERNEL, SRC_DTB, SRC_MEMRESERVE, SRC_RESMEM, SRC_INITRD, SRC_RESMEM_DYNAMIC };

// 0, or a negative FDT_ERR_* / -100 when the table is full.
int  memmap_build(const struct fdt* f, uint64_t dtb_pa, uint64_t kernel_start, uint64_t kernel_end);
// The list; dynamic entries are not in it.
const struct mem_region* memmap_regions(int* n);
// The initrd the boot loader placed (QEMU -initrd): 0 and its
// range, or -1 without one.
int  memmap_initrd(uint64_t* base, uint64_t* size);
// Is [base, base+size) inside a no-map region? 1/0.
int  memmap_nomap(uint64_t base, uint64_t size);
// One "memmap:" line per entry; `f` (MMU on) names the DT nodes.
void memmap_print(const struct fdt* f);

#endif
