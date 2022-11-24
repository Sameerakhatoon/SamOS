# Ch 101 - Reading user-task memory from the kernel (book Ch 105+106+107)

Three book chapters bundled because they together turn the sum
command from "return 0" into a real syscall that reads two integers
off the user stack and returns their sum. Each individual chapter
would commit a half-working state; the user-visible test only flips
green once all three land.

## What got added

### Ch 105 - already in our tree

The book fixes `mov ax, 10` to `mov ax, 0x10` in `kernel.asm`. We
shipped `0x10` from the start of `kernel_registers` (Ch 102), so
this chapter is a no-op for us.

### Ch 106 - reach into user-task memory

- `src/memory/paging/paging.{h,c}`:
  - `paging_get(directory, virt)` - walk a task's page tables and
    return the raw PTE for `virt`. The inverse of `paging_set`.
  - `paging_get_indexes` un-static'd (was file-static for the
    earlier `paging_set` callers); now exposed to other paging
    helpers in the same file.
- `src/task/task.{h,c}`:
  - `task_page_task(task)` - reload user segments + swap CR3 to a
    given task's page directory. The "see memory as this task sees
    it" hook.
  - `task_get_stack_item(task, index)` - swap to the task's page
    directory, peek the 32-bit value at `(uint32_t*)ESP)[index]`,
    swap back to kernel pages. Works for kernel-side reads of
    syscall arguments the user pushed.
  - `copy_string_from_task(task, virtual, phys, max)` - the page-
    table "portal" trick the book describes:
    1. kzalloc a kernel scratch buffer `tmp`.
    2. Save the task PD's existing PTE for `tmp`.
    3. Identity-map `tmp -> tmp` in the task PD too, writable.
    4. Switch CR3 to the task PD. `strncpy(tmp, virtual, max)` now
       reads from the task's user-space `virtual`.
    5. Switch CR3 back to the kernel PD. `tmp` is still mapped
       in the kernel PD (identity), so the kernel can read it.
    6. Restore the task PD's original PTE for `tmp`.
    7. `strncpy(phys, tmp, max)` copies into the caller's buffer.
    8. kfree `tmp`.
    Bounded to `< PAGING_PAGE_SIZE` to keep the single-page
    machinery simple.

### Ch 107 - real sum command

- `src/task/process.c::process_map_memory` - after mapping the
  binary at `0x400000`, also map the user stack from
  `STACK_ADDRESS_END` up to `STACK_ADDRESS_START`. Without this
  the very first user `push` would fault.
- `src/isr80h/misc.c::isr80h_command0_sum` - real body:
  ```c
  int v2 = (int)task_get_stack_item(task_current(), 1);
  int v1 = (int)task_get_stack_item(task_current(), 0);
  return (void*)(v1 + v2);
  ```
  Index 0 is the last-pushed value (30), index 1 is the one before
  (20).
- `programs/blank/blank.asm` - upgraded to push two literal ints
  before invoking the syscall:
  ```asm
  push 20
  push 30
  mov  eax, 0
  int  0x80
  add  esp, 8
  jmp  $
  ```
  Net binary: 16 bytes
  `6a 14 6a 1e b8 00 00 00 00 cd 80 83 c4 08 eb fe`.

## End-to-end proof

`tests/38-syscall-roundtrip.sh` now expects:

- `CPL = 3`, `CS = 0x1B`
- `EIP` in `[0x0040000B, 0x00400010]` - in the trailing
  `add esp, 8 / jmp $` region of blank.bin
- `EAX = 0x00000032` (= 50 = 20 + 30)

`tests/36-userland-prologue.sh` greps for the new 16-byte
blank.bin opcode sequence.

## What this enables

`copy_string_from_task` is the prerequisite for any syscall that
takes a string pointer from user space (print, fopen, etc).
`task_get_stack_item` covers integer and pointer arguments. Together
they're enough to build out the rest of the syscall API in the
following chapters.

## Counter at 30

Still 30 tests green. Test 38 now asserts the full ring-3 → kernel
→ command-dispatch → ring-3 roundtrip with a real return value.
