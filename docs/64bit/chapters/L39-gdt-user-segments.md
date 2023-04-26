# Lecture 39 - user-mode GDT descriptors

**Source commit (PeachOS64BitCourse):** `8cb8d07`
**SamOs commit:** L39 on `module1-64bit` branch
**Regression test:** `tests64/L39-gdt-user-segs.sh`

## Why this chapter exists

The 32-bit kernel had DPL=3 selectors in the GDT for the
ring-3 transition (`USER_CODE_SEGMENT = 0x1B`,
`USER_DATA_SEGMENT = 0x23`). The 64-bit rewrite has not added
those yet - the bring-up GDT only has DPL=0 segments. The
task / process restoration arc (L40+) needs to install
user-mode selectors before it can `iretq` to ring 3, so L39
fills that gap.

Two descriptors land:

| Selector | Type | Access byte | Flags |
|---|---|---|---|
| 0x28 | 64-bit user code | 0xFA (P=1, DPL=11, S=1, E=1, RW=1) | 0x20 (L=1) |
| 0x30 | user data | 0xF2 (P=1, DPL=11, S=1, E=0, RW=1) | 0x00 |

With RPL=3 OR'd in by the loader, the selectors become 0x2B
and 0x33.

## Theory primer: why both DPL=3 AND RPL=3

`mov DS, ax` (and the implicit CS load on a far jump / iretq)
checks two things:

1. The descriptor at `ax >> 3` has DPL >= max(CPL, RPL). For a
   ring-3 process, CPL=3, RPL=3, descriptor DPL=3 -> 3 >= max
   (3, 3) -> OK.
2. The descriptor isn't a NULL.

If we left the selector RPL bits as 0, the CPU would see a
ring-3 process trying to load a ring-0 selector and #GP. So
the calling code OR's 0x3 into the selector index when it
stuffs it into the user task's CS / DS / SS:

```c
// from a later lecture:
task->regs.cs = USER_CODE_SEGMENT;   // 0x2B = (0x28 | 3)
task->regs.ss = USER_DATA_SEGMENT;   // 0x33 = (0x30 | 3)
```

## Why the L bit matters for code only

The `0x20` flags byte on the user code seg sets bit 5 = L
(long mode code segment). For data segs there's no equivalent
- L is "treat this code seg as 64-bit". Data segs have no
instruction-encoding length so the flag is ignored / left 0.

Without L=1 on a code seg, an IRETQ targeting that selector
would put the CPU into 32-bit COMPAT mode under IA-32e - the
user code would suddenly execute as 32-bit ops. The L bit on
the descriptor is the only thing that distinguishes "true 64
bit" from "compat".

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | two new descriptors appended to the `gdt:` block. The `gdt_descriptor` `limit = gdt_end - gdt - 1` is computed by the assembler, so it picks up the new size automatically. |

## How we verified

Same VGA as L38:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
hello
Divide by zero error
```

No code uses the new selectors yet - they sit in the GDT
ready. A malformed descriptor would break `lgdt` only if it
made the table itself parse-invalid (which it doesn't; the
GDT is just a flat array of 8-byte descriptors and the CPU
validates entries when loaded, not at lgdt time). So this
test really confirms "the table still loads cleanly".

## Forward look

L40 - L42 rebuild the task subsystem in 64-bit (struct task,
task_save_state, task_return, etc). L43 adds paging_desc_free
so dying processes don't leak page tables. L44 - L46 finishes
the bring-up. L47 onwards walks through disk, FAT16, keyboard,
isr80h.
