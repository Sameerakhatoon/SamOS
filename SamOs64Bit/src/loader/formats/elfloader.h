#ifndef ELFLOADER_H
#define ELFLOADER_H

#include <stdint.h>
#include <stddef.h>
#include "elf.h"
#include "config.h"
#include "kernel.h"

struct elf_file {
    char  filename[SAMOS_MAX_PATH];
    int   in_memory_size;

    // Physical memory address that this ELF file is loaded at.
    void* elf_memory;

    // Virtual base / end addresses of this binary.
    void* virtual_base_address;
    void* virtual_end_address;

    // Physical base / end addresses of this binary.
    void* physical_base_address;
    void* physical_end_address;
};

void*              elf_memory(struct elf_file* file);
struct elf_header* elf_header(struct elf_file* file);
struct elf64_shdr* elf_sheader(struct elf_header* header);
struct elf64_phdr* elf_pheader(struct elf_header* header);
struct elf64_phdr* elf_program_header(struct elf_header* header, int index);
struct elf64_shdr* elf_section(struct elf_header* header, int index);
char*              elf_str_table(struct elf_header* header);
int                elf_validate_loaded(struct elf_header* header);

void*              elf_virtual_base(struct elf_file* file);
void*              elf_virtual_end(struct elf_file* file);
void*              elf_phys_base(struct elf_file* file);
void*              elf_phys_end(struct elf_file* file);
void*              elf_phdr_phys_address(struct elf_file* file, struct elf64_phdr* phdr);

int  elf_load(const char* filename, struct elf_file** file_out);
void elf_close(struct elf_file* file);

#endif
