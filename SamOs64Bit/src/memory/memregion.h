#ifndef MEMREGION_H
#define MEMREGION_H

// Arch-neutral physical memory description. The page-frame
// allocator and heap see RAM only as a list of these records; the
// boot path builds it (today from the BIOS/UEFI E820 dump, later
// from boot_info). Types match E820 so the conversion is a copy.

#include <stdint.h>

#define MEM_REGION_USABLE    1   // free RAM
#define MEM_REGION_RESERVED  2   // firmware, MMIO, holes
#define MEM_REGION_ACPI      3
#define MEM_REGION_ACPI_NVS  4
#define MEM_REGION_BAD       5
#define MEM_REGION_KERNEL    6   // in use at entry: kernel image, boot stack, the map itself

struct mem_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
};

#endif
