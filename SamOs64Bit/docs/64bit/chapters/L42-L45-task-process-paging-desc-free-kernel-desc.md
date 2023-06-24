# Lectures 42 - 45 - process / paging_desc_free / kernel_desc / wire-up

**Source commits (PeachOS64BitCourse):** `5073656` (L42), `13f4cea` (L42-extra), `698cf6d` (L43), `80a3e1b` (L44), `da8aef0` (L45)
**SamOs commit:** L45 on `module1-64bit` branch
**Regression test:** `tests64/L45-task-process-linked.sh`

## Why this chapter exists

L40 / L41 rewrote task.h / task.c / task.asm but kept them out
of the build because they referenced things that did not exist
yet:

- `paging_desc_free` (frees a 4-level paging tree)
- `kernel_desc()` (returns the kernel's paging_desc)
- `process_switch`, `struct process` machinery

L42 - L45 land the missing pieces and then add task / process
to the Makefile FILES list.

## L42 - process.c paging_4gb_chunk -> paging_desc rename

Mechanical four-site replace. Old code:

```c
paging_map_to(process->task->page_directory, ...);
```

New:

```c
paging_map_to(process->task->paging_desc, ...);
```

Plus the ELF loading paths (`process_load_elf`,
`process_map_elf`) get stubbed to return error - the 64-bit
elfloader is not in.

## L43 - paging_desc_free

Recursive teardown of a 4-level paging tree. The interesting
thing is the level discipline:

```c
static void paging_desc_entry_free(struct paging_desc_entry* table_entry,
                                   paging_map_level_t level)
{
    if (paging_null_entry(table_entry)) return;
    if (level == 0)                     panic("level must be >= 1");
    if (level > PAGING_MAP_LEVEL_4)     panic("level > 4 not supported");

    if (level > 1) {
        for (int i = 0; i < PAGING_TOTAL_ENTRIES_PER_TABLE; i++) {
            struct paging_desc_entry* entry = &table_entry[i];
            if (!paging_null_entry(entry)) {
                struct paging_desc_entry* child =
                    (struct paging_desc_entry*)(((uint64_t)entry->address) << 12);
                if (child) {
                    paging_desc_entry_free(child, level - 1);
                }
            }
        }
    }
    kfree(table_entry);
}
```

We walk DOWN from level N to level 2, recursively freeing
intermediate tables. At level 1 the walk doesn't run (the body
is `if (level > 1)`) - we just kfree the PT table itself. The
4-KiB pages mapped BY the PT are user data; whoever allocated
them owns them.

`paging_desc_free` is the public entry: walks the PML4, recurses
into non-null entries at level-1 (= 3 = PDPT), then kfrees the
PML entries block and the descriptor struct.

The level discipline matches the "kzalloc when missing" logic in
`paging_map` (L13) - we are unwinding what map built.

### Bonus side-effect: kernel.h needed for panic()

`paging_desc_entry_free` calls `panic` on invalid levels. paging.c
didn't include kernel.h before; L43 (and our port) add it.

## L44 - kernel_desc() + paging_null_entry hoist

Two small things:

1. `kernel.h` forward-declares `struct paging_desc` and the
   `kernel_desc()` getter. `kernel.c` implements it as a one-
   liner returning `kernel_paging_desc`.
2. paging.c hoists `paging_null_entry` above its callers. SamOs
   already did this at L31 (we forward-declared it for
   `paging_get`), so L44's hoist is a no-op for us.

`kernel_desc()` is what `task.c::copy_string_from_task` needs to
resolve the physical backing of a kernel-side scratch page.

## L45 - wire everything in

Makefile adds:
- `./build/task/task.o`
- `./build/task/task.asm.o`
- `./build/task/process.o`

build.sh adds `mkdir -p build/task`.

kernel.h widens the ERROR macros:

```c
#define ERROR(value)   (void*)((intptr_t)(value))
#define ERROR_I(value) (int)((intptr_t)(value))
```

The intptr_t intermediate keeps the cast chain well-defined for
both the small-negative-int case (`-EINVARG` -> `(void*)-1` etc)
and the actual-pointer case. Without it, a signed int -> void*
cast on x86_64 sign-extends to the high half and the result is
a kernel-mode address that subsequent checks may misinterpret.

process.c additionally stubs:
- `process_load_binary` (calls fopen / fstat / fread / fclose -
  FAT16/VFS not ported)
- `process_free_elf_data` (calls elf_close)
- fs/file.h and loader/formats/elfloader.h includes commented
  out

After all this, the link succeeds. Boot reaches the same point
as L38 - L40: idt_init -> "hello" -> div_test -> "Divide by
zero error". No task has been CREATED (kernel_main doesn't call
task_new), so the new code paths are dormant. But they LINK -
which is the L45 milestone.

## How the change lands in our tree

| File | Change |
|---|---|
| `src/task/process.c` | `page_directory -> paging_desc` global rename. `process_load_elf` / `process_map_elf` stub-return errors. `process_load_binary` / `process_free_elf_data` stubbed. fs/elfloader includes commented out. |
| `src/memory/paging/paging.c` | new static `paging_desc_entry_free` (recursive). New `paging_desc_free` (PML4 walk + final kfrees). kernel.h include added for panic. |
| `src/memory/paging/paging.h` | `paging_desc_free` prototype. |
| `src/kernel.h` | forward `struct paging_desc`. Declare `kernel_desc()`. ERROR macros use intptr_t intermediate. |
| `src/kernel.c` | implement `kernel_desc()` returning `kernel_paging_desc`. |
| `Makefile` | add task.o, task.asm.o, process.o to FILES; rules. |
| `build.sh` | mkdir build/task. |

## How we verified

VGA after L45:

```
Hello 64-bit!
e820 total: 267910144
ABCmultiheap ready
hello
Divide by zero error
```

Same tokens as L38 - L40. The "we are still alive at IDT
dispatch" property holds. What's new: every external symbol
in task.c / task.asm / process.c resolved at link time. A
missing dependency would have failed the build with an
"undefined reference" error.

## Forward look

L46 - L48 brings FAT16 and disk back into the 64-bit build
(so process_load_binary can read actual files). L49 - L51
builds the TSS C-side and GDT. L52 - L53 restores the keyboard
+ IRQ wiring. L54 onward fills in the isr80h command table.

Once disk + FAT16 + ELF loader land, the L40 - L45 task
machinery can finally launch a user process. Until then it's
dormant but linkable.
