#ifndef CONFIG_H
#define CONFIG_H

// Lecture 73 - the physical address the UEFI bootloader copies
// the kernel to (and the BIOS bootloader's load target as well).
// Both boot paths agree on 0x100000 so kernel.asm runs from the
// same address either way.
#define SAMOS_KERNEL_LOCATION   0x100000

// The loader reserves [SAMOS_KERNEL_LOCATION,
// SAMOS_KERNEL_WINDOW_END) for the kernel image, its .bss and the
// boot stack, which grows down from the window end. linker.ld
// checks that .bss stays below the stack.
#define SAMOS_KERNEL_WINDOW_END     0x200000
#define SAMOS_BOOT_STACK_TOP        SAMOS_KERNEL_WINDOW_END
#define SAMOS_BOOT_STACK_SIZE       0x10000
#define SAMOS_KERNEL_MAX_IMAGE_SIZE (SAMOS_KERNEL_WINDOW_END - SAMOS_KERNEL_LOCATION - SAMOS_BOOT_STACK_SIZE)

// Boot data area, also reserved by the loader: struct
// samos_boot_info (boot/boot_info.h) in the first page, then the
// memory map at SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION.
#define SAMOS_BOOT_DATA_LOCATION    0x20F000
#define SAMOS_BOOT_DATA_PAGES       5
#define SAMOS_BOOT_INFO_LOCATION    SAMOS_BOOT_DATA_LOCATION
#define SAMOS_MEMORY_MAP_MAX_ENTRIES 512

#define KERNEL_CODE_SELECTOR    0x08
#define KERNEL_DATA_SELECTOR    0x10

// Lecture 37 - the 64-bit code segment selector lives at GDT
// offset 0x18 (kernel.asm's GDT entry with L=1).
#define KERNEL_LONG_MODE_CODE_SELECTOR  0x18

// Lecture 55 - TSS selector. The 64-bit TSS descriptor lives
// in GDT slot 7 (its high half spills into slot 8). Selector
// = slot * 8 = 0x38.
#define KERNEL_LONG_MODE_TSS_SELECTOR   0x38

// Lecture 49 - TSS descriptor access byte.
//   bit 7 (P)     = 1  present
//   bits 5..6 (DPL) = 00  ring 0
//   bit 4 (S)     = 0  system descriptor (TSS lives in system class)
//   bits 0..3 (type) = 1001 = available 64-bit TSS
//   = 1000 1001 = 0x89
#define TSS_DESCRIPTOR_TYPE  0x89

// Lecture 50 - GDT slot indices for the C side.
#define KERNEL_LONG_MODE_CODE_GDT_INDEX  3
#define KERNEL_LONG_MODE_DATA_GDT_INDEX  4
#define KERNEL_LONG_MODE_TSS_GDT_INDEX   7

#define SAMOS_TOTAL_INTERRUPTS  256

// Stacks the TSS IST slots switch to for #DF, NMI and #MC.
#define KERNEL_IST_STACK_SIZE   (16 * 1024)

#define SAMOS_HEAP_BLOCK_SIZE           4096

// The book's SAMOS_HEAP_MINIMUM_SIZE_BYTES (one region of
// 100 MiB or more) and the fixed minimal-heap bitmap at 0x01000000
// are gone: the page-frame allocator (memory/pfa.c) manages every
// usable region and places its own table. It reserves
// [SAMOS_KERNEL_LOCATION, max(end of image, SAMOS_BOOT_STACK_TOP)).

#define SAMOS_SECTOR_SIZE       512

// Lecture 18 - BIOS E820 memory map. boot.asm dumps the entries at
// 0x7E00 in real mode; the count goes at 0x7DFE (overwrites the
// 0xAA55 boot signature once the BIOS has finished with it). NOTE:
// 0x7E00 is also SAMOS_HEAP_TABLE_ADDRESS, so the E820 entries are
// LIVE only until kheap_init runs. Read them first, then init the
// heap. A future lecture will copy the entries somewhere safe.
// Lecture 75 - addresses moved out of the BIOS boot sector
// area (0x7DFE / 0x7E00) into a region the UEFI bootloader
// can AllocatePages at: 0x210000. The bootloader writes the
// count as a UINT64 at 0x210000 followed by the entries
// starting at 0x210008.
//
// The 8-byte gap between count (uint64) and entries (vs the
// 2-byte gap in the BIOS layout) is the reason these two
// macros differ by 8 now, not 2.
#define SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION  0x210000
#define SAMOS_MEMORY_MAP_LOCATION                0x210008

