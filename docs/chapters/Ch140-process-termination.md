# Ch 146 - Process termination

The kernel can now tear a process down cleanly: free its allocations, drop its loaded binary/ELF data, free its stack, free its task, and unlink it from the kernel's process table. Plumbed in but not yet wired to any caller - Ch 147 hooks it into the IDT exception handler so a crashing user program gets terminated instead of triple-faulting the box.

## What landed in `src/task/process.c`

- `process_terminate_allocations(process)` - walks `allocations[]` and calls `process_free(process, ptr)` on each. `process_free`'s ownership guard makes the NULL/already-freed slots no-ops.
- `process_free_binary_data(process)` - `kfree(process->ptr)` for `PROCESS_FILETYPE_BINARY`.
- `process_free_elf_data(process)` - `elf_close(process->elf_file)` for `PROCESS_FILETYPE_ELF`.
- `process_free_program_data(process)` - switch on `filetype`, dispatches to one of the above. `-EINVARG` on unknown type.
- `process_switch_to_any()` - linear scan over `processes[]`, switch to the first non-null. If none: `panic("No processes to switch too\n")` (book typo preserved).
- `process_unlink(process)` (static) - clears the slot in `processes[]`; if the unlinked process was current, call `process_switch_to_any` to pick a successor.
- `process_terminate(process)` - the conductor:
  1. `process_terminate_allocations`
  2. `process_free_program_data`
  3. `kfree(process->stack)`
  4. `task_free(process->task)`
  5. `process_unlink(process)`

`process.h` exports just `process_terminate`; the rest stay file-local except `process_switch_to_any` which is public so the IDT-side exception handler in Ch 147 can use it indirectly through `task_next`.

## Test impact

Suite stays 32/32. Nothing in the kernel calls `process_terminate` yet - Ch 147 wires it to the exception vector so a faulting user program triggers termination instead of a triple fault.
