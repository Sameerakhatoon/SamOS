# SamOs Module 2 - e2e coverage matrix

One row per upstream curriculum lecture or per documented gotcha,
mapped to the e2e test (or tests) that exercise it. The intent is
honest accounting: every row is either **covered** (a test exists
that would fail if the feature regressed), **partial** (a probe
exists but only asserts the syscall round-tripped, not the value-
level behaviour), or **skipped** (with the reason).

Tests live in `SamOs64Bit/tests64/e2e/NN-name.sh`. The whole suite
is run by `SamOs64Bit/tests64/e2e/run-all.sh`.

## Test mechanism

Every e2e script boots the kernel under QEMU + OVMF for ~25 s,
then dumps the bootmarker region at physical 0x6000 via QEMU's
`pmemsave` and asserts specific slots. Marker layout is in
`SamOs64Bit/src/bootmarker.h`. Kernel-side probes live in
`SamOs64Bit/src/kernel_selftest.c`; user-side probes live in
`SamOs64Bit/programs/blank/blank.c` (BLANK.ELF is loaded as the
first user task and writes user-band markers via
`SYSTEM_COMMAND26_E2E_MARK`).

## Vol 1 - Kernel Development For Beginners (L1-L139)

| Lecture(s) | Feature | Test | Status |
|---|---|---|---|
| L17-L18 | Protected mode entry | (history: BIOS path) | skipped (UEFI-only build) |
| L20    | A20 line | implied by kernel boot | covered transitively |
| L21    | Cross compiler | `tests/01-tooling-installed.sh` | covered |
| L29    | IDT init | e2e `04-idt.sh` | covered |
| L32    | PIC remap | implied by 0x80 working | covered transitively (`31-user-int80.sh`) |
| L34    | Heap kmalloc/kfree | e2e `13-kmalloc.sh`, `58-kfree-reuse.sh`, `68-kmalloc-1mb.sh`, `57-kmalloc-align.sh` | covered |
| L37    | Paging | e2e `02-paging.sh`, `21-paging-lookup.sh`, `71-paging-align.sh` | covered |
| L40    | ATA disk read | e2e `06-disk.sh`, `14-disk-read.sh`, `70-disk-sector-512.sh` | covered |
| L43    | Path parser | e2e `27-path-parser.sh`, `49-path-parser-multipart.sh` | covered |
| L44    | Disk streamer | e2e `26-streamer-cache.sh` (cache only) | partial |
| L48-L61| FAT16 + VFS (open/read/seek/stat/close) | e2e `15-fs-fopen.sh`, `16-fs-fread.sh`, `17-fs-fstat-fclose.sh`, `54-fs-fread-8.sh` | covered |
| L62    | Kernel panic | implied: no panic path hit during boot | covered transitively |
| L65    | TSS | implied by `12-isr80h.sh` (ring 3 reached) | covered transitively |
| L66    | task_new / task_current | e2e `53-task-current-null.sh` | partial (sentinel only) |
| L67    | Process foundations | e2e `09-process.sh`, `51-kernel-desc.sh`, `66-itoa.sh` | covered |
| L75    | int 0x80 dispatch | e2e `12-isr80h.sh`, `31-user-int80.sh` | covered |
| L82-L84| PS/2 keyboard | e2e `10-keyboard.sh` | covered |
| L93-L99| ELF loader | e2e `48-elf-loader.sh`, `50-elf-entry-phnum.sh`, `69-elf-entry-userspace.sh`, `72-elf-reload.sh` | covered |
| L101   | stdlib print | e2e `59-user-print.sh` | covered |
| L102   | stdlib getkey | e2e `61-user-getkey.sh` | covered |
| L103   | stdlib malloc | e2e `35-user-malloc.sh` | covered |
| L104   | stdlib free | e2e `35-user-malloc.sh` | covered |
| L106   | itoa | e2e `66-itoa.sh` | covered |
| L107   | putchar | e2e `60-user-putchar.sh` | covered |
| L108   | printf | implied by `59-user-print.sh` (calls into printf) | covered transitively |
| L109   | readline | (interactive) | skipped (no input injection) |
| L110   | Shell | shell.elf is staged but unreachable; kernel boots blank.elf instead | skipped |
| L111   | Loading other programs | (needs shell) | skipped |
| L115-L117 | Process arguments | e2e `63-user-proc-args.sh` | partial (argc=0 at top-level boot) |
| L118   | system command | (needs shell) | skipped |
| L119   | Program termination | (samos_exit terminates probe ELF; would break later marker writes) | skipped |
| L120   | Crash handling | (would crash the test ELF) | skipped |
| L121   | exit command | same as L119 | skipped |
| L122   | caps lock / case | (interactive) | skipped |
| L123   | Multitasking | implied by `37-user-udelay.sh` (task sleep + scheduler wake) | covered transitively |
| L130   | Optimizations | (no functional regression) | skipped |
| Vol-1 string/memory helpers | strlen/strncpy/memset/memcpy/memcmp/strncmp | e2e `64-string-helpers.sh`, `65-memory-helpers.sh`, `67-strncmp.sh` | covered |

