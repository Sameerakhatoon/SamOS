#ifndef SAMOS_RISCV64_MMU_H
#define SAMOS_RISCV64_MMU_H

// Sv48 or Sv39 paging, the riscv64 side of x86's isolation work.
//
// The mode is probed at boot (mmu_boot): satp is written with Sv48 and
// read back; an unsupported mode leaves satp unchanged, so Sv39 is the
// fallback. One table walker serves both, parameterised by the number
// of levels (4 or 3). Layout, per mode (kernel image the same in both):
//
//                 Sv48                     Sv39
//   user          0 .. 0x00007fffffffffff  0 .. 0x0000003fffffffff
//   direct map    0xffff800000000000       0xffffffc000000000
//   heap arena    0xffffc00000000000       0xffffffd000000000 
//   mmio window   0xffffe00000000000       0xffffffe000000000  (1 GiB)
//   kernel image  0xffffffff80200000       0xffffffff80200000
//
// The direct map covers every /memory region (x86's 0xffff800000000000
// in Sv48; Sv39 has only 256 GiB of upper half, so it starts at the
// bottom of it). The shared multiheap's arena (x86's address in
// Sv48) moved the Sv48 mmio window up (layout.h). The image is linked
// at KERNEL_VMA and mapped onto
// wherever OpenSBI loaded it, with W^X: text RX, rodata R, data, bss,
// page-table pool and boot stack RW. Kernel pages are global (G).

#define KERNEL_VMA 0xffffffff80200000ul   // must match linker.ld

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct fdt;

#define PAGE_SIZE 4096ul

// PTE bits.
#define PTE_V (1ul << 0)
#define PTE_R (1ul << 1)
#define PTE_W (1ul << 2)
#define PTE_X (1ul << 3)
#define PTE_U (1ul << 4)
#define PTE_G (1ul << 5)
#define PTE_A (1ul << 6)
#define PTE_D (1ul << 7)
#define PTE_RWX (PTE_R | PTE_W | PTE_X)

#define SATP_MODE_SV39 8ul
#define SATP_MODE_SV48 9ul

extern uint64_t dmap_base;   // direct map: VA = dmap_base + PA
extern uint64_t dmap_size;   // Its size, and the heap arena (layout.h)
extern uint64_t arena_base;
extern uint64_t user_end;    // first address above the user half
extern uint64_t kernel_va_off;   // VA - PA of the image
extern int mmu_levels;       // 4 = Sv48, 3 = Sv39

static inline void* phys_to_virt(uint64_t pa)
{
    return (void*)(pa + dmap_base);
}

static inline uint64_t virt_to_phys(const void* va)
{
    return (uint64_t)va - dmap_base;
}

// Physical address of a kernel image symbol (works before and after
// the switch).
uint64_t kernel_image_pa(const void* sym);

static inline void sfence_vma(void)
{
    __asm__ volatile("sfence.vma zero, zero" : : : "memory");
}

// boot.S, at the physical address with the MMU off: probe the mode,
// build the boot tables (image identity mapped for the switch, image at
// KERNEL_VMA, image and DTB in the direct map) and return the satp
// value to load, or 0 when neither Sv48 nor Sv39 is accepted.
uint64_t mmu_boot(uint64_t dtb_pa);

// First thing after the switch: remove the identity map, probe ASIDs,
// print the mode and layout.
void mmu_init(void);
// Direct map every /memory region, list /memory and every reservation
// (the initrd too) for the frame allocator, then seal the
// kernel's root-level upper half (see mm_create): the mmio window's
// entry and the heap arena's over RAM are made first. 0 or -1.
// The shared allocator (memory/pfa.c) is built from that list
// by kheap_init, through boot_info's memory map (main.c).
int  mmu_memory_init(const struct fdt* f);
// QEMU's -initrd from /chosen: 0 and where it is, or -1.
int  mmu_initrd(uint64_t* base, uint64_t* size);
uint64_t mmu_kernel_root(void);   // physical

// The walker for paging.c: the PTE for `va` at `level` under
// `root` (tables above created when `alloc`, NULL if a leaf is in the
// way or a sealed root entry would be needed), and the leaf for `va`.
uint64_t* pt_entry(uint64_t root, uint64_t va, int level, bool alloc);
// The 4 KiB PTE for `va`, splitting a larger leaf in the way.
uint64_t* pt_entry_split(uint64_t root, uint64_t va);
uint64_t  pt_lookup(uint64_t root, uint64_t va, int* level);
uint64_t  make_satp(uint64_t root, uint64_t asid);
// Map `size` bytes of device registers at `pa` RW, never executable,
// into the mmio window. Returns the virtual address of `pa`, 0 when
// the window is full.
uintptr_t mmu_map_mmio(uint64_t pa, uint64_t size);

// Kernel root table walk: the leaf PTE for `va` (0 if unmapped) and,
// optionally, the level it sits at (0 = 4 KiB).
uint64_t mmu_kernel_pte(uint64_t va, int* level);
// Valid entries in the lower half of the kernel root (0 once the
// identity map is gone).
int      mmu_kernel_low_entries(void);
uint64_t mmu_kernel_pa(void);   // where the image was loaded
// Leaves anywhere under the kernel root with every bit of `flags` set
// (PTE_W | PTE_X: the W^X check).
unsigned long mmu_kernel_leaves(uint64_t flags);
// The region list the frame allocator was built from: /memory as
// MEM_REGION_USABLE, every reservation after it.
struct mem_region;
const struct mem_region* mmu_regions(int* count);

// ---- address spaces -----------------------------------------------
// A user address space: its own root, the kernel's upper half shared
// by copying the root-level entries, which mmu_memory_init has made
// final (x86's isolation work preallocates every upper-half PML4 slot for the same
// reason).
struct mm {
    uint64_t root;      // physical
    uint16_t asid;      // 0 when ASIDs are not used
};

int  mm_create(struct mm* mm);
// User pages (U set) for [va, va + size): `prot` is PTE_R/W/X. 0 or -1
// (outside the user half, overlap, or no memory).
int  mm_map(struct mm* mm, uint64_t va, uint64_t pa, uint64_t size, uint64_t prot);
// Load satp: the ASID tags the TLB entries when the hart has them,
// otherwise the whole TLB is flushed. NULL = the kernel's own root.
void mm_switch(struct mm* mm);
struct mm* mm_current(void);
// Free the root and every lower-half table (not the pages the
// leaves point at); switches to the kernel root first if `mm` is live.
void mm_destroy(struct mm* mm);
// Drop `va` of `mm` from this hart's TLB.
void mm_flush_page(struct mm* mm, uint64_t va);
unsigned mmu_asid_bits(void);
// samos.mmu_noasid: ignore ASIDs and flush on every switch.
void mmu_disable_asids(void);
#endif

#endif
