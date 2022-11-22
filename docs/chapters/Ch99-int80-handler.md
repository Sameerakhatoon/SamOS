# Ch 99 - int 0x80 handler wiring (book Ch 102)

Book Ch 102: stand up the kernel-side plumbing for the syscall
mechanism. Wiring only - no commands implemented yet, no IDT[0x80]
gate bound, so user code that triggers `int 0x80` today would still
hit the `no_interrupt` placeholder. The next chapter (Ch 103+) will
register isr80h_wrapper at vector 0x80 and start adding commands.

## What got added

### `src/kernel.asm`

- New `kernel_registers` (mirror of Ch 95's `user_registers`): load
  DS/ES/FS/GS with the kernel data selector (`0x10`). Reset segment
  regs back to ring 0 context at the top of every syscall handler.
- Added `global kernel_registers` at the file head so the linker
  exports the symbol.

### `src/kernel.c`

- `kernel_page()`: calls `kernel_registers()` then `paging_switch(
  kernel_chunk)`. The "go back to kernel address space and selectors"
  helper that the syscall entry uses before touching kernel data.

### `src/kernel.h`

- Prototypes `void kernel_page();` and `void kernel_registers();`.

### `src/idt/idt.h`

- New `struct interrupt_frame` (packed) - layout matches what `pushad`
  + the CPU's automatic interrupt frame produce on the stack:
  ```
  uint32_t edi, esi, ebp, reserved, ebx, edx, ecx, eax;  // pushad
  uint32_t ip, cs, flags, esp, ss;                       // CPU-pushed
  ```
  The `reserved` slot is where `pushad` stores the pre-push ESP,
  which we don't care about.

### `src/task/task.h`

- Forward decl `struct interrupt_frame;` so task.c can include it
  before pulling in `idt.h`.
- Prototype `void task_current_save_state(struct interrupt_frame*);`.

### `src/task/task.c`

- Includes `idt/idt.h` for the new struct.
- `task_save_state(task, frame)` copies ip/cs/flags/esp/ss + all
  general-purpose registers out of the interrupt frame into the
  task's saved-registers area. This is what lets us pause a task,
  do kernel work, and later resume exactly where it was.
- `task_current_save_state(frame)` is the convenience entry point
  the syscall handler calls; panics if no current task exists.

### `src/idt/idt.c`

- Includes `task/task.h`.
- `isr80h_handle_command(int command, struct interrupt_frame*)` -
  stub returning 0 for now. Ch 103+ adds the dispatch table.
- `isr80h_handler(int command, struct interrupt_frame* frame)` -
  the C entry point the asm wrapper calls:
  1. `kernel_page()` to switch CR3 + segment regs back to kernel.
  2. `task_current_save_state(frame)` so the kernel can recover
     the user task's full state if it needs to.
  3. Dispatch via `isr80h_handle_command`.
  4. `task_page()` to switch CR3 + segment regs back to the user
     task before returning.

### `src/idt/idt.asm`

- New `extern isr80h_handler` and `global isr80h_wrapper`.
- New `isr80h_wrapper`:
  - `pushad` - completes the interrupt_frame layout on the kernel
    stack.
  - `push esp` - pass the frame pointer to the C handler.
  - `push eax` - pass the user-side EAX (the command number) as the
    first C argument.
  - `call isr80h_handler` then stash the return value in
    `tmp_res` (because `popad` would overwrite it).
  - `add esp, 8` to drop the two pushed args.
  - `popad` to restore the user GPRs.
  - Reload EAX from `tmp_res` so the syscall return value reaches
    user space.
  - `iretd`.
- New `.data tmp_res: dd 0` for the return-value stash.
- Dropped the `cli`/`sti` pair around `int21h` and `no_interrupt`.
  With the IDT gates set up as interrupt gates (`type_attr = 0xEE`,
  type=1110), the CPU already clears IF on entry and the iret pops
  it back, so the manual disable/enable was redundant. Required by
  the book before the syscall path lands.

## What hasn't changed

- IDT[0x80] is still `no_interrupt`. Until Ch 103 wires
  `idt_set(0x80, isr80h_wrapper)`, the new plumbing is dormant.
- No new test - the path isn't reachable from any caller yet.
  All 29 existing tests still pass, which is the bar: nothing
  upstream was broken by adding the wiring.

## Why the wiring lands before the IDT entry

The asm wrapper, the C handler, the task state-save helpers, and
the `kernel_page`/`kernel_registers` segment-reload helpers are all
prerequisites for the IDT entry to do anything safe. Landing them
in one chapter keeps the next commit small and focused on "register
this vector with the IDT" rather than mixing it with a hundred lines
of supporting code.
