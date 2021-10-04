#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Lecture 49 - 64-bit GDT entries.
//
// Renamed `struct gdt` -> `struct gdt_entry` (avoids the
// confusion of having "the GDT" and "a GDT entry" share the same
// name).
//
// Layout matches the 8-byte 64-bit GDT entry. For ordinary code
// / data descriptors the fields are interpreted exactly as in
// 32-bit; for TSS descriptors we use the longer `struct tss_desc_64`
// below (16 bytes - takes TWO GDT slots).
struct gdt_entry {
    uint16_t segment;
    uint16_t base_first;
    uint8_t  base;
    uint8_t  access;
    uint8_t  high_flags;
    uint8_t  base_24_31_bits;
} __attribute__((packed));

// Lecture 49 - 64-bit TSS descriptor.
//
// In long mode the TSS descriptor is 16 bytes - twice the normal
// GDT entry size, so it occupies two consecutive GDT slots. The
// extra 8 bytes hold the high 32 bits of the TSS base address
// plus a reserved zero word.
struct tss_desc_64 {
    // Lower 8 bytes (matches a regular GDT entry layout)
    uint16_t limit0;        // limit 0..15
    uint16_t base0;         // base 0..15
    uint8_t  base1;         // base 16..23
    uint8_t  type;          // access byte (0x89 = P=1 DPL=0 type=9 (avail 64-bit TSS))
    uint8_t  limit1_flags;  // bits 0..3 = limit 16..19; bits 4..7 = flags
    uint8_t  base2;         // base 24..31

    // Upper 8 bytes (the long-mode extension)
    uint32_t base3;         // base 32..63
    uint32_t reserved;      // must be zero
} __attribute__((packed));

// Lecture 49 - direct setters (replace the old structured-array
// pipeline that encoded each entry through a generic structure).
void gdt_set(struct gdt_entry* gdt_entry, void* address,
             uint16_t limit_low, uint8_t access_byte, uint8_t flags);
void gdt_set_tss(struct tss_desc_64* desc, void* tss_addr,
                 uint16_t limit, uint8_t type, uint8_t flags);

#endif
