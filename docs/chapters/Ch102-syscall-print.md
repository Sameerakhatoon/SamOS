# Ch 102 - Print syscall (book Ch 108)

Book Ch 108: command 1 = print. User pushes a `char*` onto its
stack, fires `int 0x80` with EAX=1, kernel pulls the pointer, copies
the string out of user-space, prints it to the VGA buffer.

## What got added

- `src/isr80h/isr80h.h` - `SYSTEM_COMMAND1_PRINT = 1` joins the enum.
- `src/isr80h/io.h` + `src/isr80h/io.c`:
  - `isr80h_command1_print(frame)` - reads the user's pushed
    pointer via `task_get_stack_item(0)`, copies the string out
    with `copy_string_from_task` into a 1 KiB stack buffer, hands
    it to the kernel's `print()`.
- `src/isr80h/isr80h.c` registers the new command alongside command 0.
- `Makefile` - new `./build/isr80h/io.o` target.
- `programs/blank/blank.asm` - upgraded again:
  ```asm
  push message
  mov eax, 1            ; SYSTEM_COMMAND1_PRINT
  int 0x80
  add esp, 4

  push 20
  push 30
  mov eax, 0            ; SYSTEM_COMMAND0_SUM (Ch 107 sum still in)
  int 0x80              ; -> EAX = 50
  add esp, 8
  jmp $

  section .data
  message: db 'I can talk with the kernel!', 0
  ```
  Two syscalls per boot: first the print, then the sum, then idle.
- `programs/blank/linker.ld` and `src/linker.ld` - move `.asm`
  above `.rodata` so the `.asm` section starts immediately after
  `.text`. Otherwise nasm-only programs put their executable code
  *after* read-only constants, which the page-aligned base-0x400000
  layout doesn't tolerate.

## End-to-end proof

`tests/39-syscall-print.sh` boots the kernel, lets the user program
run, dumps VGA, and greps for the literal string
`I can talk with the kernel!`. The string only reaches the screen
if:

- the user code can find `message` (linker put `.asm` above `.data`
  so the absolute address baked into `push message` resolves
  correctly with the binary base at `0x400000`),
- the syscall wrapper hands EAX=1 to `isr80h_handler`,
- `task_get_stack_item(0)` reads the right slot,
- `copy_string_from_task` walks the user page directory, copies the
  bytes through the scratch portal, and restores the PTE without
  corrupting it,
- the kernel's `print()` is still functional after the page-table
  swap dance.

`tests/38-syscall-roundtrip.sh` was relaxed to require `EAX = 0x32`
and `EIP` somewhere in the binary, since the trailing `jmp $`
offset now shifts with every program edit.

`tests/36-userland-prologue.sh` was relaxed to grep for `EB FE`
anywhere in blank.bin instead of asserting an exact opcode
prefix. The new `.data` section means strict tail-byte checks no
longer line up.

## Counter at 31

Test count now 31. The kernel can now run a user program that
*talks*. Next chapters add the keyboard half so the user can also
listen.
