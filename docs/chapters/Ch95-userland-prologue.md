# Ch 95 - User-space transition prologue (book Ch 95+96+97)

Three book chapters folded into one commit:

- **Ch 95** - the asm + C plumbing that pushes the CPU into ring 3.
- **Ch 96** - the first user program (`blank.bin`).
- **Ch 97** - a two-line bug fix in `task.asm` (we ship the fix
  directly rather than commit-then-fix; see "Book bugs absorbed").

## What got added

### Userland transition asm (`src/task/task.asm`)

- `user_registers` - reload DS/ES/FS/GS with `0x23` (user data
  selector | RPL=3).
- `restore_general_purpose_registers(struct registers* regs)` - load
  edi/esi/ebp/ebx/edx/ecx/eax (and ebp via `pop ebp`) from the
  passed struct. The Ch 97 fix pops into `ebp` not `esp` here; we
  ship the correct version.
- `task_return(struct registers* regs)` - the iret-into-userland
  trick. Pushes SS / ESP / EFLAGS (with IF set so interrupts come
  back on in ring 3) / CS / EIP in the order the CPU expects for a
  ring-3 trap return, loads user data selectors into the segment
  registers, calls `restore_general_purpose_registers`, then
  `iretd`. The Ch 97 fix uses `[ebp+4]` instead of `[ebx+4]` for the
  final argument push; we ship the correct version.

### Task switch C code (`src/task/task.{h,c}`)

- `task_switch(task)` - set `current_task = task`, then
  `paging_switch` to its directory. After this, every memory access
  in ring 0 goes through the task's address space.
- `task_run_first_ever_task()` - panic if no current task exists,
  `task_switch(task_head)`, `task_return(&task_head->registers)`.
  Doesn't return.
- `task_page()` - `user_registers()` + `task_switch(current_task)`.
  This is the "go back to the current task's address space and ring
  context" helper for the syscall return path that lands later.

### First user program (`programs/blank/`)

- `blank.asm` - the minimal user program: `_start: jmp $`. Two bytes
  long (`EB FE`). Does nothing forever.
- `linker.ld` - tells `i686-elf-ld` that the binary's base is
  `0x400000` (matching `SAMOS_PROGRAM_VIRTUAL_ADDRESS`), and lays
  out `.text/.rodata/.data/.bss/.asm` each page-aligned. Output
  format `binary` so the result is raw bytes with no ELF wrapper.
- Per-program `Makefile` - `nasm -f elf` then `i686-elf-gcc -T
  linker.ld -o blank.bin` with the freestanding flags.

### Root `Makefile`

- New `user_programs` target that recurses into
  `programs/blank/`. `all` now depends on it.
- `clean` depends on `user_programs_clean`.
- After mformat'ing the FAT16 volume, the build runs
  `mcopy -i bin/os.bin programs/blank/blank.bin ::/blank.bin` so
  the user program is reachable from the kernel via FAT16. Two
  files now live in the volume: `/hello.txt` and `/blank.bin`.

## Book bugs absorbed inline

Per project convention we ship book bugs and fix them with a
follow-up `gxx` commit. Ch 97 in the book *is* such a follow-up,
fixing two Ch 95 typos. Rather than committing the broken
intermediate and then a g-fix, we go straight to the corrected
asm and call out both lines in the file header comment:

- `restore_general_purpose_registers` - book originally ended with
  `pop esp`, which would overwrite the kernel stack pointer with
  the saved base pointer and immediately go off the rails. Correct
  is `pop ebp`.
- `task_return` - book originally did `push dword [ebx+4]` for the
  argument to `restore_general_purpose_registers`. `ebx+4` is the
  `esi` field of the registers struct, not the struct pointer
  itself. Correct is `push dword [ebp+4]` (the original argument).

Both fixes are needed for the kernel to even link cleanly with the
asm symbols referenced from C; without them the boot would fault
the first time `task_return` ran.

## Test coverage

`tests/36-userland-prologue.sh` is a static-only check:

1. `mdir -i bin/os.bin ::/blank.bin` confirms `/blank.bin` is in
   the FAT16 volume.
2. `i686-elf-nm build/kernelfull.o` confirms the four globally-
   visible symbols (`task_return`, `user_registers`,
   `restore_general_purpose_registers`, `task_run_first_ever_task`)
   are linked into the kernel.
3. The first two bytes of `programs/blank/blank.bin` are `EB FE`
   (the assembled `jmp $`). If the linker script ever stops
   producing a raw binary, or the asm grew unexpectedly, the entry
   point would shift.

The test sets `PATH=$HOME/opt/cross/bin:$PATH` so `i686-elf-nm` is
findable; the rest of the test suite already inherits the
cross-compiler PATH from `build.sh`.

End-to-end execution of `blank.bin` (we'd see the kernel actually
enter ring 3, then loop forever) waits for the chapter that calls
`task_run_first_ever_task` from `kernel_main`. Today nothing does;
the prologue is loaded into a dormant code path.

## Counter at 28

Test count is now 28; all green.
