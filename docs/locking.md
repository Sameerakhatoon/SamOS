# SamOS locking

The x86-64 kernel runs on every CPU the MADT lists. This file is the lock list: what each lock protects, the order they nest in, and the rules around them. The code that checks the order is `src/lib/spinlock/spinlock.c`.

## Rules

1. **Spinlocks only.** No sleeping locks: a task cannot block inside the kernel (one kernel stack per CPU, syscalls that wait are restarted).
2. **Interrupts off while held.** `spin_lock` saves RFLAGS and clears IF; the outermost `spin_unlock` restores it. Every trap already runs with IF clear (interrupt gates), so the only place the kernel runs with IF set is the idle loop, which holds no lock. An interrupt handler may therefore take any lock: it can never interrupt its own CPU's holder. The one exception is a disk completion wait, which holds `disk` and opens a window only the disk vectors and the IPIs can use, whose handlers take no lock below (see the last section).
3. **Order.** A CPU may take a lock only if its level is higher than every lock it already holds. A recursive lock may be taken again by its holder whatever it holds since. Two locks of the same level are never held together.
4. **Debug check** (`SAMOS_LOCK_DEBUG`, on by default; there is no separate release build). Every acquire checks rule 3 and panics on a violation, naming both locks, so an inversion shows up on its first run, even on one CPU. A waiter that spins more than 30 s (`SAMOS_LOCK_TIMEOUT_US`) prints the lock, the CPU holding it and the return address it was taken from, then panics. The same timeout guards the wait for a TLB shootdown or cross-CPU call answer.
5. **Spinning answers IPIs.** A CPU waiting for a lock, a shootdown or a cross-CPU call polls its pending TLB request (`smp_poll`). Without that, a CPU holding the kernel memory lock and waiting for a shootdown answer would deadlock with a CPU spinning, interrupts off, on that same lock.
6. **Panic takes no lock.** `panic` stops the other CPUs with an NMI first and then writes with `serial_write_unlocked`: the holder of the COM1 lock may be one of the stopped CPUs.

## Locks, outermost first

| Level | Lock | Where | Recursive | Protects |
|---|---|---|---|---|
| 10 | `gfx` | `graphics/graphics.c` (`gfx_lock`) | yes | The window system, the graphics tree, terminals, the mouse, every window-event ring and all screen output. Taken by syscalls 16-24 as a whole (`isr80h_handle_command`), `process_print`, `print`, the mouse IRQ, `keyboard_push`, and `process_terminate` around `process_close_windows`. |
| 20 | `process` | `task/process.c` (`process_lock`) | yes | `process_vector`, pid allocation, parent links, the zombie list, the foreground process, the per-process keyboard rings, and the "check, then block" of waitpid and getkey_block. |
| 30 | `vfs` | `fs/file.c` (`vfs_lock`) | yes | The descriptor table, the path parser and the filesystem drivers (FAT keeps one stream and its tables per volume). Every public VFS call; `file_remove` holds it over its type check and the remove. |
| 40 | `disk` | `disk_lock`, one for every disk (`disk/disk.c`) | yes | One CPU at a time in any disk driver, and the sector caches. Taken at the `disk_read_block` / `disk_write_block` boundary and around `diskstreamer_read` / `diskstreamer_write`. Never inside `disk/drivers/*`. One lock, not one per disk: two PATA drives share a channel's registers and IRQ, the AHCI ports of an HBA share its line and wait flag, and a completion wait steers that line to its own CPU (last section). |
| 50 | `sched` | `task/task.c` | no | Every CPU's run queue and sleep list, task states, `task->cpu`, the kill flags, the task list. |
| 60 | `kmem` | `memory/heap/kheap.c` (`kmem_lock`) | yes | The PFA, the multiheap, the slabs and the kernel half of the page tables (`paging_map` of an address at or above the direct map). One lock because it is one structure: the multiheap's sub-heap is the PFA's frame table, and mapping an arena page allocates page tables from the heap again. |
| 65 | `ioapic` | `io/apic.c` | no | The IOAPIC's IOREGSEL/IOWIN register pair: `ioapic_set_masked`, `ioapic_route_gsi`, `ioapic_set_dest`. |
| 70 | `smp`, `smp_call` | `cpu/smp.c` | no | One TLB shootdown at a time; one cross-CPU call at a time. |
| 80 | `serial` | `io/serial.c` | no | COM1. After the APs start, each CPU buffers up to a newline and sends the line under one hold, so lines never mix. |

`smp_test` (level 50) exists only in the SMP self-test.

## Nesting that happens

- `gfx` > `process`: a window event finds its process (`process_get_from_kernel_window`); `keyboard_push` picks the focused window's owner, pushes the key and wakes the reader.
- `gfx` > `vfs`: a window loads its icon.
- `process` > `sched`: waitpid blocks; an exiting child wakes its parent; kill marks and wakes its victim.
- `process` > `kmem`: a zombie is freed.
- `vfs` > `disk` > `kmem`: a read fills the sector cache.
- `disk` > `ioapic`: a completion wait points its line at the CPU that waits.
- `kmem` > `smp` > `serial`: a kernel-half unmap sends a shootdown.
- anything > `serial`: printing.

## Not under any lock, and why

- **A process's own memory, descriptors and user page tables.** A process has one task, and only that task's syscalls (or the loader, before `process_start`) touch them. The exceptions are a window closed by a mouse click while its owner runs elsewhere (under `gfx`, and the unmap is shot down on the owner's CPU) and kill, which never touches its victim's memory.
- **Kernel print from inside a lock below `gfx`.** `print` then writes COM1 only: `gfx_lock_for_print` refuses rather than invert the order.
- **The IOAPIC at boot** (`apic_init` masking every entry): before the APs start.
- **A disk wait's flags** (`struct disk_irq` in `disk/irqwait.h`): written by the waiter under `disk`, and by the interrupt handler, which takes no lock and only sets `pending` and `signal_cpu`.
- **Statistics** (`struct cpu` counters): written by their own CPU, read without a lock by syscall 51.
- **Init-time state** (ACPI tables, PCI list, filesystems registry, drivers list): built on the BSP before the APs start, read-only after.

## Disk completions by interrupt on several CPUs

The driver waits under `disk`, so with interrupts off; `disk_irq_wait` opens them in a `sti; nop; cli` window with the LAPIC TPR at 0x40, so only the disk vectors (0x50-0x5F) and the IPIs (0xF1-0xF3) can arrive there. The disk handlers take no lock. Of the IPIs, the TLB one flushes and takes no lock, the reschedule one does nothing in ring 0, and the call one runs only the self-test's functions. So the window breaks no rule above.

The IOAPIC sends each line to one CPU, the BSP at boot. A wait on an AP would then rely on the BSP taking the interrupt, and the BSP may be spinning on `disk` (or any lock) with interrupts off: the wait would run into its grace period and, after three in a row, drop to polling for good. So `disk_irq_arm` points the line at the CPU that is about to issue the command (`ioapic_set_dest`, only when it changes). With one `disk` lock there is one waiter per line at a time. `e2e/194` checks this on q35 (AHCI) and pc (PATA) with four CPUs reading at once: APs take their own completions (marker 190), none is taken by another CPU (191) and none is polled (187).

## Adding a lock

Pick a level from the table so that everything it is held around comes after it, add a row here, and run the suite once: rule 4 panics at the first acquire in the wrong order.
