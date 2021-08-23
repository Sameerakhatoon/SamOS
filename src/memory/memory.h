#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

// Lecture 18 - BIOS E820 entry layout. The BIOS writes 24 bytes
// per entry into our buffer (the 4th field is "extended_attr",
// often zero on legacy systems but newer BIOSes set bit 0 to
// indicate the entry should be used).
struct e820_entry {
    uint64_t base_addr;       // start of the region
    uint64_t length;          // size in bytes
    uint32_t type;            // 1 = usable, 2 = reserved, 3 = ACPI reclaim, 4 = ACPI NVS, 5 = bad
    uint32_t extended_attr;
} __attribute__((packed));

size_t              e820_total_accessible_memory(void);
struct e820_entry*  e820_largest_free_entry(void);
size_t              e820_total_entries(void);
struct e820_entry*  e820_entry(size_t index);

void* memset(void* ptr, int c, size_t size);
int   memcmp(void* s1, void* s2, int count);
void* memcpy(void* dest, void* src, int len);

#endif
