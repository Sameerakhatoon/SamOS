# SamOs - GDB usage guide

End-to-end GDB workflows for debugging this kernel: attach,
breakpoints, symbol management for multiple ELFs at the same VMA,
stepping through interrupts, dissecting #PF / #GP, walking ring 3
entry, and the recipes I actually used to find G05/G07/G08/G10.

The terse cheatsheet lives in `docs/part6-gdb.md` (the book's own
Part 6 reproduced verbatim). This file is the long-form, with
worked examples.

---

## 0. Setup

QEMU has a built-in GDB stub. Run it with two flags:

| Flag | What it does |
|---|---|
| `-S` | Start the CPU paused; GDB has to tell it to continue |
| `-gdb stdio` | Multiplex the gdbserver over the same stdio QEMU is using |

Workflow:

```bash
cd ~/projects/samos
# Window 1: QEMU paused, gdbserver over stdio
qemu-system-x86_64 -hda bin/os.bin -m 256 -accel tcg -display none \
    -monitor none -S -gdb tcp::1234 &

# Window 2: GDB attaches
gdb -q
(gdb) set architecture i386
(gdb) target remote :1234
(gdb) add-symbol-file build/kernelfull.o 0x100000
(gdb) c          # let QEMU start executing
```

The `-gdb tcp::1234` form is what I prefer; QEMU's `-gdb stdio`
version works too but conflicts with `-monitor stdio`.

`add-symbol-file build/kernelfull.o 0x100000` loads symbols for
the linked kernel at the address `boot.asm` jumps to.

---

## 1. Loading symbols for user programs too

Every user task starts at virtual address 0x400000 with its own
page directory. Symbol-wise they all look the same to GDB:

```
(gdb) add-symbol-file programs/blank/blank.elf 0x400000
(gdb) add-symbol-file programs/shell/shell.elf 0x400000
```

Both are at the SAME VMA. GDB will use whichever was added last
when you ask it to resolve a name. If you want to switch which
user binary's symbols are "live", `remove-symbol-file` the other:

```
(gdb) info files          # which symbol files are loaded
(gdb) remove-symbol-file programs/blank/blank.elf
```

In practice for SamOs: once you know which task is running (via
`task_current()->process->arguments.argv[0]`), keep symbols for
that ELF loaded and unload the others.

---

## 2. Setting useful breakpoints

By symbol:

```
(gdb) break kernel_main
(gdb) break task_run_first_ever_task
(gdb) break interrupt_handler
(gdb) break idt_handle_exception
(gdb) break isr80h_handler
(gdb) break process_map_elf
(gdb) break panic
```

