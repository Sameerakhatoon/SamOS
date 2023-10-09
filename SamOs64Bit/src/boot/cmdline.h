#ifndef SAMOS_BOOT_CMDLINE_H
#define SAMOS_BOOT_CMDLINE_H

// boot_info.cmdline lookups for code outside
// kernel.c (test hooks in subsystems). Words are separated by spaces.
// Header only, so no Makefile change.

#include <stdbool.h>
#include <stddef.h>
#include "boot/boot_info.h"
#include "string/string.h"

extern struct samos_boot_info* samos_boot_info_ptr;

// The text after "key=" when the command line has a word starting
// with it, else NULL.
static inline const char* boot_cmdline_value(const char* key){
    const char* start = samos_boot_info_ptr->cmdline;
    int n = strlen(key);
    for(const char* s = start; *s; s++){
        if((s == start || s[-1] == ' ') && strncmp(s, key, n) == 0 && s[n] == '='){
            return s + n + 1;
        }
    }
    return NULL;
}

// True when `word` appears as a whole word.
static inline bool boot_cmdline_has(const char* word){
    const char* start = samos_boot_info_ptr->cmdline;
    int n = strlen(word);
    for(const char* s = start; *s; s++){
        if((s == start || s[-1] == ' ') && strncmp(s, word, n) == 0 && (s[n] == ' ' || s[n] == 0)){
            return true;
        }
    }
    return false;
}

#endif
