# Lectures 34 + 35 - sector load count + 64-bit pushad/popad

**Source commits (PeachOS64BitCourse):** `1dc2761` (L34), `068bdad` (L35)
**SamOs commit:** L35 on `module1-64bit` branch
**Regression test:** `tests64/L35-idt-pushad-macros.sh`

## L34 was already done

PeachOS64's L34 bumps boot.asm's `mov ecx, 100` (sector load
count) to 250 so kernel.bin has room for the upcoming IDT and
process subsystems. SamOs already bumped to 250 at L12 when the
heap.c L10 fix and friends pushed kernel.bin past 51 KiB.
Nothing to do here; we skip the commit. This doc notes the
deviation.

## L35 - pushad / popad macros

x86_64 dropped the 1-byte `pushad` / `popad` instructions. The
existing 32-bit idt.asm depends on them in three places:

- `no_interrupt` (the catch-all unused-vector handler)
- The per-vector `interrupt` macro that generates int0..int511
- `isr80h_wrapper` (the syscall entry stub)

L35 defines a pair of NASM macros that emulate the 32-bit
behaviour at 64-bit width.

## Theory primer: what pushad does

32-bit `pushad` pushes 8 regs in a fixed order:

```
eax, ecx, edx, ebx, esp, ebp, esi, edi
```

with one quirk: the `esp` slot holds the VALUE OF ESP BEFORE
pushad started (i.e. the "outer" stack pointer). On `popad`,
the third pop from the top discards that saved esp because
restoring esp from a stack value would be circular. After popad
the cpu's esp is back to its pre-pushad value naturally (we
just popped 8 regs).

## Our macros

```nasm
temp_rsp_storage: dq 0x00

%macro pushad_macro 0
    mov qword [temp_rsp_storage], rsp
    push rax
    push rcx
    push rdx
    push rbx
    push qword [temp_rsp_storage]   ; saved rsp slot
    push rbp
    push rsi
    push rdi
%endmacro

%macro popad_macro 0
    pop rdi
    pop rsi
    pop rbp
    pop qword [temp_rsp_storage]    ; discard pushed rsp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    mov rsp, [temp_rsp_storage]
%endmacro
```

`temp_rsp_storage` is a static .data slot, NOT a stack
variable, so it lives across the macro boundary. The push of
`[temp_rsp_storage]` after the rbx push gives the C handler an
rsp slot at the same position the 32-bit pushad gave it - the
interrupt_frame struct layout stays interpretable from C.

popad reverses the push order and discards the pushed rsp into
`temp_rsp_storage` (overwriting the value we set in pushad - we
no longer need it because the final `mov rsp, [temp_rsp_storage]`
isn't really restoring anything useful here since the pops
already advanced rsp back).

Wait, that's an issue: by the time we reach `mov rsp,
[temp_rsp_storage]`, the pops have advanced rsp PAST the saved
rsp slot. The mov re-sets rsp to whatever the LAST overwrite
of `temp_rsp_storage` was. Since the third pop wrote to it,
rsp is set to the SAVED rsp from pushad time. That ends up at
the right place, BUT only because pushad pushed exactly 8 8-byte
slots which is what the pops consumed. So the final mov is
redundant in the well-formed case, defensive in the buggy case.
Upstream keeps it; we follow.

(Aside: a cleaner implementation would skip the
[temp_rsp_storage] dance entirely and just `lea rsp, [rsp + 64]`
after the 8 pops, or just leave the stack as-is after pop-rdi.
Upstream's version is functionally equivalent and matches the
32-bit layout, so we mirror it.)

## Why idt.asm is NOT in the build yet

idt.asm is still mostly 32-bit even after L35: `push esp` (not
rsp), `iret` (not iretq), the `interrupt_pointer_table` uses
`dd` (4-byte entries) instead of `dq`. L36 finishes the
migration. We keep idt.asm out of the Makefile FILES list
until that's done.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/idt/idt.asm` | adds `temp_rsp_storage` + `pushad_macro` / `popad_macro` NASM macros. Replaces every `pushad` / `popad` with the macro version (3 sites). Rest of file untouched - still 32-bit. |

## How we verified

VGA after L35:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
```

Same tokens as L32 - L33. idt.asm is not wired into the build,
so this is purely a "tree still builds" check.

## Forward look

L36 finishes idt.asm (`push esp -> push rsp`, `iret -> iretq`,
`dd -> dq` in the pointer table). L37 widens idt.c to match.
L38 puts the IDT back in the build and exercises it.
