# Ch 100 - Syscall dispatch (book Ch 103+104)

Book Ch 103 (dispatch infrastructure) + Ch 104 (first command,
sum stub). Bundled because the dispatch table is useless without
a registered command and the IDT gate is useless without something
to dispatch to.

## What got added

### Dispatch infrastructure (Ch 103)

- `src/config.h` - `SAMOS_MAX_ISR80H_COMMANDS = 1024` (size of the
  function-pointer table).
- `src/idt/idt.h`:
  - Function-pointer type
    `typedef void* (*ISR80H_COMMAND)(struct interrupt_frame* frame);`
  - Public `isr80h_register_command(int id, ISR80H_COMMAND command);`.
- `src/idt/idt.c`:
  - `extern void isr80h_wrapper();` so we can take its address.
  - File-scope `static ISR80H_COMMAND isr80h_commands[1024];`.
  - `isr80h_register_command` - bounds-checks `command_id`, panics on
    bound violation or duplicate registration, otherwise stores the
    pointer in the slot.
  - `isr80h_handle_command` is no longer a stub: bounds-checks the
    incoming `command`, looks up the registered function, calls it
    with the interrupt frame, returns whatever it returns.
  - `idt_set(0x80, isr80h_wrapper);` added to `idt_init` - the IDT
    now actually dispatches `int 0x80` into our wrapper.

### First command (Ch 104)

- `src/isr80h/misc.h` + `src/isr80h/misc.c`:
  - `void* isr80h_command0_sum(struct interrupt_frame* frame);` -
    stub that just returns `0`. The real "sum the two integers off
    the user stack" implementation lands once we have user-pointer
    extraction helpers.
- `src/isr80h/isr80h.h` + `src/isr80h/isr80h.c`:
  - `enum SystemCommands { SYSTEM_COMMAND0_SUM };` (will grow).
  - `isr80h_register_commands()` calls
    `isr80h_register_command(SYSTEM_COMMAND0_SUM, isr80h_command0_sum)`.
- `src/kernel.c`:
  - `#include "isr80h/isr80h.h"`.
  - Call `isr80h_register_commands()` from `kernel_main` right after
    `enable_paging()`, before we drop into ring 3.
- `Makefile` + `build.sh` - new `./build/isr80h/{isr80h.o,misc.o}`
  in `FILES`, two compile rules, and a `mkdir -p build/isr80h` in
  build.sh.

## User-program proof

`programs/blank/blank.asm` upgraded from `jmp $` to:

```asm
mov eax, 0
int 0x80
label:
    jmp label
```

The user code fires `int 0x80` with command 0 in EAX, gets back to
ring 3 with EAX = 0 (the sum-stub return value), then enters its
infinite-loop tail. `tests/38-syscall-roundtrip.sh` confirms:

- After 12 s of QEMU run, CPL = 3 and CS = 0x1B (we're in ring 3).
- EIP is in `[0x00400007, 0x00400010]` - past the `int 0x80`
  instruction (which is 5 bytes for `mov eax, 0` + 2 bytes for
  `int 0x80`), so the syscall returned.
- EAX = 0 - the wrapper plumbed the C handler's return value back
  into EAX before iretd.

`tests/36-userland-prologue.sh` retasked to grep for the new
`mov eax, 0; int 0x80` opcode bytes (`b8 00 00 00 00 cd 80`) at the
start of blank.bin instead of the old `eb fe`. The trailing `jmp $`
is still asserted.

## End-to-end path proven

User code → `int 0x80` →
CPU pushes user frame onto TSS-`esp0` kernel stack →
IDT[0x80] dispatches to `isr80h_wrapper` →
`pushad` completes the `interrupt_frame` →
C `isr80h_handler` does `kernel_page()` (swap to kernel CR3 +
selectors), saves task state, looks up command 0, calls
`isr80h_command0_sum` (returns 0), calls `task_page()` (swap back),
returns 0 →
wrapper stashes EAX in `tmp_res`, `popad`s, restores EAX from
`tmp_res`, `iretd`s →
CPU pops user frame, drops to CPL=3 → user code keeps going at the
instruction after `int 0x80` with `EAX = 0`.

Every piece of the syscall mechanism is now exercised by an actual
boot. The remaining chapters add real commands (print, fopen,
fread, fclose, getkey, putchar, malloc, free, exit, ...) on top of
this scaffold.

## Counter at 30

Test count now 30; all green.
