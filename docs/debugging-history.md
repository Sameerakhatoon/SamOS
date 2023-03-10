# SamOs - debugging history

A chronological tour of every fault, panic, hang, silent miscompile,
and behavioural mystery this project ran into while being walked
through the book, plus the tool used to find each one and the fix.

If a future regression smells like one of these, this is the place
to start looking.

| # | What broke | When | Symptom | Found with | Fix lives in |
|---|---|---|---|---|---|
| G01 | Keyboard handler never drained 0x60 | Ch 45 | One keypress shows, the rest silently disappear | Direct inspection: `info registers` on QEMU monitor showed CPU was fine, but `info pic` showed IRQ1 line still asserted | `int21h_handler` reads port 0x60 before EOI |
| G02 | Key-release scancodes printed too | Ch 45 (after G01) | One press = two "Keyboard pressed!" lines | Sent specific keys, counted on screen vs expected | High-bit filter in `int21h_handler` |
| G03 | Stale .o files after header edit | Ch 73 | Test 23 flipped `fs=FAT16` to `fs=6` | Diff the test output vs source - `fat16_init()` was correct, the printed buffer was wrong | `build.sh` runs `make clean` every time |
| G04 | iret to ring 3 triple-faulted | Ch 98 | "entering userland..." prints, then CS stays 0x08 and EIP is in BIOS shadow | QEMU monitor `info registers` after the supposed iretd | Deferred per book; later patched by G05/G07/G08 chain |
| G05 | `task_page()` null-derefs on every IRQ before the first user task | Ch 113 | Kernel triple-faults under PIT/keyboard the moment IRQs go on, BEFORE the first task is launched | `info registers` showed CR3 = 0; tracing the IRQ stub revealed `task_page() -> task_switch(NULL)` from `interrupt_handler` | Guard `task_page()` and `task_current_save_state(frame)` with `if (task_current())` |
| G06 | `keyboard_pop` early-return was inverted | Ch 116 | `getkey` blocked forever; user task hung at the keystroke loop | Single-stepping `keyboard_pop` in QEMU monitor; the `if (!current_task) return 0;` ran when there WAS a task | Flip the condition |
| G07 | `interrupt_handler` panics on null current task between `enable_interrupts()` and the first task | Ch 150 | After enabling interrupts but before `task_run_first_ever_task()`, PIT fires, `idt_clock` calls `task_next()`, panic "No current task to save" | Added trace prints right before the panic message; only happened during the dual-load window | Wrap callback dispatch in `if (task_current())` AND guard the post-callback `task_page()` |
| G08 | `interrupt_handler` reloads kernel segments and saves task state on EVERY IRQ, even if the trap came from kernel mode | Ch 150 | Tests 05, 10, 34, 39, 40 flake/fail after Ch 150; sometimes `#GP` at `mov fs, ax` inside `kernel_registers` | `info registers` mid-fault showed `cs=0x08` (kernel) but `task_current_save_state` was writing into the task as if it had been user-mode | Gate the whole fix-up on `(frame->cs & 3) == 3` |
| G09 | PIT IRQ between the first and second `process_load_switch` hijacks the kernel | Ch 150 | Two-task demo only ever prints "Testing!" - "Abc!" never shows | Counter wired into `idt_clock` showed PIT fired exactly once between loads | `disable_interrupts()` across the dual load |
| G10 | `task_list_remove` missing next->prev symmetry | post-Ch 150 | 5-task demo: `Testing!` task NEVER prints, even after ~90 PIT cycles | Counter in `isr80h_command1_print` confirmed Testing!'s cmd 1 count = 0; then traced the list pointer state through each transient's death | Add `task->next->prev = task->prev` |
| G11 | `task_switch` doesn't update `current_process` | post-Ch 149 | Test 50 (caps lock): sendkeys never reach the KEY task | Probed sendkey + KEY task with serial counter; ALL keys were landing in whichever process was last `process_load_switch`'d at boot | Call `process_switch(task->process)` inside `task_switch` |
| G12 | `.bss`-only PHDR aliases the start of the ELF buffer | post-Ch 137 | Test 57: `static int c; c++; print(itoa(c));` printed `1179403647` (ELF magic interpreted as int) on first load instead of `1` | `readelf -l blank.elf` showed PHDR-2 has filesz=0 memsz=0x14 at vaddr 0x403000; mapping put it at `elf_buffer + 0` | Detour `p_filesz < p_memsz` PHDRs through a fresh `kzalloc(p_memsz)` buffer in `process_map_elf` |

---

## Anatomy of the bug-hunt loop we used

