# Lectures 55 + 56 - load the TSS

**Source commits (PeachOS64BitCourse):** `00c5129` (L55), `2c7d685` (L56)
**SamOs commit:** L55-L56 on `module1-64bit` branch
**Regression test:** `tests64/L56-tss-loaded-isr80h-registered.sh`

## Why this chapter exists

L50/L51 installed the TSS descriptor into GDT slot 7 but
never `ltr`'d it. L55 plugs that gap.

L56 adds two debug prints in kernel_main and corrects the
isr80h cast bugs that 32-bit got away with - we already fixed
the same spots in our L54 port (using `intptr_t` and `uintptr_t`
intermediates), so the L56 isr80h diff is a no-op for SamOs.

## The TSS-visible-now story

The GDTR was loaded at boot with `limit = gdt_end - gdt - 1`.
At L50 we appended two reserved-zero quadwords inside the gdt
block (BEFORE gdt_end). So the lgdt at boot already saw a GDT
limit covering slots 0..8 - 9 entries x 8 bytes = 72 bytes,
limit = 71.

Selector 0x38 = slot 7 (TSS slot lower half).
`ltr 0x38` walks the GDT to slot 7, finds our descriptor with
type=0x89 (available 64-bit TSS), and copies rsp0 + the rest
into the CPU's Task Register state.

After this returns, ring-3 -> ring-0 transitions consult
`tss.rsp0` for the kernel-side stack. Until we actually have a
user task to enter ring 3, the load is dormant - but the
hardware state is correct.

## Why upstream's two prints matter

```c
tss_load(KERNEL_LONG_MODE_TSS_SELECTOR);
print("tss load was fine\n");
```

If `ltr` had #GP'd (selector index out of bounds, descriptor
malformed, NULL selector), the `print` would never run. Seeing
"tss load was fine" is the smoke proof that `ltr` accepted
the selector.

Same logic for "register isr80h" after
`isr80h_register_commands()` - any panic inside the
registration (the L52 commit warned that
`isr80h_register_command` panics on duplicate registrations)
would silence the followup.

## L56's isr80h cast fixes

Upstream changes:
- `isr80h/heap.c`: `(int)` -> `(uintptr_t)` for size_t cast
- `isr80h/io.c`: `(int)` -> `(uintptr_t)` for char cast
- `isr80h/misc.c`: `int` v1/v2 -> `intptr_t` v1/v2

SamOs already did all of these in the L54 port using
`(intptr_t)` consistently. The semantics are equivalent on
two's complement architectures (which is everything we care
about); the diagnostic-suppression effect is the same.

So no source changes in L56 except the two kernel.c print
breadcrumbs.

## How we verified

VGA after L55+L56:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
tss load was fine
register isr80h
tss ready
```

All three "after gate" tokens fire, proving:
- `ltr` survived
- `isr80h_register_commands` survived
- the final "tss ready" still prints

## Forward look

L57 calls additional initializers (probably keyboard_init,
fs_init, disk_search_and_init). L58 - L60 build and load a
simple user program. L61 remaps the PIC. L66 fires the first
user-land ELF.
