#include "elfloader.h"
#include "fs/file.h"
#include "status.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "arch/arch.h"
#include "kernel.h"
#include "config.h"
#include <stdbool.h>

// The loader trusts nothing in the file. Limits for a
// static SamOS program.
#define ELF_MAX_PHNUM         32
#define ELF_MAX_SEGMENT_SIZE  (64ULL * 1024 * 1024)
#define ELF_MAX_FILE_SIZE     (64ULL * 1024 * 1024)

const char elf_signature[] = { 0x7F, 'E', 'L', 'F' };

static bool elf_valid_signature(void* buffer){
    return memcmp(buffer, (void*)elf_signature, sizeof(elf_signature)) == 0;
}

// Lecture 65 - accept ELFCLASS64 now (was ELFCLASS32).
// And only that; ELFCLASSNONE was accepted too.
static bool elf_valid_class(struct elf_header* header){
    return header->e_ident[EI_CLASS] == ELFCLASS64;
}

static bool elf_valid_encoding(struct elf_header* header){
    return header->e_ident[EI_DATA] == ELFDATA2LSB;
}

// Called now (the book defined it and never used it).
static bool elf_is_executable(struct elf_header* header){
    return header->e_type == ET_EXEC
        && header->e_machine == ARCH_ELF_MACHINE     // This arch's
        && header->e_version == EV_CURRENT
        && header->e_ident[EI_VERSION] == EV_CURRENT;
}

static bool elf_has_program_header(struct elf_header* header){
    return header->e_phoff != 0
        && header->e_phnum != 0
        && header->e_phnum <= ELF_MAX_PHNUM
        && header->e_phentsize == sizeof(struct elf64_phdr);
}

void* elf_memory(struct elf_file* file){
    return file->elf_memory;
}

struct elf_header* elf_header(struct elf_file* file){
    return file->elf_memory;
}

struct elf64_shdr* elf_sheader(struct elf_header* header){
    return (struct elf64_shdr*)((uintptr_t)header + header->e_shoff);
}

struct elf64_phdr* elf_pheader(struct elf_header* header){
    if(header->e_phoff == 0){
        return 0;
    }
    return (struct elf64_phdr*)((uintptr_t)header + header->e_phoff);
}

struct elf64_phdr* elf_program_header(struct elf_header* header, int index){
    return &elf_pheader(header)[index];
}

struct elf64_shdr* elf_section(struct elf_header* header, int index){
    return &elf_sheader(header)[index];
}

char* elf_str_table(struct elf_header* header){
    return (char*)header + elf_section(header, header->e_shstrndx)->sh_offset;
}

// Header fields only, for a header that is already in memory.
int elf_validate_loaded(struct elf_header* header){
    return (elf_valid_signature(header)
            && elf_valid_class(header)
            && elf_valid_encoding(header)
            && elf_is_executable(header)
            && elf_has_program_header(header))
                ? SAMOS_ALL_OK
                : -EINFORMAT;
}

// One PT_LOAD: inside the file, inside user space above the
// stack, not writable and executable at once.
static bool elf_valid_load_segment(struct elf64_phdr* phdr, size_t file_size){
    if(phdr->p_filesz > phdr->p_memsz || phdr->p_memsz > ELF_MAX_SEGMENT_SIZE){
        return false;
    }
    if(phdr->p_offset > file_size || phdr->p_filesz > file_size - phdr->p_offset){
        return false;
    }
    if(phdr->p_vaddr < SAMOS_PROGRAM_VIRTUAL_ADDRESS || phdr->p_vaddr >= SAMOS_USER_SPACE_END ||
       phdr->p_memsz > SAMOS_USER_SPACE_END - phdr->p_vaddr){
        return false;
    }
    return !((phdr->p_flags & PF_W) && (phdr->p_flags & PF_X));
}

static uint64_t elf_page_down(uint64_t v){
    return v & ~(uint64_t)(PAGING_PAGE_SIZE - 1);
}

static uint64_t elf_page_up(uint64_t v){
    return elf_page_down(v + PAGING_PAGE_SIZE - 1);
}

// Two segments may not share a page: each gets its own pages and
// permissions (process_map_elf).
static bool elf_segments_overlap(struct elf64_phdr* a, struct elf64_phdr* b){
    return elf_page_down(a->p_vaddr) < elf_page_up(b->p_vaddr + b->p_memsz)
        && elf_page_down(b->p_vaddr) < elf_page_up(a->p_vaddr + a->p_memsz);
}

