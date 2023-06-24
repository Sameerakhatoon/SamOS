# Ch 93 - Task foundations

Book Ch 91. The first chapter that treats "a running thing" as a
first-class object. No userland execution yet; just the types and
table management.

## What got added

- `src/config.h` - five constants the loader / scheduler will use:
  - `SAMOS_PROGRAM_VIRTUAL_ADDRESS = 0x400000` - where every user
    program's `_start` is expected to land in its own address space.
  - `SAMOS_USER_PROGRAM_STACK_SIZE = 16 KiB`.
  - `SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START = 0x3FF000` - top of
    the user stack (stack grows downward toward `..._END`).
  - `SAMOS_PROGRAM_VIRTUAL_STACK_ADDRESS_END = START - SIZE`.
  - `USER_DATA_SEGMENT = 0x23`, `USER_CODE_SEGMENT = 0x1B`. These
    are the GDT byte offsets (0x18 for user code, 0x20 for user
    data) OR'd with RPL=3 so the CPU treats them as ring-3 selectors.
- `src/memory/paging/paging.h`/`paging.c` - new public
  `paging_free_4gb(chunk)`:
  - Iterates `chunk->directory_entry[0..1023]`, masks off the low 12
    bits (the flag bits) to recover each page table's heap pointer,
    `kfree`s each table.
  - Then `kfree`s the directory itself, then `kfree`s the chunk
    struct.
  - This is the destructor counterpart to Ch 51's `paging_new_4gb`;
    we need it so a terminated task can release its address space
    without leaking 4 MiB worth of page tables.
- `src/task/task.h` - two structs and four prototypes:
  - `struct registers` - the 12 user-side register slots we'll need
    to save/restore around context switches: 7 GPRs (edi, esi, ebp,
    ebx, edx, ecx, eax) plus ip, cs, flags, esp, ss.
  - `struct task` - the per-task control block:
    - `page_directory` -> the task's own `struct paging_4gb_chunk`.
    - `registers` -> embedded register save area.
    - `next`/`prev` -> doubly linked list spine for the (eventual)
      scheduler.
  - `task_new`, `task_current`, `task_get_next`, `task_free`.
- `src/task/task.c`:
  - File-static `current_task`, `task_head`, `task_tail`. The list is
    canonical; `current_task` indexes into it.
  - `task_init(task)` (static) - zero the struct, allocate a fresh
    identity-mapped 4 GiB page directory (present+access-from-all,
    *not* writable yet - the program loader will OR in writable per
    page), and pre-fill `registers.ip` with the user program entry,
    `registers.ss` with `USER_DATA_SEGMENT`, `registers.esp` with the
    top of the user stack.
  - `task_new()` - kzalloc, init, splice onto the tail of the list.
    Rolls back via `task_free` if init fails. Returns `ERROR(res)`
    on failure so callers can use `ISERR`.
  - `task_current()` - one-liner accessor.
  - `task_get_next()` - if `current_task->next` is null, return
    `task_head` (wrap-around round-robin).
  - `task_list_remove(task)` (static) - unlink from prev/next, fix
    up head/tail if removing an end, swap `current_task` to the
    next task if removing the running one.
  - `task_free(task)` - `paging_free_4gb` the directory, unlink
    from the list, `kfree` the task struct. Returns 0.
- `Makefile` - `./build/task/task.o` added to `FILES` plus its
  build rule.

## What still doesn't work

- Nothing in `kernel_main` calls `task_new()` yet. We have the
  bookkeeping but no scheduler, no first task, no `iret`-into-ring-3
  trick. Those come in subsequent chapters.
- `task_init` doesn't actually map any code into the new page
  directory. Even if we ran the task, ring 3 would page-fault
  immediately on its first instruction fetch at `0x400000`.
- `task_get_next()` deferences `current_task` without a null check;
  it'll fault if called before any task exists. The book leaves
  this for the scheduler chapter to manage.

## Why no test

Nothing observable changes at boot. The full 27-test suite still
passes (the kernel boots, opens hello.txt, closes it). A proper
task test waits until we can launch a task and either watch its
state evolve or see it triple-fault on a known input.

## Known book bug in this code

`task_list_remove` only updates `task->prev->next` when `prev` is
non-null, but it never updates `task->next->prev` to skip over the
removed node. Removing a middle task leaves the following node
still pointing at the freed one. Will g-commit once we have a
multi-task scheduler exercising it.
