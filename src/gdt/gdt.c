// Lecture 49 - GDT entry + TSS descriptor setters.
//
// Replaces the old `encodeGdtEntry` / `gdt_structured_to_gdt`
// pipeline. The 32-bit code built a table of struct
// gdt_structured (base, limit, type) and then encoded each
// entry into a byte array. L49 is more direct: callers fill in
// a gdt_entry struct via gdt_set, or a tss_desc_64 struct via
// gdt_set_tss.
//
// 64-bit twist: ordinary GDT entries still hold a 32-bit base
// (the address is treated as zero-extended). TSS descriptors
// hold a FULL 64-bit base and span two GDT slots.

#include "gdt.h"
#include "memory/memory.h"
#include "kernel.h"

void gdt_set(struct gdt_entry* gdt_entry, void* address,
             uint16_t limit_low, uint8_t access_byte, uint8_t flags){
    if((uintptr_t)address > 0xFFFFFFFFULL){
        panic("gdt_set: address must fit in 32 bits\n");
    }

    uint32_t base = (uint32_t)(uintptr_t)address;
    gdt_entry->base_first       =  base        & 0xFFFF;
    gdt_entry->base             = (base >> 16) & 0xFF;
    gdt_entry->base_24_31_bits  = (base >> 24) & 0xFF;

    gdt_entry->segment    = limit_low;
    gdt_entry->access     = access_byte;
    gdt_entry->high_flags = flags;
}

void gdt_set_tss(struct tss_desc_64* desc, void* tss_addr,
                 uint16_t limit, uint8_t type, uint8_t flags){
    memset(desc, 0, sizeof(*desc));

    desc->limit0       = limit & 0xFFFF;
    // Bottom 4 bits of limit1_flags carry limit bits 16..19; the
    // top 4 bits are flags. We OR rather than assign so callers
    // can pass non-zero flags too.
    desc->limit1_flags = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));

    uint64_t base = (uint64_t)tss_addr;
    desc->base0 = (uint16_t)( base        & 0xFFFF);
    desc->base1 = (uint8_t) ((base >> 16) & 0xFF);
    desc->base2 = (uint8_t) ((base >> 24) & 0xFF);
    desc->base3 = (uint32_t)((base >> 32) & 0xFFFFFFFF);

    desc->type     = type;       // 0x89 = present + DPL=0 + 64-bit-TSS available
    desc->reserved = 0;
}