// The whole file. -EINFORMAT means "not an ELF file"
// (process.c may then run it as a flat binary); anything wrong with
// a file that carries the signature is -EBADEXEC.
static int elf_validate(struct elf_header* header, size_t file_size){
    if(file_size < sizeof(elf_signature) || !elf_valid_signature(header)){
        return -EINFORMAT;
    }
    if(file_size < sizeof(struct elf_header) || elf_validate_loaded(header) < 0){
        return -EBADEXEC;
    }
    uint64_t ph_bytes = (uint64_t)header->e_phnum * sizeof(struct elf64_phdr);
    if(header->e_phoff > file_size || ph_bytes > file_size - header->e_phoff){
        return -EBADEXEC;
    }

    int  loads    = 0;
    bool entry_ok = false;
    for(int i = 0; i < header->e_phnum; i++){
        struct elf64_phdr* phdr = elf_program_header(header, i);
        if(phdr->p_type == PT_DYNAMIC || phdr->p_type == PT_INTERP){
            return -EBADEXEC;   // static executables only
        }
        if(phdr->p_type != PT_LOAD || phdr->p_memsz == 0){
            continue;
        }
        if(!elf_valid_load_segment(phdr, file_size)){
            return -EBADEXEC;
        }
        for(int j = 0; j < i; j++){
            struct elf64_phdr* other = elf_program_header(header, j);
            if(other->p_type == PT_LOAD && other->p_memsz && elf_segments_overlap(phdr, other)){
                return -EBADEXEC;
            }
        }
        if((phdr->p_flags & PF_X) && header->e_entry >= phdr->p_vaddr &&
           header->e_entry - phdr->p_vaddr < phdr->p_memsz){
            entry_ok = true;
        }
        loads++;
    }
    return (loads > 0 && entry_ok) ? SAMOS_ALL_OK : -EBADEXEC;
}

void* elf_virtual_base(struct elf_file* file){
    return file->virtual_base_address;
}

void* elf_virtual_end(struct elf_file* file){
    return file->virtual_end_address;
}

void* elf_phys_base(struct elf_file* file){
    return file->physical_base_address;
}

void* elf_phys_end(struct elf_file* file){
    return file->physical_end_address;
}

void* elf_phdr_phys_address(struct elf_file* file, struct elf64_phdr* phdr){
    return elf_memory(file) + phdr->p_offset;
}

// Lecture 65 - p_vaddr is now elf64_addr (= uint64_t), same
// width as void*, so the (uintptr_t) cast L63 added is gone.
//
// Records the lowest and highest segment addresses only. The
// book also zeroed p_memsz - p_filesz bytes here, from the FIRST
// segment's base in the file buffer, which wrote over
// code; process_map_elf now gives every segment fresh zeroed pages.
static int elf_process_phdr_pt_load(struct elf_file* elf_file, struct elf64_phdr* phdr){
    if(elf_file->virtual_base_address >= (void*)phdr->p_vaddr ||
       elf_file->virtual_base_address == 0x00){
        elf_file->virtual_base_address  = (void*)phdr->p_vaddr;
        elf_file->physical_base_address = elf_memory(elf_file) + phdr->p_offset;
    }

    uintptr_t end_virtual_address = phdr->p_vaddr + phdr->p_filesz;
    if(elf_file->virtual_end_address <= (void*)end_virtual_address ||
       elf_file->virtual_end_address == 0x00){
        elf_file->virtual_end_address  = (void*)end_virtual_address;
        elf_file->physical_end_address = elf_memory(elf_file) + phdr->p_offset + phdr->p_filesz;
    }
    return 0;
}

static int elf_process_pheaders(struct elf_file* elf_file){
    struct elf_header* header = elf_header(elf_file);
    for(int i = 0; i < header->e_phnum; i++){
        struct elf64_phdr* phdr = elf_program_header(header, i);
        if(phdr->p_type == PT_LOAD && phdr->p_memsz){
            elf_process_phdr_pt_load(elf_file, phdr);
        }
    }
    return 0;
}

int elf_load(const char* filename, struct elf_file** file_out){
    int fd  = 0;
    int res = 0;
    struct elf_file* elf_file = kzalloc(sizeof(struct elf_file));
    if(!elf_file){
        return -ENOMEM;
    }
    res = fopen(filename, "r");
    if(res <= 0){
        res = -EIO;
        goto out;
    }
    fd = res;

    struct file_stat stat;
    res = fstat(fd, &stat);
    if(res < 0){
        goto out;
    }
    // An empty or huge file never reaches kzalloc/fread.
    if(stat.filesize == 0 || stat.filesize > ELF_MAX_FILE_SIZE){
        res = stat.filesize == 0 ? -EINFORMAT : -EBADEXEC;
        goto out;
    }

    elf_file->elf_memory = kzalloc(stat.filesize);
    if(!elf_file->elf_memory){
        res = -ENOMEM;
        goto out;
    }
    elf_file->in_memory_size = stat.filesize;
    // A short read was ignored ("res < 0" only).
    if(fread(elf_file->elf_memory, stat.filesize, 1, fd) != 1){
        res = -EIO;
        goto out;
    }

    res = elf_validate(elf_header(elf_file), stat.filesize);
    if(res < 0){
        goto out;
    }
    elf_process_pheaders(elf_file);
    *file_out = elf_file;

out:
    if(fd > 0){
        fclose(fd);
    }
    if(res < 0){
        elf_close(elf_file);   // The book leaked both on every failure
    }
    return res;
}

void elf_close(struct elf_file* file){
    if(!file){
        return;
    }
    kfree(file->elf_memory);
    kfree(file);
}
