# Lecture 62 - rebuild userland stdlib for 64-bit

**Source commit (PeachOS64BitCourse):** `c7f3e45`
**SamOs commit:** L62 on `module1-64bit` branch
**Regression test:** `tests64/L62-stdlib-64bit.sh`

## Why this chapter exists

Up to L58 the user-program build path used `i686-elf-*`
toolchain because none of the user programs were loaded yet.
L58 brought `simple.bin` (raw 64-bit nasm output) into the disk,
but the C-language user programs (blank, shell) plus the stdlib
were still built as i686 ELF32 binaries.

L62 switches every user-program toolchain invocation to
`x86_64-elf-*` and updates the linker scripts to output ELF64.

## What changed

| File | i686 -> x86_64 |
|---|---|
| `programs/stdlib/Makefile` | i686-elf-gcc -> x86_64-elf-gcc; nasm -f elf -> -f elf64; i686-elf-ld -m elf_i386 -> x86_64-elf-ld -m elf_x86_64 |
| `programs/blank/Makefile` | same compiler swap |
| `programs/shell/Makefile` | same |
| `programs/stdlib/src/samos.asm` | `[BITS 32]` -> `[BITS 64]`. cdecl stack-arg layout `[ebp+8]` rewritten under AMD64 SysV: arg in RDI, push RDI to stack so `task_get_stack_item(0)` finds it kernel-side, syscall id in RAX (was EAX). |
| `programs/stdlib/src/start.asm` | `[BITS 32]` -> `[BITS 64]`. Body unchanged (`call c_start; ret`). |
| `programs/blank/linker.ld` | already `OUTPUT_FORMAT(binary)` - no change |
| `programs/blank/linker-elf.ld` | `elf32-i386` -> `elf64-x86-64` |
| `programs/shell/linker.ld` | `elf32-i386` -> `elf64-x86-64` |
| `Makefile` (root) | user_programs target now `cd stdlib && make; cd blank && make; cd shell && make` (in addition to simple) |

## Theory primer: the syscall wrapper shape

The 32-bit pattern:

```nasm
print:
    push ebp
    mov  ebp, esp
    push dword [ebp+8]   ; the arg (was at [ebp+8])
    mov  eax, 1          ; SYSTEM_COMMAND1_PRINT
    int  0x80
    add  esp, 4
    pop  ebp
    ret
```

becomes:

```nasm
print:
    push qword rdi       ; arg arrives in RDI (SysV); push to stack
                         ; so the kernel's task_get_stack_item(0)
                         ; finds it
    mov  rax, 1
    int  0x80
    add  rsp, 8
    ret
```

The kernel side (`isr80h_handler`) reads the syscall id from RAX
and the arguments via `task_get_stack_item(task_current(), N)` -
which pulls qwords off the user's stack. So even though the
ABI now passes args in registers, the kernel still expects
them on the stack. The wrapper bridges that.

No prologue/epilogue: SysV doesn't require frame pointers, and
keeping the body tight makes the diff readable.

## Why mass rebuild

stdlib.elf, blank.elf, shell.elf are the user programs the
shell will eventually launch. ELF64 is needed because:

- our ELF loader (lands in L63+) will only parse ELF64
- the kernel runs in long mode; ELF32 user code would need a
  compat segment we haven't set up

The compiler swap also means the userland C code gets compiled
with x86_64-elf-gcc's calling conventions - 8-byte alignment,
SysV ABI for any future function calls between user code and
stdlib.

## How we verified

`tests64/L62-stdlib-64bit.sh`:
1. Confirms `file programs/stdlib/stdlib.elf` etc. report
   "ELF 64-bit".
2. Confirms the kernel still boots and reaches "user enter".

Both pass. The kernel's user-program load path still uses
SIMPLE.BIN (the flat binary), so the new ELF binaries are
sitting on disk waiting for the L63+ ELF loader.

## Forward look

L63 - re-enable ELF loading in process_load_elf /
process_map_elf.
L65 - the elfloader's ELF64 refactor (currently it's still
ELF32-only in our tree).
L66 - first ELF user program runs end-to-end.
