#ifndef ARCH_RISCV64_LAYOUT_H
#define ARCH_RISCV64_LAYOUT_H
// The address-space layout the shared kernel sees on riscv64.
//
// config.h names it as constants (x86's). Here the paging mode is
// probed at boot (Sv48, else Sv39), so the names are the
// variables mmu.c sets for the mode. The Makefile -includes this file
// before every riscv64 source, so config.h's defaults stay unused.
//
//                    Sv48                  Sv39
//   user below       0x0000800000000000    0x0000004000000000
//   direct map       0xffff800000000000    0xffffffc000000000  (64 TiB / 64 GiB)
//   heap arena       0xffffc00000000000    0xffffffd000000000  (multiheap, + PA)
//   mmio window      0xffffe00000000000    0xffffffe000000000  (1 GiB)
//   kernel image     0xffffffff80200000    0xffffffff80200000

#ifndef __ASSEMBLER__
#include <stdint.h>

extern uint64_t dmap_base, dmap_size, arena_base, user_end, kernel_va_off;
extern volatile uint64_t riscv64_boot_markers[];

#define SAMOS_USER_SPACE_END            user_end
#define SAMOS_DIRECT_MAP_BASE           dmap_base
#define SAMOS_DIRECT_MAP_SIZE           dmap_size
#define SAMOS_KERNEL_HEAP_ARENA_BASE    arena_base
#define SAMOS_KERNEL_VMA                kernel_va_off   // image VA - PA
#endif

#endif