Every diagnosis above followed the same five-step loop. If a future
fault doesn't match one of the gotchas above, run this loop:

### 1. Is the kernel even alive?

The first question is always "is the CPU still running, or is it
spinning in a hlt loop after a triple-fault reset?"

Cheapest probe: COM1 mirror.

```bash
( sleep 5; printf 'quit\n' ) | qemu-system-x86_64 \
    -hda bin/os.bin -m 256 -accel tcg -display none \
    -serial file:/tmp/com.log -monitor stdio -no-reboot
cat /tmp/com.log
```

If the log shows the `entering userland... (deferred, G04)` banner,
kernel-init completed. If it stops at e.g. `bootsig=000055AA`, the
fault happened between that and the next print probe. The COM1 log
gives you a binary-search target with no guessing.

### 2. Where is the CPU right now?

Once you know the kernel is alive (or stuck in a panic loop), use
QEMU monitor's `info registers` (driven by `tests/_lib.sh ::
run_kernel_inspect`).

The seven registers you care about, in order:

| Register | What it tells you |
|---|---|
| `EIP` | Where the CPU is RIGHT NOW. Inside `[0x100000, 0x200000)` = kernel; `[0x400000, 0x500000)` = user code; anywhere else = something exotic (BIOS shadow, page-fault handler dispatch, panic spin) |
| `CS` | `0x08` = ring 0, `0x1B` = ring 3, anything else = wrong |
| `CR0` | Bit 0 = PE (protected mode), Bit 31 = PG (paging) |
| `CR2` | Faulting address on #PF. Cross-check against user PT setup |
| `CR3` | Page directory base. Zero = no paging or hijacked-by-task_switch(NULL) |
| `TR` | `0x28` once `ltr` ran (TSS is the bridge for ring-3->ring-0 traps) |
| `EFL` | IF bit 9 = interrupts on |

`info gdt` and `info tlb` are also available when you suspect the
GDT got reloaded with garbage or paging is mapping the wrong frames.

### 3. Are the IRQ paths what you think they are?

Use `info pic` to see which IRQ lines are currently asserted. If
your kernel "ignored a keypress" but IRR shows IRQ1 high, the PIC
sees the keypress but your handler hasn't drained it (G01). If the
PIC is fine but `task_current()` is wrong inside your handler,
that's a current_process/current_task drift (G11).

### 4. What if the kernel boots but the user task misbehaves?

Three orthogonal probes, in escalating cost:

1. **Counter in a syscall handler.** Easiest: declare a
   `static int counter = 0;` inside `isr80h_command1_print` or any
   handler you suspect; write the counter to a fixed VGA cell on
   each call. No tooling change, just rebuild. Used for G10
   (proving `Testing!`'s print count was zero) and G11 (counting
   keys by task).
2. **`hexdump bin/blank.elf`** to confirm what's actually on disk
   vs what you think the loader is mapping. Used for G12 (the .bss
   PHDR at offset 0 = aliases ELF header).
3. **GDB attach over `-S -gdb stdio`**, breakpoint on
   `process_map_elf` or `task_switch`, single-step. Used for G05
   (caught `task_switch(NULL)` in flight).

### 5. Did adding the fix break anything else?

Three different gotchas (G05, G07, G08) were all "interrupt_handler
shipped book-broken in some way" and each fix interacted with the
previous. Always re-run the full `tests/run.sh` after a kernel
patch, not just the failing test.

---

## The post-Ch-150 cluster

The post-Ch-150 work (this session) found three real kernel bugs
that the book ships intact and the symbol-presence tests do NOT
catch:

- **G10** task_list_remove (mid-list removal)
- **G11** task_switch (current_process drift)
- **G12** ELF loader (.bss aliasing)

All three only surface when you (a) load 3+ tasks AND (b) some die
mid-list (G10), or (a) have a task that consumes keys (G11), or
(a) have a user binary that touches .bss (G12). The book's own
sample programs don't trigger any of them.

Each was caught by writing a **behavioural** test that exercised
the path. The lesson: symbol-presence tests prove the function
exists, but only behavioural round-trip tests catch logic bugs that
ship in the function's body. The 13 new behavioural tests this
session (45..58) found 3 real bugs - roughly one bug per 4 new
behavioural tests. Anyone walking through the book today should
expect a similar hit rate as they add behavioural coverage.

---

## See also

- `docs/gotchas/G01.md` through `G12.md` - one file per bug with
  the full diagnosis and fix
- `docs/testing-guide.md` - how to drive the regression suite
- `docs/feature-walkthrough.md` - manual QEMU testing recipes
- `docs/part6-gdb.md` - the GDB cheatsheet for live debugging
