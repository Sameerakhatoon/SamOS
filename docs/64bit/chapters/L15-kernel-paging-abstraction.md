# Lecture 15 - abstracting out kernel paging functionality

**Source commit (PeachOS64BitCourse):** `f02adfe`
**SamOs commit:** L15 on `module1-64bit` branch
**Regression test:** `tests64/L15-kernel-page.sh`

## Why this chapter exists

L13 proved the C-side mapper works by building a one-shot
`struct paging_desc*` inside `kernel_main` and switching to it.
That descriptor is the kernel's address space, but L13 stored it
in a local variable - inaccessible once `kernel_main` falls into
its infinite loop. Future code (fault handlers, the future
scheduler, a soon-to-arrive user-mode return path) needs a stable
way to say "load the kernel's address space and the kernel's
segment registers". L15 introduces that abstraction:

1. `kernel_paging_desc` - a file-scope global owning the kernel's
   PML4.
2. `kernel_registers()` - asm helper that reloads ds/es/fs/gs with
   the long-mode kernel data selector.
3. `kernel_page()` - C wrapper that calls
   `kernel_registers()` then `paging_switch(kernel_paging_desc)`.
4. `panic(msg)` - tiny halt-with-message helper for places where
   recovery is impossible.

After L15, anywhere in the kernel that has lost track of which
address space is live (returning from a user task, post-#PF
handler, etc.) can just call `kernel_page()` and be back on safe
ground.

## Theory primer

### Why reload the segment registers at all?

In long mode the **base/limit** fields of data segment descriptors
are ignored - the linear address is the virtual address straight
through. So why bother reloading ds/es/fs/gs?

Two reasons:

1. **DPL gating.** The descriptor in the GDT still carries a
   privilege level. If we drop to user mode (DPL=3), the CPU
   loads user data selectors into the segregs. When we come back
   to kernel mode (e.g. via an interrupt) the CS gets reloaded by
   the gate descriptor, but ds/es/fs/gs keep whatever the user
   set them to. Touching memory through a user selector while
   we're in CPL=0 still works (and that's the danger - a
   compromised user-supplied selector index could point at a
   present-but-bogus descriptor and #GP later in a hard-to-debug
   spot). The defensive move is: on every kernel entry, reload
   ds/es/fs/gs with our known-good kernel data selector.

2. **FS/GS hooks.** Later in the course we'll use GS as the
   per-CPU base (swapgs + IA32_KERNEL_GS_BASE). `kernel_registers`
   resets GS to a sane selector so the swapgs dance starts from a
   known state.

### Why SS is left untouched

SamOs follows PeachOS64 here: `kernel_registers` reloads ds/es/fs/gs
but NOT ss. Three reasons:

1. **CPL invariant.** In long mode CS.RPL == SS.RPL is enforced.
   When `kernel_registers` runs, CS already encodes the kernel CPL,
   so SS must too. If our caller's SS is wrong we have bigger
   problems than a missing mov - we'd already have #GP'd on the
   stack pointer.
2. **`mov ss, ax` is a CPU oddity.** It implicitly inhibits
   interrupts for one instruction. Doing it from a normal C call
   site (not an interrupt entry) gains nothing.
3. **The kernel entry path is the right place** to fix SS. When we
   build the IDT later, the gate descriptor will load CS+SS
   atomically from the TSS for us.

So `kernel_registers`'s job is strictly the "soft" segregs that
the user can dirty.

### Why kernel_page = registers + paging_switch in that order

`kernel_page()` could plausibly do paging_switch first then
kernel_registers, or vice versa. PeachOS64's choice (and ours):

```c
void kernel_page() {
    kernel_registers();
    paging_switch(kernel_paging_desc);
}
```

`kernel_registers` reloads selectors from the GDT, which lives at a
fixed physical address that BOTH the inbound (user) and outbound
(kernel) page tables map. Either order works here, but the
inbound order is safer in the general case: if a future
descriptor switch happens to unmap the GDT mid-flight, having
already cached the kernel data selectors into the segreg shadow
registers means subsequent loads use the shadow values rather
than re-walking the now-unmapped GDT.

In long mode "selectors are loaded once, shadow values stick" is
the practical invariant - so doing the segreg reload while we
KNOW the inbound map is valid is the conservative choice.

### `panic` semantics

L15's `panic` is the simplest possible version:

```c
void panic(const char* msg) { print(msg); while(1) {} }
```

No stack dump, no register dump, no machine state save. The point
is just: when an invariant breaks, print why and stop. Later
chapters will grow it into a proper crash handler.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/kernel.asm` | adds `global kernel_registers` and a 64-bit `kernel_registers` function in the `[BITS 64]` section. Reloads ax = LONG_MODE_DATA_SEG into ds/es/fs/gs, leaves ss alone, ret. |
| `src/kernel.c` | adds `panic`, `kernel_paging_desc` (file-scope, init 0), `kernel_page`. `kernel_main` rewrites the L13 local `desc` to use the global `kernel_paging_desc`. After the first `paging_switch + data[0]='M' + print`, calls `kernel_page() + data[0]='K' + print` to exercise the new abstraction. |
| `src/kernel.h` | already had the L15 prototypes (anticipated earlier). No change needed. |

## How we verified

`tests64/L15-kernel-page.sh` asserts four VGA tokens:

| Token | Proves |
|---|---|
| `Hello 64-bit!` | banner, kernel.asm reached kernel_main |
| `ABC` | kheap survived |
| `MBC` | first paging_switch (kernel_paging_desc just built) worked |
| `KBC` | kernel_page() (kernel_registers + paging_switch) survived |

Observed on VGA:

```
Hello 64-bit!
ABCMBCKBC
```

All four tokens present. `KBC` is the new one for L15 - it proves
that `kernel_registers()` does not corrupt segreg state (no #GP)
and that calling `paging_switch(kernel_paging_desc)` while
already on `kernel_paging_desc` is the no-op we expect.

## Forward look

L16 lets the kernel heap grow dynamically (currently it's a fixed
~100 MiB region declared by macros). After that, L18 builds the
E820 memory map probe so the kernel knows which physical regions
are actually usable. The next several lectures (L20..L30) build a
multi-heap on top.