#define SAMOS_MAX_FILESYSTEMS        12
#define SAMOS_MAX_FILE_DESCRIPTORS  512

#define SAMOS_TOTAL_GDT_SEGMENTS     6

#define SAMOS_PROGRAM_VIRTUAL_ADDRESS                       0x400000
#define SAMOS_USER_PROGRAM_STACK_SIZE                       (1024 * 16)
#define SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START           0x3FF000
#define SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END             (SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START - SAMOS_USER_PROGRAM_STACK_SIZE)

// Address-space layout.
//
//   0x0000000000000000  user space: nothing below the stack guard,
//   0x00000000003FA000    the guard page (never mapped),
//   0x00000000003FB000    the 16 KiB user stack,
//   0x0000000000400000    ELF segments,
//   0x0000000010000000    process_malloc allocations, one guard page
//                         apart, up to SAMOS_USER_SPACE_END.
//   0xFFFF800000000000  direct map of physical memory (RAM, low MiB,
//                       MMIO mapped on demand), supervisor, NX.
//   0xFFFFC00000000000  multiheap defragment arena (kpalloc).
//   0xFFFFFFFF80000000  kernel image (+ physical address).
//
// The upper half (PML4 slots 256-511) is built once and shared by
// every process; the lower half belongs to the process alone.
#define SAMOS_USER_SPACE_END            0x0000800000000000ULL
#define SAMOS_USER_HEAP_START           0x0000000010000000ULL
#define SAMOS_USER_STACK_GUARD          (SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END - 0x1000)
#define SAMOS_DIRECT_MAP_BASE           0xFFFF800000000000ULL
#define SAMOS_DIRECT_MAP_SIZE           0x0000400000000000ULL
#define SAMOS_KERNEL_HEAP_ARENA_BASE    0xFFFFC00000000000ULL
#define SAMOS_KERNEL_VMA                0xFFFFFFFF80000000ULL

// Lecture 40 + L41 fix - user segment selectors point at the
// L39 user GDT entries with RPL=3 OR'd in. The L39 GDT lays out
// the user code seg at slot 0x28 and the user data seg at
// 0x30, so with RPL=3:
//   USER_CODE_SEGMENT = 0x28 | 3 = 0x2B
//   USER_DATA_SEGMENT = 0x30 | 3 = 0x33
//
// SamOs deviation: upstream PeachOS64 swaps these two macros
// (#define USER_DATA_SEGMENT 0x2B / USER_CODE_SEGMENT 0x33)
// which contradicts the GDT layout - their task.c then assigns
// task->registers.cs = USER_CODE_SEGMENT = 0x33 (their "code"
// macro), but 0x33 indexes the data seg descriptor. It works
// on QEMU because long-mode SS / CS validation is loose, but
// it's wrong. We use the correct mapping.
#define USER_CODE_SEGMENT       0x2B
#define USER_DATA_SEGMENT       0x33

#define SAMOS_MAX_PROGRAM_ALLOCATIONS    1024
#define SAMOS_MAX_PROCESSES              16

#define SAMOS_MAX_ISR80H_COMMANDS        1024

#define SAMOS_KEYBOARD_BUFFER_SIZE       1024

// Lecture 118 - Window title cap (chars including NUL).
#define WINDOW_MAX_TITLE                 128
// Lecture 119 - frame thickness in pixels (each side) and the
// title bar height in pixels. Used by window_create's layout.
#define WINDOW_BORDER_PIXEL_SIZE         2
#define WINDOW_TITLE_BAR_HEIGHT          32

#endif
