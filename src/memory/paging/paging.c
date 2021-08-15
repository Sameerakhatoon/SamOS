// Lecture 12 - paging.c is mostly empty.
//
// The 32-bit paging functions (paging_new_4gb, paging_switch,
// paging_set, paging_get, paging_map_*, paging_align_*,
// paging_get_physical_address, etc.) are gone. Their 64-bit
// replacements land at Lecture 13 and beyond.
//
// The original 32-bit implementations live in main branch history
// (or in the commented block at the bottom of this file in the
// PeachOS64 reference repo) and are kept there as a cross-reference
// for the new design rather than as live code.
//
// At L12 this file is NOT in the Makefile's FILES list - nothing
// links it in. That's fine: it doesn't define any symbols. We
// keep the file so the directory layout stays stable for the L13
// commit.

#include "paging.h"
#include "memory/heap/kheap.h"
#include "status.h"

// Placeholder. L13 fills in the real implementation.
