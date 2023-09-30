# Lecture 126 - CPUID wrapper

L126 drops a single-instruction CPUID wrapper into
`src/io/cpuid.[ch]`. Used by the L128+ TSC delay path to read
the frequency / brand / hypervisor leaves.

## API

```c
void cpuid(uint32_t leaf, uint32_t subleaf,
           uint32_t* eax, uint32_t* ebx,
           uint32_t* ecx, uint32_t* edx);
```

## Implementation

A single GCC inline asm block:

```c
asm volatile("cpuid"
             : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
             : "a"(leaf), "c"(subleaf));
```

Inputs land in eax / ecx (leaf / subleaf); outputs come back
in all four GPRs. `volatile` keeps the optimiser from hoisting
the serialising instruction.

## Build

`build/io/cpuid.o` joins `FILES` with a recipe matching the
other `io/` objects.

## BIOS test status

Source-only. Test asserts both files exist, the prototype is
in the header, the asm block is in the .c, the Makefile lists
the new object, and the build links.
