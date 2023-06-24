# Lecture 18 - boot-time BIOS E820 memory map probe

**Source commit (PeachOS64BitCourse):** `5b3ee75`
**SamOs commit:** L18 on `module1-64bit` branch
**Regression test:** `tests64/L18-e820.sh`

## Why this chapter exists

So far the kernel has been pretending physical memory is a flat
region we can do anything with. That's good enough for one heap,
but the multi-heap work coming up (L20+) needs to know which
physical regions are actually usable RAM vs. reserved for the
BIOS, ROM, MMIO, etc. The de-facto answer on PC-class systems is
BIOS int 0x15 / function 0xE820 - the "Query System Address
Map" call.

L18 wires that up:

1. `boot.asm` makes the E820 BIOS call in real mode and dumps
   the entries at a fixed physical address.
2. `memory.c` adds two C readers that interpret the dump.
3. `kernel.c` prints the total usable bytes as a smoke test.

## Theory primer

### Why E820 must run in real mode

`int 0x15` is a BIOS interrupt. BIOS code lives in real mode and
expects 16-bit segment:offset addressing. By the time we reach
`load32` in our boot.asm we have set CR0.PE=1 and far-jumped to a
32-bit code segment - real-mode `int 0x15` no longer works
there (the IVT is gone, the interrupt would go through the IDT
which we have not loaded). So the call has to live BEFORE
`.load_protected`.

That's the structural reason boot.asm grows a real-mode helper
above the `[BITS 32]` divider.

### The E820 ABI

For each entry the BIOS expects:

| Register | Meaning |
|---|---|
| `EAX` | function = 0xE820 |
| `EDX` | magic = ASCII `'SMAP'` = `0x534D4150` |
| `EBX` | continuation token (caller passes 0 on first call, BIOS updates) |
| `ECX` | entry size in bytes (24 for the legacy layout, 20 if BIOS is ancient, 24+ for newer ACPI 3.0 BIOSes that add a 4-byte attribute) |
| `ES:DI` | pointer to a 24-byte buffer |

On return:

| Register | Meaning |
|---|---|
| `EAX` | should equal `'SMAP'` on success; anything else = bail |
| `ECX` | number of bytes the BIOS actually wrote |
| `EBX` | continuation token; 0 means this was the last entry |
| `CF` | set on error or end of list |

So the loop is:

```nasm
xor bx, bx                ; first call - no continuation token yet
.next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc  .done
    cmp eax, 0x534D4150
    jne .done
    inc word [count]
    add di, cx
    test bx, bx           ; EBX == 0 -> last entry
    jnz .next
.done:
```

The `o32` prefix on a few of those movs is NASM's way of forcing
32-bit operand size in a 16-bit segment - because the magic
numbers (`0xE820`, `'SMAP'`) do not fit in a 16-bit register.

### The entry layout

```c
struct e820_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;            // 1 = usable, 2 = reserved, 3 = ACPI reclaim,
                              // 4 = ACPI NVS, 5 = bad memory
    uint32_t extended_attr;   // ACPI 3.0+: bit 0 = "valid", bit 1 = "non-volatile"
} __attribute__((packed));
```

We treat `type == 1` as the only usable category. ACPI reclaim
(3) is technically usable after the OS reads ACPI tables, but we
don't yet, so we conservatively skip it.

The packed attribute matters: without it the C compiler is
allowed to insert padding between fields (e.g. to align the
trailing uint32_t on an 8-byte boundary), which would walk the
buffer wrong by 4 bytes per entry.

### Where do we put the dump?

PeachOS64 (and SamOs after this lecture):

- Entries: `0x7E00` (`SAMOS_MEMORY_MAP_LOCATION`)
- Count: `0x7DFE` (`SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION`)

`0x7E00` is the byte right after the boot sector (the BIOS loads
the 512-byte MBR at `0x7C00..0x7DFF`). `0x7DFE` is the last two
bytes of the boot sector - the very location of the `0xAA55`
boot signature.

Why overwrite the boot signature? Because by the time
`load_memory_map` runs the BIOS has already validated the
signature and handed control to us. The two bytes are dead
weight at runtime, so we repurpose them as the entry count.

This is the same pattern PeachOS / PeachOS64 use. It is
clever-bordering-on-cursed but completely safe.

### Conflict with `SAMOS_HEAP_TABLE_ADDRESS`

SamOs's `SAMOS_HEAP_TABLE_ADDRESS` is also `0x7E00`. So the heap
bitmap and the E820 dump share the same address. Order of
operations in `kernel_main`:

```c
print(e820_total_accessible_memory());   // reads 0x7E00 - still E820
kheap_init(SAMOS_HEAP_SIZE_BYTES);       // writes 0x7E00 - now heap bitmap
```

After `kheap_init` runs, `e820_*` returns garbage. We work
around it by reading once, before init.

A future lecture (probably around L20 multi-heap setup) will
need to copy the entries somewhere else before init, since the
multi-heap orchestrator will want to enumerate them after the
heap is up. For L18 the read-once pattern is sufficient.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/boot/boot.asm` | A20 enable moved up into real-mode `step2`. New `load_memory_map` helper between the GDT and the `[BITS 32]` divider. Last two bytes of the boot sector get the label `total_memory_map_entries` sitting on top of the 0xAA55 word. |
| `src/config.h` | adds `SAMOS_MEMORY_MAP_LOCATION` and `SAMOS_MEMORY_MAP_TOTAL_ENTRIES_LOCATION`. |
| `src/memory/memory.h` | adds `struct e820_entry` (packed), `e820_total_accessible_memory`, `e820_largest_free_entry`. |
| `src/memory/memory.c` | implements both. Linear scan over the entries; only `type == 1` counted. |
| `src/kernel.c` | prints `e820 total: <bytes>` right after the banner, BEFORE `kheap_init`. |

## How we verified

Boot under QEMU `-m 256`:

```
Hello 64-bit!
e820 total: 267910144
ABCMBCKBC
heap size: 104857600
heap used: 839680
heap free: 104017920
```

`267910144` = `255.49 MiB`. QEMU passes us a clean E820 with
roughly `256 MiB - 513 KiB` of usable RAM, the rest reserved at
the bottom of memory for BIOS/EBDA/ROM. The exact value depends
on QEMU's firmware (SeaBIOS); the test asserts a sanity band of
`100 MiB < total < 256 MiB`.

L13/L15/L16 regression tokens all still present.

## Forward look

L19 in upstream is a placeholder. L20..L30 builds the multi-heap
on top of E820, using `e820_largest_free_entry` (currently
unused but ready) to pick the best region for each sub-heap.
Before that, we'll likely need to copy the E820 dump out of the
shared 0x7E00 region so kheap_init does not clobber it.