By raw address (for ASM stubs that don't get debug symbols):

```
(gdb) break *0x100020       # right after boot.asm jumps to kernel
(gdb) break *0x401000       # _start in any blank/shell
```

Conditional breakpoints are great for catching only the
interesting path:

```
(gdb) break interrupt_handler if interrupt == 0x0E
# fires only on #PF
(gdb) break task_switch if task == 0
# G05 sentinel: only when task_switch(NULL) is about to happen
```

Watchpoints catch silent memory corruption:

```
(gdb) watch current_task
# fires every time current_task is written
(gdb) watch *0xb8000
# fires every time the first VGA cell is touched
```

---

## 3. Inspecting machine state

The most-used commands are:

```
(gdb) info registers              # GPRs + segments + EFLAGS
(gdb) info reg cr0 cr2 cr3 cr4    # control registers
(gdb) info reg gdtr idtr tr ldtr  # descriptor-table bases
(gdb) info threads                # one CPU = one "thread" for us
(gdb) x/i $pc                     # current instruction
(gdb) x/8i $pc                    # next 8 instructions
(gdb) x/4i $pc-16                 # context before $pc too
(gdb) x/16xw $esp                 # top 16 dwords of stack
```

For SamOs-specific state:

```
(gdb) p current_task
(gdb) p current_task->process->filename
(gdb) p current_task->registers           # saved snapshot of task
(gdb) p task_head
(gdb) p *task_head                        # walk the schedule list
(gdb) p task_head->next->next             # ditto
(gdb) p current_process
(gdb) p kernel_chunk->directory_entry
(gdb) x/256xw current_task->page_directory->directory_entry
# dump the user page directory
```

---

## 4. Stepping through interrupts

Set a breakpoint on `interrupt_handler` and `continue`. When it
hits:

```
(gdb) p interrupt           # which vector?
(gdb) p frame->cs           # ring 0 or ring 3?
(gdb) p frame->eip          # where was the user when interrupted?
(gdb) p task_current()      # which task got interrupted?
```

To step into the callback:

```
(gdb) n                     # step over the kernel_page() etc
(gdb) s                     # step INTO interrupt_callbacks[i]
```

To skip ahead to "kernel returned to user mode":

```
(gdb) finish                # run until the current frame returns
# (gets you back to where the IRQ trapped from, in user mode)
```

For ring-3 -> ring-0 traps specifically, set a breakpoint on the
asm wrapper `isr80h_wrapper`, then `stepi` once you're in. The
`pushad` happens in front of your eyes and you can verify the
frame layout matches `struct interrupt_frame`.

---

## 5. Diagnosing a page fault (#PF, vector 0x0E)

When `interrupt_handler` fires with `interrupt == 0x0E`:

```
(gdb) p/x $cr2              # the faulting virtual address
(gdb) p frame->error_code   # decode below
(gdb) p frame->cs           # ring of the fault
(gdb) p frame->eip          # instruction that faulted
```

Error code bits (Intel SDM Vol 3, sec 4.7):

| Bit | Name | Meaning |
|---|---|---|
| 0 | P | 1 = protection violation, 0 = page not present |
| 1 | W | 1 = write, 0 = read |
| 2 | U | 1 = user-mode access, 0 = supervisor |
| 3 | RSVD | 1 = reserved bit set in PTE |
| 4 | I/D | 1 = instruction fetch, 0 = data |

Example decode: `error_code = 0x07 = 0b0111` means "user write to a
present page". Combined with the fact CR2 sits at 0x3FF000 = stack
top + 1, this is a user task writing past its mapped stack region
(see G10's investigation notes for the full case study).

After you know which page faulted, dump the user PD entry for it:

```
(gdb) set $vaddr = $cr2
(gdb) set $pde = current_task->page_directory->directory_entry[$vaddr >> 22]
(gdb) p/x $pde                  # PD entry
(gdb) set $pt = (uint32_t*)($pde & 0xfffff000)
(gdb) p/x $pt[($vaddr >> 12) & 0x3ff]  # PT entry
```

If PT entry is 0 the page is unmapped. If its low bits don't have
P=1 set, that's why the access faulted.

---

## 6. Walking the iret to ring 3

For G04 (the "iret to ring 3 appears to reset CS to 0x08"
diagnosis), the workflow was:

```
(gdb) break task_return
(gdb) c
# At task_return entry, examine the synthesized iret frame:
(gdb) x/5xw $esp           # SS, ESP, EFLAGS, CS, EIP that iretd will pop
(gdb) ni                   # step the pushes
(gdb) ni
...
(gdb) ni                   # we're now at the iretd instruction
(gdb) x/i $pc              # confirm
(gdb) si                   # STEP THE IRETD itself
```

After the single-step, check `info registers`:

| Expected | Got? |
|---|---|
| CS = 0x1B | If 0x08, the iretd silently flushed to 0 (CPU rejected the descriptor) |
| EIP = 0x401000 | If something else, the IP push was wrong |
| EFLAGS bit 9 (IF) = 1 | If 0, you missed `or eax, 0x200` before pushing |
| CR3 = task's PD | If kernel_chunk, you forgot `paging_switch(task->page_directory)` |

---

## 7. Tracing a syscall round-trip

To watch a `print()` from user mode all the way to VGA:

```
(gdb) break isr80h_handler
(gdb) c
# User calls int 0x80 cmd 1. We break in the C dispatcher.
(gdb) p command
(gdb) p frame->eax          # should match `command`
(gdb) p frame->esp          # user stack pointer
(gdb) x/xw frame->esp       # the pushed string pointer
(gdb) p (char*)frame->esp + 0    # ... peeked through user PT
```

If you want to peek the user's string contents from a kernel
breakpoint, you need the task's CR3:

```
(gdb) set $oldcr3 = $cr3
(gdb) set $cr3 = current_task->page_directory->directory_entry
# now reads of 0x400000+ go to the user's pages
(gdb) x/s *(char**)frame->esp
(gdb) set $cr3 = $oldcr3
```

(This last trick only works if GDB lets you write $cr3, which
depends on QEMU version. If it doesn't, set a breakpoint AFTER
the kernel's own `task_page()` / `paging_switch` and inspect there.)

---

## 8. Watching the schedule list

For G10 I wanted to watch the task list across a transient's
death:

```
(gdb) break task_list_remove
(gdb) commands
> silent
> printf "list_remove %s\n", task->process->arguments.argv[0]
> printf "  head=%p tail=%p current=%p\n", task_head, task_tail, current_task
> printf "  task->prev=%p next=%p\n", task->prev, task->next
> cont
> end
```

Now every list_remove logs a one-liner and continues. The G10 fix
showed up as the `task->next->prev = task->prev` line being absent
- the right-hand half of the chain stayed pointing at freed memory.

---

## 9. Catching a triple-fault

A triple-fault resets the CPU. Two ways to catch it pre-reset:

1. Run with `-no-reboot` so QEMU exits on triple-fault instead of
   resetting. Then GDB reports the exit and you can `bt`.
2. Set a breakpoint on `panic` AND on `interrupt_handler`. If the
   triple-fault came from a double-fault that wasn't double-faulted
   either, `interrupt_handler` may have fired with `interrupt ==
   0x08`. (We don't install a #DF handler, so this is mostly
   theoretical for SamOs.)

In practice, you'll catch most "kernel suddenly resets" issues
with `-no-reboot` + a breakpoint on `panic` and `idt_handle_
exception`.

---

## 10. End-to-end recipes for the bugs we found

### G05 (task_page null deref) recipe

```
(gdb) break task_page
(gdb) c
(gdb) p current_task           # is it 0?
(gdb) bt                       # who called task_page?
```

If `current_task == 0` and the caller is `interrupt_handler`, you
just reproduced G05.

### G07 (no task pre-iret) recipe

```
(gdb) break panic if (int)msg == (int)"No current task to save\n"
(gdb) c
(gdb) bt                       # interrupt_handler -> task_current_save_state
(gdb) p current_task           # null
(gdb) info registers           # CS = 0x08, EIP is in interrupt path
```

### G08 (kernel-mode IRQ segment reload) recipe

```
(gdb) break kernel_registers
(gdb) c
(gdb) info registers           # check CS at time of call
# If CS == 0x08, we're already in kernel mode and reloading kernel
# segments is a noop at best, a #GP source at worst.
```

### G10 (task_list_remove prev) recipe

See section 8 above.

### G11 (task_switch current_process) recipe

```
(gdb) break keyboard_push
(gdb) c
(gdb) p process_current()->arguments.argv[0]
(gdb) p current_task->process->arguments.argv[0]
# If those don't match, current_process and current_task have drifted.
```

### G12 (BSS aliasing) recipe

```
(gdb) break process_map_elf
(gdb) c
(gdb) p phdr->p_filesz
(gdb) p phdr->p_memsz
# If filesz < memsz, this PHDR has a .bss tail.
(gdb) p phdr_phys_address
# Without G12, this == elf_buffer + 0 for a .bss-only PHDR.
```

---

## See also

- `docs/part6-gdb.md` - the book's own brief GDB chapter, kept verbatim
- `docs/debugging-history.md` - what we hit and when
- `docs/testing-guide.md` - automated regression suite
- `docs/feature-walkthrough.md` - manual QEMU verification recipes
