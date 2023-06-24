#include "memory.h"
#include "config.h"

// Lecture 18 - E820 readers. boot.asm puts the entries at
// SAMOS_MEMORY_MAP_LOCATION (0x7E00) and the count at
// SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION (0x7DFE - reused boot
// signature). Both readers are read-only and safe to call before
// kheap_init; after kheap_init runs they return garbage because
// the heap bitmap lives at the same address.

// Lecture 20 - typed helpers for multiheap to iterate E820.

size_t e820_total_entries(void){
    return *((uint16_t*)SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION);
}

struct e820_entry* e820_entry(size_t index){
    if(index >= e820_total_entries()){
        return NULL;
    }
    struct e820_entry* entries = (struct e820_entry*)SAMOS_MEMORY_MAP_LOCATION;
    return &entries[index];
}

size_t e820_total_accessible_memory(void){
    size_t total_entries        = *((uint16_t*)SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION);
    struct e820_entry* entries  = (struct e820_entry*)SAMOS_MEMORY_MAP_LOCATION;
    size_t total = 0;
    for(size_t i = 0; i < total_entries; i++){
        if(entries[i].type == 1){ // usable
            total += entries[i].length;
        }
    }
    return total;
}

struct e820_entry* e820_largest_free_entry(void){
    size_t total_entries        = *((uint16_t*)SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION);
    struct e820_entry* entries  = (struct e820_entry*)SAMOS_MEMORY_MAP_LOCATION;
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
    for(int i = 0; i < size; i++){
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
