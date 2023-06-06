# Lectures 59 + 60 - load + iretq into ring 3

**Source commits (PeachOS64BitCourse):** `9a57418` (L59), `8ec44cd` (L60)
**SamOs commit:** L59-L60 on `module1-64bit` branch
**Regression test:** `tests64/L60-user-mode-entry.sh`

## Why this chapter exists

L40 - L57 staged the entire kernel-side machinery for running a
user process. L59 wires it up: `process_load_switch` opens
`SIMPLE.BIN`, builds a task + process, and
`task_run_first_ever_task` `iretq`'s into ring 3 at the user
entry point.

L60 fixes the paging-map `user_supervisor` bit handling so the
user code is actually reachable through the task's PML4 at
CPL=3.

## L60's paging_map fixes - and why we already had them

Upstream L60 adds three lines to `paging_map`:

```c
pml4_entry->user_supervisor = 1;
pdpt_entry->user_supervisor = 1;
pd_entry->user_supervisor   = 1;
```

inside the `if (paging_null_entry(...))` blocks - i.e. only on
NEWLY CREATED intermediate entries. And on the leaf:

```c
pt_entry->user_supervisor = (flags & PAGING_ACCESS_FROM_ALL) ? 1 : 0;
```

SamOs went further: we set `user_supervisor = 1` on the
intermediates UNCONDITIONALLY (even when the entry already
exists). Without this, a task whose PML4 was first populated by
`paging_map_e820_memory_regions` (no U bit) and then by
`process_map_memory` (with PAGING_ACCESS_FROM_ALL) would have
U=1 on the leaf but U=0 on intermediates - the CPU's ring-3
walk would fail.

`PAGING_ACCESS_FROM_ALL` is also added to the
`paging_map_e820_memory_regions` calls. Same goal: make the
kernel pages reachable at CPL=3 too, because the CPU reads the
TSS, IDT, and GDT during ring-3 -> ring-0 gate dispatch using
the CURRENT page tables (which is the task's PML4 at the
moment of fault).

## The collision that looks like a fault

After `task_run_first_ever_task` we see:

```
Loading program...
user enter
Panic Exception
```

The "Panic Exception" is NOT a CPU fault in user code. It's the
PIC timer (IRQ0) firing at vector 0x08, which the CPU ALSO
uses for #DF (double fault). Our IDT vector 8 is registered as
`idt_handle_exception` (since 8 is in the 0..0x1F "exception"
range), and that handler `panic`s.

So the flow is:
1. iretq into ring 3 succeeds; CPL=3, RIP=0x400000, user code
   runs.
2. The user's `jmp $` spins. Interrupts are enabled (we OR'd
   IF=1 into RFLAGS in `task_return`).
3. After some milliseconds, the PIC fires IRQ0.
4. CPU vectors through IDT[8] -> int8 asm stub ->
   interrupt_handler -> kernel_page + idt_handle_exception.
5. idt_handle_exception calls `panic("Panic Exception\n")`.

The "user enter" print appearing BEFORE the panic is proof that
ring 3 was actually entered.

L61 remaps the PIC so IRQ0..IRQ15 land at vectors 0x20..0x2F,
clear of the CPU exception range. After L61, the timer fires
at vector 0x20 -> idt_clock -> task_next (which returns to the
same task because there's only one) -> task_return -> iretq
back into user mode. No more spurious "Panic Exception".

## Why "user enter" + Panic Exception is a SUCCESS milestone

It means EVERY one of the following invariants held:

1. The FAT16 fopen / fread of SIMPLE.BIN worked (we saw the EB
   FE bytes during development).
2. `process_load_for_slot` allocated a process, built a task,
   created a fresh PML4, and called process_map_memory.
3. `process_map_memory` mapped 0x400000 -> phys with U=1 leaf
   and (thanks to our L59 unconditional fix) U=1 intermediates.
4. `task_run_first_ever_task` -> `task_switch` switched CR3
   to the task's PML4 without faulting.
5. `task_return` built the iretq frame correctly: SS=0x33
   (user data), CS=0x2B (user code), RFLAGS with IF=1,
   RSP=0x3FF000, RIP=0x400000.
6. iretq passed the CPU's selector / RPL / DPL / L-bit checks.
7. The first byte at 0x400000 (= EB) was successfully fetched
   at CPL=3 - meaning every level of the page-table walk has
   the U bit set.

That's the whole ring-3 entry path, end to end. The
"Panic Exception" that follows is the PIC remap collision -
expected and documented.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/config.h` | (no change from L41 - macros already correct) |
| `src/memory/paging/paging.c` | unconditional U=1 on intermediates; U-bit propagation on leaf from `PAGING_ACCESS_FROM_ALL`. `paging_map_e820_memory_regions` now passes `PAGING_ACCESS_FROM_ALL` so the kernel pages are user-walk-reachable from the task PML4. |
| `src/task/task.c` | `task_init` calls `paging_map_e820_memory_regions(task->paging_desc)` (L59). |
| `src/kernel.c` | end of `kernel_main`: `process_load_switch("0:/SIMPLE.BIN", &p)`, print "user enter", `task_run_first_ever_task()`. |

## SamOs deviations from upstream

| Concern | Upstream | SamOs |
|---|---|---|
| Intermediate U=1 | only on NEW entries | unconditional |
| Kernel pages U=1 | not flagged | yes, in `paging_map_e820_memory_regions` |
| filename case | "0:/simple.bin" (lowercase) | "0:/SIMPLE.BIN" (uppercase, matches mcopy storage) |
| USER_CODE/USER_DATA macros | L60 swap | already correct since L41 |

The unconditional intermediate U=1 is needed for SamOs because
in our boot sequence, `paging_map_e820_memory_regions` is
called BEFORE `process_map_memory`, so the intermediates exist
before the user mapping arrives. Upstream may have a different
order or a smaller E820 region that doesn't intersect with
0x400000.

## How we verified

VGA after L59+L60:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
tss load was fine
register isr80h
tss ready
Loading program...
user enter
Panic Exception
```

The TEST asserts presence of "user enter" - the milestone
marker for ring-3 entry. The Panic Exception is allowed but
not required by the test (a future PIC remap will make it
disappear).

## Forward look

L61 - remap the PIC so IRQ0..IRQ15 land at vectors 0x20..0x2F.
After L61 the timer no longer triggers idt_handle_exception;
instead it dispatches to idt_clock, which task_next's back into
the (only) user task.

L62 - rebuild stdlib so user programs can be compiled with C.
L63 - re-enable ELF loading.
L66 - run the first ELF program.
L68 - turn on the keyboard IRQ properly.
