#ifndef CONFIG_H
#define CONFIG_H

#define KERNEL_CODE_SELECTOR    0x08
#define KERNEL_DATA_SELECTOR    0x10

#define SAMOS_TOTAL_INTERRUPTS  512

// Lecture 24 - this is the MINIMUM heap size: we refuse to bring
// up the kernel if no E820 type-1 region is at least this big.
// The actual minimal-heap data pool size is computed at runtime
// from the chosen region's length (see kheap_init).
#define SAMOS_HEAP_MINIMUM_SIZE_BYTES   104857600
#define SAMOS_HEAP_BLOCK_SIZE           4096

// Lecture 20 - multi-heap layout. The minimal kernel heap is built
// from the first E820-type-1 region whose length covers
// SAMOS_HEAP_SIZE_BYTES. The bitmap goes at a fixed kernel address
// (MINIMAL_HEAP_TABLE_ADDRESS, 16 MiB - well above the kernel
// image at 1 MiB and outside the 0x7E00 E820 dump area, so kheap
// init no longer clobbers the E820 entries). The heap data pool
// starts MINIMAL_HEAP_TABLE_SIZE bytes later. After the minimal
// heap is live, any other E820 type-1 regions get added as
// additional sub-heaps via multiheap_add.
#define SAMOS_MINIMAL_HEAP_TABLE_ADDRESS        0x01000000
#define SAMOS_MINIMAL_HEAP_ADDRESS              0x01100000
#define SAMOS_MINIMAL_HEAP_TABLE_SIZE           (SAMOS_MINIMAL_HEAP_ADDRESS - SAMOS_MINIMAL_HEAP_TABLE_ADDRESS)

#define SAMOS_SECTOR_SIZE       512

// Lecture 18 - BIOS E820 memory map. boot.asm dumps the entries at
// 0x7E00 in real mode; the count goes at 0x7DFE (overwrites the
// 0xAA55 boot signature once the BIOS has finished with it). NOTE:
// 0x7E00 is also SAMOS_HEAP_TABLE_ADDRESS, so the E820 entries are
// LIVE only until kheap_init runs. Read them first, then init the
// heap. A future lecture will copy the entries somewhere safe.
#define SAMOS_MEMORY_MAP_LOCATION                0x7E00
#define SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION  0x7DFE

#define SAMOS_MAX_FILESYSTEMS        12
#define SAMOS_MAX_FILE_DESCRIPTORS  512

#define SAMOS_TOTAL_GDT_SEGMENTS     6

#define SAMOS_PROGRAM_VIRTUAL_ADDRESS                       0x400000
#define SAMOS_USER_PROGRAM_STACK_SIZE                       (1024 * 16)
#define SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START           0x3FF000
#define SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END             (SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START - SAMOS_USER_PROGRAM_STACK_SIZE)

#define USER_DATA_SEGMENT       0x23
#define USER_CODE_SEGMENT       0x1B

#define SAMOS_MAX_PROGRAM_ALLOCATIONS    1024
#define SAMOS_MAX_PROCESSES              16

#define SAMOS_MAX_ISR80H_COMMANDS        1024

#define SAMOS_KEYBOARD_BUFFER_SIZE       1024

#endif
