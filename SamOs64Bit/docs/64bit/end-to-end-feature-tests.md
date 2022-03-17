# SamOs Module 2 - end-to-end feature tests

How to build the kernel, boot it under QEMU, and observe each
feature actually doing its job from outside. The per-lecture
tests under `tests64/` are source-level audits; this doc is the
runtime walkthrough.

The kernel boots in either UEFI (upstream's default after L74)
or BIOS (SamOs's repair path in `boot.asm`). Both paths reach
the same `kernel_main`.

---

## 1. Build

```bash
cd SamOs64Bit
bash build.sh
```

Produces:

| Output | What it is |
|---|---|
| `bin/boot.bin` | 512-byte boot sector |
| `bin/kernel.bin` | flat kernel image, loaded by boot.asm to 0x100000 |
| `bin/os.bin` | 16 MiB FAT16 disk image with boot + kernel + user ELFs |
| `programs/*/blank.elf`, `shell.elf`, ... | userland binaries embedded on the disk image |

Useful build invariants you can check immediately:

```bash
# Kernel binary size (should be > 100 KiB once PCI/PATA/NVME land)
ls -la bin/kernel.bin

# Kernel binary embeds these source strings if the print path
# of each subsystem made it into the link
grep -ao 'Hello 64-bit!\|multiheap ready\|tss ready\|Total PCI devices' bin/kernel.bin

# Confirm user ELFs are on the FAT16 image
mdir -i bin/os.bin ::/
```

---

## 2. Boot under QEMU

### 2.1 BIOS path (works with SamOs's `boot.asm` long-mode rescue)

```bash
qemu-system-x86_64 \
    -drive file=bin/os.bin,format=raw,if=ide \
    -m 256 -accel tcg -display gtk -vga std
```

You should see the graphics framebuffer come up. Expected
visible elements:

- A solid grey desktop background (whatever the L87 startup
  paints).
- Mouse cursor responsive to host mouse.
- Title-bar / body chrome for any window the boot creates.

If nothing comes up, replace `-display gtk` with
`-display none -monitor stdio -no-reboot` and use the QEMU
monitor (`info registers`, `info status`) to confirm whether
the kernel halted or triple-faulted. See
`running-tests.md` for the monitor reference.

### 2.2 UEFI path (matches upstream verbatim)

Drop the disk image into an OVMF firmware run:

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive file=bin/os.bin,format=raw,if=ide \
    -m 256 -accel tcg -display gtk -vga std
```

If `OVMF.fd` is not installed: `sudo apt install ovmf` and
re-run.

---

## 3. Feature-by-feature smoke tests

Once the kernel is up, each subsystem can be exercised from
the outside. Group them by lecture range.

### 3.1 Boot + paging (L07-L33)

| Check | How |
|---|---|
| Long mode active | QEMU monitor: `info registers` → `EFER` bit 8 (LME) and bit 10 (LMA) set; CR0 bit 31 (PG) set; CR4 bit 5 (PAE) set. |
| Kernel-side paging desc loaded | Same view: CR3 should NOT be `0x9000` (boot's transition tables). It should be whatever `paging_desc_new` allocated from kheap. |
| Multiheap online | Booted kernel proceeds past `multiheap ready` print - observable in graphics terminal. |

### 3.2 IDT + IRQ (L35-L52, L67-L68)

| Check | How |
|---|---|
| IDT loaded | `info registers` → IDTR base/limit non-zero (a real table). |
| Timer IRQ firing | After a few seconds, `info registers` should show RIP rotating through scheduler code, not pinned to one instruction. |
| Keyboard IRQ unmasked | Press a key in the QEMU window; the focused user window receives a `WINDOW_EVENT_TYPE_KEY_PRESS`. |

### 3.3 Filesystem + disk (L46-L48, L57-L58, L64, L78, L82-L84, L183-L189)

| Check | How |
|---|---|
| FAT16 driver registered | `mdir -i bin/os.bin ::/` from the host shows BLANK.ELF, SHELL.ELF, etc. The kernel's `fs_resolve` recognises the same image at boot. |
| GPT partitions enumerated | Console output (or graphics terminal scroll) shows entries when boot reaches L82-L83. |
| PATA driver attached | After L189, `disk_search_and_init` calls `disk_driver_system_init` → `pata_driver_mount` registers disk0. |
| NVME driver enumerates if device present | Add `-device nvme,drive=nvme0 -drive if=none,id=nvme0,file=/tmp/nvme.img,format=raw` to QEMU. Kernel boot enumerates the controller via PCI scan, creates queues, exposes a second disk. |

### 3.4 Processes + ELF loader (L40-L48, L60-L66, L113-L116)

| Check | How |
|---|---|
| Process slot vector live | After L114, `process_system_init` runs at boot. |
| ELF loader resolves segments | If `process_load_switch("@:/shell.elf", &p)` succeeds, `p->task` has a non-zero RIP pointing into the ELF's `.text`. |
| Per-process allocations vector | Userland `peachos_malloc` returns successive pointers each call (visible via shell or blank program). |

### 3.5 Graphics + windows (L87-L100, L116-L150, L153-L173)

| Check | How |
|---|---|
| Framebuffer painted | Visible from `-display gtk` - solid background colour, no garbage. |
| Window create from userland | A user program calls `peachos_window_create("Test", 200, 200, 0, 0)` and a 200x200 window appears centred on screen. |
| Window keyboard listener | With a window focused, keypresses scroll into its body terminal. |
| Window mouse click | Clicking inside the body produces body-relative coords visible by reading window events. |
| Window close button | Clicking the title-bar close icon triggers `WINDOW_EVENT_TYPE_WINDOW_CLOSE` → `process_window_closed` frees the record. |

### 3.6 Mouse (L132-L142, L150)

| Check | How |
|---|---|
| Cursor responsive | Move the host mouse over the QEMU window. Cursor follows. |
| PS/2 IRQ 12 wired | Without moving, no spurious clicks; with movement, the move handler fires (visible as cursor position changing). |
| Click handler dispatches | Left-click on a window's title bar → focus event. Left-click on close icon → close event. |

### 3.7 Keyboard events (L174-L175, L196)

| Check | How |
|---|---|
| Keyboard listener registered before stage 2 | Kernel boot order has `keyboard_init` BEFORE `window_system_initialize_stage2` (L196). Without this, listener registration silently fails. |
| Event dispatched to focused window | Type into a focused window; the userland program's `peachos_process_get_window_event` loop receives `WINDOW_EVENT_TYPE_KEY_PRESS`. |

### 3.8 Sleep (L197-L198)

| Check | How |
|---|---|
| Task can sleep | Userland calls `peachos_udelay(100000)` (100 ms). The scheduler skips that task until `tsc_microseconds()` exceeds the deadline. |
| Whole-ring asleep does not panic | If every user task is sleeping, `task_next` udelays 100 us and retries instead of panicking. |
| Deadline is `+ microseconds` not `*` | L198 part 2 fix - verified by sleeping 1 second and timing it externally with `time`. |

### 3.9 PCI + NVME (L177-L195)

| Check | How |
|---|---|
| PCI device count printed at boot | L181 lands `print("Total PCI devices:")` + `itoa(pci_device_count())`. With `-machine pc` you'll see ~5 (host bridge, IDE, VGA, ISA, etc). |
| ECAM falls back to legacy IO | Without ACPI MCFG, `pci_ecam_available()` returns false and config-space access goes via 0xCF8/0xCFC. Validated by watching boot succeed without `-machine q35`. |
| BAR sizing fires | `pci_size_bars` runs once per (bus, dev, func). No exception during enumeration. |
| NVME enumeration | With an NVME drive attached (see 3.3), `nvme_disk_driver_mount` calls `nvme_disk_driver_mount_for_device`, completes the IO queue create flow, and exposes a disk. |

### 3.10 Stream cache (L203-L207)

| Check | How |
|---|---|
| Cache allocated per disk | Each disk created via `disk_create_new` gets a `disk->cache` from `diskstreamer_cache_new`. |
| Hit on second read | Use the userland shell to read a file, then read it again - second read goes through `diskstreamer_cache_find` without re-issuing disk IO. (Observable as faster response, or via QEMU's IDE trace.) |

---

## 4. Stress / regression smoke

After any non-trivial change to the kernel, run these in order:

```bash
cd SamOs64Bit

# 1. Build clean
bash build.sh

# 2. Per-lecture tests
bash tests64/run-all.sh
# Expect: N passed, M failed where M matches the known
# snapshot-pin set (see running-tests.md).

# 3. Boot in headless QEMU
qemu-system-x86_64 \
    -drive file=bin/os.bin,format=raw,if=ide \
    -m 256 -accel tcg -display none \
    -monitor stdio -no-reboot \
    -d int,cpu_reset -D /tmp/qemu-stress.log <<EOF
info status
info registers
pmemsave 0x100000 4096 "/tmp/kbin.dump"
quit
EOF
grep -q 'Triple fault' /tmp/qemu-stress.log && echo "TRIPLE FAULT" || echo "stress OK"
```

The kbin dump should match the start of `bin/kernel.bin`:

```bash
cmp -n 4096 /tmp/kbin.dump bin/kernel.bin && echo "kernel image loaded"
```

---

## 5. When something doesn't behave

If a feature you expected doesn't work:

1. **Check the per-lecture test.** It tells you what the
   lecture commit promised. If the test still passes, the
   subsystem source is intact and the issue is downstream
   (boot order, paging, etc).
2. **Check `debugging-history.md`.** Most non-obvious
   behaviour quirks (keyboard listener registration order,
   process_realloc(NULL) bug, NVME LBA-advance dead expression)
   are documented there with the lecture they landed in.
3. **Re-read the lecture's chapter doc.** Every commit has a
   `docs/64bit/chapters/Lxx-*.md` that records intentional
   deviations from upstream - preserved typos, NULL fall-
   back paths, static-inline shims.

If the bug is new (not in the docs), the workflow is:

- Capture a QEMU `-d int,cpu_reset` log.
- Identify the failing RIP via `nm bin/kernel.bin | sort -k1,1`.
- Read the source. Look for one of the known SamOs deviation
  patterns:
  - `if(!driver)` fallback paths in disk code (introduced at
    L186 - what happens when a vtable call lands on a NULL
    driver pointer).
  - `static inline` shims under typo'd names (PCI / NVME).
  - `(void)(...)` casts on upstream-preserved dead expressions
    (NVME read path).
- Cross-reference with the matching upstream commit at
  `~/work/PeachOS64BitModuleTwo`.
