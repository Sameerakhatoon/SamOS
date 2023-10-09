#include "memory.h"
#include "config.h"
#include "boot/boot_info.h"

// The map is read through the direct map.
#define E820_COUNT   ((uint16_t*)(SAMOS_DIRECT_MAP_BASE + SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION))
#define E820_ENTRIES ((struct e820_entry*)(SAMOS_DIRECT_MAP_BASE + SAMOS_MEMORY_MAP_LOCATION))

// Lecture 18 - E820 readers. The book's boot.asm put the entries at
// 0x7E00; since the UEFI boot work the UEFI loader writes the count at
// SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION and the entries right
// after it, in the boot data area (SAMOS_E820_TYPE_KERNEL). The PFA
// reserves that area, so the map stays readable after kheap_init.

// Lecture 20 - typed helpers for multiheap to iterate E820.

size_t e820_total_entries(void){
    return *E820_COUNT;
}

extern struct samos_boot_info* samos_boot_info_ptr;

// Reports loader and boot-services
// memory as SAMOS_E820_TYPE_RECLAIMABLE. Nothing the kernel reads
// after entry lives there: boot_info, its command line and this map
// are in the boot data area (SAMOS_E820_TYPE_KERNEL), the
// framebuffer is MMIO, and kernel.asm has left the firmware's stack,
// GDT, IDT and page tables before kernel_main. The one pointer that
// can lead into it is the ACPI RSDP, so a region holding it keeps
// its type. Every other one becomes usable RAM, for the PFA and the
// direct map. Returns the bytes reclaimed.
size_t e820_reclaim_loader_memory(void){
    size_t total_entries        = *E820_COUNT;
    struct e820_entry* entries  = E820_ENTRIES;
    uint64_t rsdp  = samos_boot_info_ptr->acpi_rsdp;
    size_t   total = 0;
    for(size_t i = 0; i < total_entries; i++){
        struct e820_entry* r = &entries[i];
        if(r->type != SAMOS_E820_TYPE_RECLAIMABLE){
            continue;
        }
        if(rsdp && rsdp >= r->base_addr && rsdp < r->base_addr + r->length){
            continue;
        }
        r->type = SAMOS_E820_TYPE_USABLE;
        total += r->length;
    }
    return total;
}

struct e820_entry* e820_entry(size_t index){
    if(index >= e820_total_entries()){
        return NULL;
    }
    struct e820_entry* entries = E820_ENTRIES;
    return &entries[index];
}

size_t e820_total_accessible_memory(void){
    size_t total_entries        = *E820_COUNT;
    struct e820_entry* entries  = E820_ENTRIES;
    size_t total = 0;
    for(size_t i = 0; i < total_entries; i++){
        if(entries[i].type == 1){ // usable
            total += entries[i].length;
        }
    }
    return total;
}

struct e820_entry* e820_largest_free_entry(void){
    size_t total_entries        = *E820_COUNT;
    struct e820_entry* entries  = E820_ENTRIES;
    struct e820_entry* chosen   = NULL;
    for(size_t i = 0; i < total_entries; i++){
        if(entries[i].type != 1){
            continue;
        }
        if(!chosen || entries[i].length > chosen->length){
            chosen = &entries[i];
        }
    }
    return chosen;
}

void* memset(void* ptr, int c, size_t size){
    char* c_ptr = (char*)ptr;
    for(size_t i = 0; i < size; i++){
        c_ptr[i] = (char)c;
    }
    return ptr;
}

int memcmp(void* s1, void* s2, int count){
    char* c1 = s1;
    char* c2 = s2;
    while(count-- > 0){
        if(*c1++ != *c2++){
            return c1[-1] < c2[-1] ? -1 : 1;
        }
    }
    return 0;
}

void* memcpy(void* dest, void* src, int len){
    char* d = dest;
    char* s = src;
    while(len--){
        *d++ = *s++;
    }
    return dest;
}
