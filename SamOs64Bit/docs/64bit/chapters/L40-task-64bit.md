# Lecture 40 - rebuild the task system in 64-bit

**Source commit (PeachOS64BitCourse):** `a827303`
**SamOs commit:** L40 on `module1-64bit` branch
**Regression test:** `tests64/L40-task-source-64bit.sh`

## Why this chapter exists

Tasks are how PeachOS / SamOs models a running user program:
its saved CPU state, its address space, its place in a doubly-
linked list. The 32-bit struct had 32-bit registers and used
`paging_4gb_chunk*` for the address space. Both need to grow
for 64-bit.

L40 - L42 is the task / process rebuild arc; L43 - L44 fills
in the supporting paging APIs; L45 wires everything into the
build.

## Theory primer

### struct registers widening

```c
// before
struct registers {
    uint32_t edi, esi, ebp, ebx, edx, ecx, eax;
    uint32_t ip, cs, flags, esp, ss;
};

// after
struct registers {
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t ip, cs, flags, rsp, ss;
};
```

Mirrors the L37 interrupt_frame widening. `task_save_state`
just copies field-for-field from the frame into the task's
registers struct.

### paging_4gb_chunk -> paging_desc

The 32-bit task created a 4 GiB identity-mapped paging chunk
via `paging_new_4gb`. Long mode needs a 4-level tree; we use
the descriptor API we built in L13. `task_init` now does:

```c
task->paging_desc = paging_desc_new(PAGING_MAP_LEVEL_4);
```

`task_free` calls `paging_desc_free` (a function that lands
in L43; we reference it now so the file is consistent).
`task_switch` calls `paging_switch(task->paging_desc)`. The
old "paging_switch on the directory_entry pointer" form is
gone - the new switch takes the desc directly.

### USER_CODE_SEGMENT / USER_DATA_SEGMENT now 0x33 / 0x2B

L39 added DPL=3 selectors at GDT slots 0x28 and 0x30. L40
updates the config macros to point at those, with RPL=3
OR'd in:

```c
#define USER_DATA_SEGMENT 0x2B   // 0x28 | 3
#define USER_CODE_SEGMENT 0x33   // 0x30 | 3
```

A task's registers.cs / ss get these values; iretq into ring
3 uses them.

### copy_string_from_task: the trick

A syscall handler running in the kernel needs to copy a
NUL-terminated string FROM the user's address space INTO a
kernel buffer. The kernel and the task have DIFFERENT page
tables - same virtual address may map differently.

The old 32-bit trick:
1. Allocate a kernel scratch page.
2. Look at the task's page directory entry covering that
   address; save it.
3. Install a "writeable, present, user-accessible" mapping at
   that address in the TASK's directory.
4. Switch CR3 to the task's directory.
5. strncpy from `virtual` to `tmp`.
6. Switch CR3 back to the kernel's directory.
7. strncpy from `tmp` to the caller's `phys` buffer.
8. Restore the original task directory entry.

The L40 version does the same, but using the 4-level walk:

```c
void* phys_tmp = paging_get_physical_address(kernel_desc(), tmp);
struct paging_desc_entry old_entry;
memcpy(&old_entry, paging_get(task_desc, phys_tmp), sizeof(old_entry));
int old_entry_flags = old_entry.read_write | old_entry.present | old_entry.user_supervisor;
paging_map(task_desc, phys_tmp, phys_tmp, RW|P|U);
task_page_task(task);
strncpy(tmp, virtual, max);
kernel_page();
strncpy(phys, tmp, max);
paging_map(task_desc, phys_tmp,
           (void*)((uint64_t)old_entry.address << 12),
           old_entry_flags);
```

`kernel_desc()` is the kernel's paging_desc - it lands in L44.
`paging_get_physical_address` is from L29. `paging_get`
real-impl is from L31.

This function isn't tested until very late - it lights up the
first time isr80h dispatches a string-argument syscall (L52+).

## SamOs deviations from upstream

| Upstream | SamOs | Why |
|---|---|---|
| `panic("Elf files not supported\n");` then dead `//task->registers.ip = elf_header(...)` | same panic; comment kept | parity |
| trailing extra blank line in `task_new` | omitted | style |
| `goto out_free` -> `kfree(tmp)` -> `goto out` | single `out` with `if(tmp) kfree` | upstream's flow is fine; we mirror |
| process_switch call (SamOs had it from earlier) | KEPT removed for now; L40 has only paging_switch in task_switch | upstream's intent: process tracking happens at the process layer in L41 / L42 |

## What's NOT buildable yet

task.c references:

- `kernel_desc()` - L44
- `paging_desc_free` - L43
- `paging_get`, `paging_get_physical_address` - L29/L31 (done)
- `paging_desc_entry::user_supervisor`, `paging_desc_entry::address` - already present
- `task_return`, `restore_general_purpose_registers`, `user_registers` - task.asm symbols; need 64-bit task.asm rewrite (L41)
- `process_switch`, `struct process` - process.c (L42)
- `elf_header` - elfloader (not yet ported)

L45 reassembles all of this into a single buildable point.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/config.h` | USER_CODE_SEGMENT / USER_DATA_SEGMENT updated to 0x33 / 0x2B (RPL=3 selectors over the L39 GDT entries). |
| `src/task/task.h` | struct registers widened to 64-bit; struct task uses paging_desc; new task_paging_desc / task_current_paging_desc prototypes. |
| `src/task/task.c` | full rewrite mirroring upstream: 64-bit register copy, paging_desc API, panic on ELF, new copy_string_from_task using paging_get / paging_get_physical_address / kernel_desc. |

## How we verified

VGA tokens unchanged from L38 / L39:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
hello
Divide by zero error
```

task.c isn't in the build; we just confirm the tree still
builds with the rewritten source on disk.

## Forward look

L41 - task system part 2 (likely the task.asm 64-bit rewrite
of `task_return`, `restore_general_purpose_registers`,
`user_registers`).

L42 - process system rebuild (struct process widening,
process_load_switch, etc).

L42-extra - a small follow-up to L42.

L43 - paging_desc_free implementation.

L44 - kernel_desc + moving paging prototype to a common
header.

L45 - everything wired into the build, regression test.