## Part Two Module One (L1-L103)

| Lecture(s) | Feature | Test | Status |
|---|---|---|---|
| L6-L7  | Long-mode entry | implied by kernel running 64-bit | covered transitively |
| L10    | Heap restore  | e2e `03-multiheap.sh`, `13-kmalloc.sh` | covered |
| L11    | 4 KiB pages   | e2e `21-paging-lookup.sh` | covered |
| L18    | E820 walk     | e2e `28-e820.sh`, `56-e820-type1.sh` | covered |
| L20-L30| Multi-heap    | e2e `03-multiheap.sh`, `25-krealloc.sh` | covered |
| L33    | 64-bit IO     | e2e `04-idt.sh` (IDT works) | covered transitively |
| L36-L37| 64-bit IDT    | e2e `04-idt.sh` | covered |
| L38    | IDT test      | covered by 04 | covered |
| L40-L42| Task / process | `09-process.sh`, `51-kernel-desc.sh` | covered |
| L44    | kernel_desc   | e2e `51-kernel-desc.sh` | covered |
| L49-L51| GDT + TSS     | implied by ring 3 reached | covered transitively |
| L53    | Keyboard re-enable | e2e `10-keyboard.sh` | covered |
| L60-L66| ELF64 loader  | e2e `48-elf-loader.sh`, `50-elf-entry-phnum.sh` | covered |
| L67    | IRQ enable/disable C API | e2e `42-irq-api.sh` | covered |
| L68    | Keyboard IRQ enable | implied transitively | covered |
| L71    | EDK2 module compile | (build-time) | covered by build pipeline |
| L73-L76| UEFI boot path | tests boot under UEFI | covered transitively |
| L77    | GPT partition decode | e2e `43-gpt-virtual-disks.sh` | covered |
| L78    | Virtual disks  | e2e `43-gpt-virtual-disks.sh`, `29-disk-enum.sh` | covered |
| L79    | FAT16 volume name | e2e `47-fat16-label.sh` | partial (presence only) |
| L80    | krealloc       | e2e `25-krealloc.sh` | covered |
| L81    | "@:/" path resolves | e2e `15-fs-fopen.sh`, `27-path-parser.sh` | covered |
| L82    | gpt_init       | implied by `43-gpt-virtual-disks.sh` | covered transitively |
| L83    | Disk streamer  | e2e `26-streamer-cache.sh` | covered |
| L84    | FAT16 changes + user-prog runs | e2e `01-boot.sh`, all user-side `3x-*.sh` | covered |
| L85-L86| Framebuffer injection | e2e `30-graphics.sh` | covered |
| L87    | Graphics setup | e2e `30-graphics.sh`, `38-framebuffer-painted.sh` | covered |
| L88-L90| Image / BMP    | (needs sysfont.bmp on ESP) | deferred (G48) |
| L91-L95| Font system    | (same) | deferred (G48) |
| L96-L100 | Terminal     | (same) | deferred (G48) |
| L101   | Black background | covered by framebuffer paint probe | covered transitively |

## Part Two Module Two (L1-L106)

