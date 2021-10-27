#include "elf.h"
#include <stdint.h>
#include <stddef.h>

void* elf_get_entry_ptr(struct elf_header* elf_header){
    // L63 - widen the cast: e_entry is 32 bits in ELF32 headers,
    // but void* is 64 bits on x86_64. uintptr_t intermediate
    // suppresses GCC's int-to-pointer-cast-of-different-size.
    return (void*)(uintptr_t)elf_header->e_entry;
}

// L65 - return type widened to elf64_addr.
elf64_addr elf_get_entry(struct elf_header* elf_header){
    return elf_header->e_entry;
}
