#ifndef CONFIG_H
#define CONFIG_H

#define KERNEL_CODE_SELECTOR    0x08
#define KERNEL_DATA_SELECTOR    0x10

#define SAMOS_TOTAL_INTERRUPTS  512

// 100 MiB heap.
#define SAMOS_HEAP_SIZE_BYTES   104857600
#define SAMOS_HEAP_BLOCK_SIZE   4096
#define SAMOS_HEAP_ADDRESS      0x01000000
#define SAMOS_HEAP_TABLE_ADDRESS 0x00007E00

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