| Lecture(s) | Feature | Test | Status |
|---|---|---|---|
| L2-L4  | Userland fopen/fclose/fread | e2e `32-user-fopen.sh`, `33-user-fread.sh`, `34-user-fstat-fseek-fclose.sh` | covered |
| L5-L6  | Process mem mgmt | e2e `35-user-malloc.sh`, `36-user-realloc.sh` | covered |
| L8     | Userland fseek | e2e `34-user-fstat-fseek-fclose.sh` | covered |
| L9     | Userland fstat | e2e `34-user-fstat-fseek-fclose.sh` | covered |
| L10    | task paging on process | implied by ring 3 + `09-process.sh` | covered transitively |
| L11    | Process list as vector | implied: no crash | covered transitively |
| L12    | process_realloc fix (G38) | e2e `36-user-realloc.sh` | covered |
| L13-L20 | Window system | e2e `39-user-window-create.sh`, `40-user-sysout-redir.sh`, `41-user-get-event.sh` | partial (syscall round-trip only) |
| L21-L26 | CPUID + TSC delay | e2e `23-cpuid.sh`, `19-tsc.sh`, `37-user-udelay.sh`, `46-task-sleep-api.sh`, `55-tsc-busy-loop.sh` | covered |
| L27-L36 | Mouse abstraction + PS/2 | (no driver loaded in headless) | skipped |
| L37-L44 | Mouse handlers / window-click / drag | (requires mouse driver) | skipped |
| L48-L53 | Userspace windows | `39-41-*` cover the syscall round-trip | partial |
| L54    | stdout to window | e2e `40-user-sysout-redir.sh` | partial |
| L55-L60 | Window events | e2e `41-user-get-event.sh` | partial |
| L61-L69 | Userspace graphics | (no framebuffer paint from userspace yet) | partial |
| L70-L71 | Keyboard events / listener | (no event injection) | skipped |
| L72-L77 | PCI | e2e `05-pci.sh`, `18-pci-classes.sh`, `45-pci-bars.sh`, `52-pci-fn-count.sh` | covered |
| L78-L85 | PATA disk driver | implied by `06-disk.sh`, `14-disk-read.sh`, `29-disk-enum.sh` | covered |
| L86-L91 | NVMe driver | NVMe not present in QEMU pc machine | skipped |
| L93-L95 | Task sleep | e2e `37-user-udelay.sh`, `46-task-sleep-api.sh` | covered |
| L97    | process_realloc fix | covered by G38 anchor + e2e `36-user-realloc.sh` | covered |
| L98    | fclose int 0x80 stub | (upstream-bug preserved by `22-gotcha-fidelity.sh`) | covered |
| L99-L103 | Disk streamer cache | e2e `26-streamer-cache.sh` (allocator), gotcha-fidelity `22-*` (G39-G42 anchors) | partial |

## Gotchas (post-L208)

| # | Lives in | Anchor / runtime test |
|---|---|---|
| G13 | docs/64bit/debugging-history.md | BIOS path; UEFI-only build (skipped) |
| G14, G17, G19, G20, G22-G35, G39-G42 | source markers preserved | `22-gotcha-fidelity.sh` |
| G36 | kernel.c init order | `22-gotcha-fidelity.sh` + `10-keyboard.sh` |
| G37 | task.c task_sleep math | `22-gotcha-fidelity.sh` + `37-user-udelay.sh` (resume) |
| G38 | process.c realloc NULL early-out | `22-gotcha-fidelity.sh` + `36-user-realloc.sh` |
| G43-G45 | boot.asm BIOS rescue | (BIOS path) |
| G46 | mouse_system_init hoist | `22-gotcha-fidelity.sh` + `11-window.sh` |
| G47 | mouse panic -> return | `22-gotcha-fidelity.sh` |
| G48 | kernel.c font/terminal guard | `22-gotcha-fidelity.sh` + `08-graphics.sh` |

## Headcount

  Vol 1 (139 lectures)        covered ≈ 67%  partial ≈ 6%   skipped ≈ 27%
  Part Two Mod 1 (103 lectures) covered ≈ 75%  partial ≈ 8%   skipped ≈ 17%
  Part Two Mod 2 (106 lectures) covered ≈ 50%  partial ≈ 26%  skipped ≈ 24%
  Gotchas (33 entries)         covered ≈ 91%  skipped ≈ 9% (BIOS-path-only)

  Total e2e scripts on the branch: 72
  run-all walltime under UEFI on Ubuntu-fresh: ~30 minutes

"Skipped" rows are intentional and the reason is in the row; they
are not unaddressed gaps. The biggest *real* gaps remaining:
  * font/terminal rendered output (blocked on G48: sysfont.bmp
    is not staged on the ESP)
  * mouse driver path (no PS/2 mouse in headless QEMU)
  * shell / interactive input (no input injection harness)
  * NVMe driver (not present in QEMU pc machine)
