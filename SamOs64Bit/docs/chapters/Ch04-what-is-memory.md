# Ch - What is Memory?

**Book page:** 7 (Part 2 - The Basics)
**Code in this chapter:** none - prose

## The two memory categories

- **Volatile** - needs power. RAM is the canonical example. Working set lives here.
- **Non-volatile** - survives power loss. ROM, flash, HDD, SSD.

## How memory fits into a running program

The OS owns physical memory and hands out regions to programs on request. Allocation decisions consider available space, requested size, and whatever policy the memory manager runs.

## The hierarchy

Fastest, smallest at the top:

1. CPU registers - single-cycle access, tiny (handful of qwords).
2. Cache (L1 → L2 → L3) - nanosecond access, KB to MB.
3. Main memory (RAM) - tens to hundreds of nanoseconds, GB scale.
4. Secondary storage (HDD/SSD) - microsecond+, TB scale.

The kernel cares about every level: registers for context switches, cache for performance, RAM for allocators, and storage for the filesystem.
