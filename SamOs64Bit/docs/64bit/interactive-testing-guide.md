# SamOs Module 2 - interactive testing guide

End-to-end guide for *driving the kernel by hand* and watching what
happens. This complements the automated suite (`tests64/e2e/run-all.sh`)
which exercises the same code paths headlessly; here we trade speed
for the ability to see, type at, poke at, and pause the running OS.

Read this in order on your first session; bookmark sections 3-6 as the
day-to-day reference once you've done it once.

---

## 0. What "interactive" means here, exactly

SamOs has **no terminal you can type at like a Unix shell**. There is
no readline prompt, no exit-and-rerun cycle from inside the OS. What
we have:

- A **kernel boots in QEMU and paints a framebuffer.** You see it
  with your eyes. The first ~12 init-checkpoint markers tell you
  whether each subsystem came up.
- **One user task** (`BLANK.ELF`) is loaded by `kernel.c` after the
  kernel-side selftest runs. It exercises ring-3 syscalls (fopen,
  fread, malloc, etc.) and exits to a spin loop.
- **QEMU's monitor** is your debugger console. From it you can read
  guest memory, send keys, single-step, dump framebuffer to PPM,
  inject device events, halt the CPU.
- **GDB attached to QEMU's gdbserver** is your source-level debugger.

"Interactive testing" therefore means a combination of:
1. *Visual boot inspection* (look at the QEMU window)
2. *Monitor poking* (`pmemsave`, `info registers`, `sendkey`, etc.)
3. *GDB stepping* (breakpoints in kernel.c, watch variables)
4. *Edit-rebuild-relaunch loops* (the iteration unit is ~30 s)

There is **no live REPL inside the OS** until L110 shell.elf is wired
back into the boot path (see section 13).

---

## 1. Prerequisites - verify before you start

Run these once. If any fail, run `bootstrap.sh` from the project root.

```bash
# Apt-level dependencies
which qemu-system-x86_64 nasm parted mkfs.vfat mcopy
ls /usr/share/ovmf/OVMF.fd

# Cross compiler
$HOME/opt/cross/bin/x86_64-elf-gcc --version | head -1

# EDK2 + BaseTools
ls $HOME/edk2/BaseTools/Source/C/bin/GenFw
ls $HOME/edk2/MdeModulePkg/Application/SamOs/SamOs.inf
```

Expected: every command produces output, no "not found" errors. If
EDK2 fails, run `bash bootstrap.sh` and wait for "bootstrap complete".

---

## 2. Cold build → first interactive boot

The "minimum viable hello":

```bash
cd ~/projects/samos
bash build.sh           # ~1-2 min; writes bin/os.img and bin/SamOs.efi
bash run.sh             # opens a QEMU window
```

`run.sh` is just:

```
qemu-system-x86_64 -machine pc \
    -drive file=./bin/os.img,format=raw,if=ide \
    -m 512M -cpu Skylake-Server,tsc-frequency=2800000000 \
    -bios /usr/share/ovmf/OVMF.fd
```

**What you should see, in this order, after `bash run.sh`:**

1. White-on-black TianoCore UEFI splash (OVMF's boot logo).
2. A pause of 1-3 seconds while EDK2 loads SamOs.efi → kernel.bin.
3. The screen flips to a black framebuffer. (G45 in the docs: print()
   writes to the graphics framebuffer after L100, not to 0xB8000.
   In headless / TCG the painting may be too fast to read.)
4. (After our G48-unblock commit) The 9x16 stub font renders text via
   `terminal_create`, but every glyph is a solid white block - the
   atlas is intentionally stubbed.
5. The OS halts in a spin loop forever (BLANK.ELF's `while(1){}`).

**To quit QEMU:**
- Click into the QEMU window to capture keyboard
- Press `Ctrl+Alt+G` to release pointer/keyboard back to host
- Close the QEMU window, or hit `Ctrl+Alt+Q`

**If you got past steps 1-2 but the screen stays UEFI-grey:** the
kernel didn't paint. Either `graphics_setup_stage_two` died, or
`graphics_redraw_all` was never called. Jump to section 11.

**If you see ANY shell-like text, like `samos> _`:** you accidentally
swapped BLANK.ELF for SHELL.ELF in `kernel.c`. That's the L111
configuration; today, the kernel ships with BLANK as the user task.

---

## 3. The QEMU monitor - your live debugger console

Stop using `run.sh` and start QEMU with **the monitor on stdin**:

```bash
qemu-system-x86_64 \
    -machine pc -cpu Skylake-Server \
    -drive file=$HOME/projects/samos/bin/os.img,format=raw,if=ide \
    -m 512M -bios /usr/share/ovmf/OVMF.fd \
    -monitor stdio
```

The terminal you launched from is now a `(qemu)` prompt. Every command
in this section runs at that prompt. To return to running the OS:
just hit Enter; the OS keeps running in the QEMU window while you
type commands here.

### 3.1 Inspect the bootmarker region while the OS is alive

```
(qemu) pmemsave 0x6000 1536 "/tmp/markers.bin"
```

Then in a separate shell:

```bash
od -An -v -tx8 -w8 /tmp/markers.bin | nl -ba | head -30
```

You should see one 8-byte slot per line, starting at slot 0. The high
half is `b007c0de` for every slot the kernel reached. The low half
encodes the stage id in its top byte and the value in the lower 24
bits. Example:

```
2  b007c0de01000000   <-- slot 1 = BM_STAGE_KERNEL_MAIN, value 0
3  b007c0de02000000   <-- slot 2 = BM_STAGE_PAGING_SWITCHED
...
6  b007c0de05000006   <-- slot 5 = BM_STAGE_PCI_SCANNED, value 6 PCI devices
```

`SamOs64Bit/src/bootmarker.h` lists every slot. Scanning this dump by
eye tells you exactly how far the boot got and what each subsystem
reported.

### 3.2 Read kernel memory at a known address

```
(qemu) x/16xw 0x100000      # first 64 bytes of kernel.bin in RAM
(qemu) x/32xb 0x6000        # first 32 marker bytes
(qemu) x/4s 0x100000        # try to read 4 strings (rarely useful)
```

`x/Nxw` = N words in hex. Replace `x` with `i` for instructions.

### 3.3 Look at CPU state

```
(qemu) info registers
(qemu) info mem              # memory translation tree
(qemu) info irq              # outstanding IRQs
(qemu) info pic              # PIC mask / ISR / IRR
```

### 3.4 Capture the framebuffer to PPM

```
(qemu) screendump /tmp/screen.ppm
```

Then open `/tmp/screen.ppm` with any image viewer or convert it:

```bash
convert /tmp/screen.ppm /tmp/screen.png
```

This is what `tests64/e2e/79-qemu-screendump.sh` does headlessly.
Useful when iterating on font/graphics regressions: take a snapshot
before and after the change.

### 3.5 Inject keystrokes

```
(qemu) sendkey a            # press 'a'
(qemu) sendkey ret          # Enter
(qemu) sendkey ctrl-c       # chord
(qemu) sendkey shift-1      # 'A' (kind of)
```

These deliver scancodes through QEMU's PS/2 emulation. Whether the
kernel sees them depends on whether the IRQ1 routing chain is alive
all the way to the per-process keyboard buffer (today the
`116-user-keyboard-input.sh` test reports DEFER because this chain
loses scancodes; see section 11).

### 3.6 Pause and resume execution

```
(qemu) stop                 # halts the CPU
(qemu) info registers       # examine state while paused
(qemu) cont                 # resume
```

### 3.7 Quit

```
(qemu) quit
```

---

## 4. Pre-canned interactive boot recipes

These four shells cover the common cases. Pick one based on what you
need to look at.

### 4.1 Default GUI boot (eyes-on)

```bash
bash run.sh
```

### 4.2 Headless + monitor (poke memory)

```bash
qemu-system-x86_64 -machine pc -cpu Skylake-Server \
    -drive file=bin/os.img,format=raw,if=ide -m 512M \
    -bios /usr/share/ovmf/OVMF.fd \
    -display none -monitor stdio
```

You get the `(qemu)` prompt and no window. Useful when SSH'd in
or running over a slow display.

### 4.3 Serial console capture (kernel print output)

The kernel's `print()` goes to the graphics framebuffer, not to
serial - so for the 64-bit kernel this serial trick is **NOT useful**
for reading kernel output. (It IS useful for tests/ in Module 1.)

If you want kernel prints to land on stdout, the right move is to
temporarily add `outb(0xE9, c)` to `terminal_writechar` (the bochs
debug port) and add `-debugcon stdio` to QEMU. That's a 2-line patch.

### 4.4 GDB stub on port 1234

```bash
qemu-system-x86_64 -machine pc -cpu Skylake-Server \
    -drive file=bin/os.img,format=raw,if=ide -m 512M \
    -bios /usr/share/ovmf/OVMF.fd \
    -monitor stdio -s -S
```

The `-s` exposes a gdbserver on tcp:1234. The `-S` pauses the CPU at
boot so GDB can attach before the kernel runs. From another terminal:

```bash
x86_64-elf-gdb SamOs64Bit/bin/kernel.bin \
    -ex 'target remote :1234' \
    -ex 'break kernel_main' \
    -ex 'continue'
```

Stays paused at `kernel_main`'s opening brace. See section 9.

---

## 5. Driving each subsystem interactively

For each subsystem below: what to look at, what command, what you
should see, what a regression looks like.

### 5.1 Heap (kmalloc / multiheap)

In a monitor session, dump the marker region. Slot 16 (kmalloc round
trip), 17 (kmalloc 64KB), 28 (kzalloc zero), 31 (krealloc), 73
(kmalloc alignment), 74 (kfree reuse), 119 (1MB recycle), 159
(deterministic alloc/free cycles), 166 (OOM), 167 (kzalloc 32KB).

If slot 17 is reached but value=0, your big allocation broke.
If slot 16 is not reached at all, the kmalloc round-trip probe
crashed silently - your kmalloc panics on a corner case.

### 5.2 Paging

Slot 2 (paging switched), 27 (paging_get_physical roundtrip), 66
(kernel_desc returns), 90 (paging_align_address rounds up), 154
(kernel_desc level == 4).

If slot 2 is not reached, the kernel never enabled paging. Boot
froze in `kernel_paging_init`. Attach GDB and `break paging_init`.

### 5.3 Disk / FS

Slot 6 (disk_init), 7 (fs_resolved), 18 (disk_read_block sector 0),
36 (disk_get 0), 57 (GPT virtual disks), 70 (sector_size 512),
89 (PATA sector size 512), 126 (LBA 2 read), 127 (two open fds),
161 (LBA 8 read), 165 (font_load missing → NULL).

To watch the disk pipeline live: `pmemsave 0x6000 1536 /tmp/m.bin`
*twice* - once near boot and once 10 seconds later. Compare. If
slot 19 (fopen) is value=1 in the second dump but not the first,
something between t=N and t=N+10 succeeded that hadn't before.

### 5.4 Graphics / framebuffer

Slot 8 (graphics setup), 37 (screen info), 38 (size > 0), 39 (back
buffer painted), 51 (font stroke), 52 (window border), 97 (front
buffer painted), 98 (font loaded), 100 (terminal create), 102 (BMP
load), 103 (font draw), 133 (rect), 138 (redraw region), 140 (pixel
get/set), 141 (draw at offset), 148 (root z=0), 167 (relative gfx).

Interactive flow:

```
(qemu) screendump /tmp/before.ppm   # at t=0
(qemu) ... wait 10 seconds for stuff to paint ...
(qemu) screendump /tmp/after.ppm    # at t=10
```

Then `diff /tmp/before.ppm /tmp/after.ppm | head` should be non-empty.

### 5.5 PCI

Slot 5 (PCI scanned, value = device count), 23 (IDE class), 24 (VGA
class), 60 (BAR populated), 68 (count > 1), 93 (host bridge),
121 (vendor field non-zero), 151 (OOB returns < 0), 96 (NVMe present).

The QEMU monitor also gives you a live PCI view:

```
(qemu) info pci
```

Compare this against what the kernel's `pci_device_count` reported in
slot 5.

### 5.6 Keyboard

Slot 10 (keyboard init), 56 (IRQ enable round trip), 120 (push/pop
API surface). The **end-to-end IRQ chain test** is slot 160; it is
currently DEFER (see section 11).

Live keystroke injection:

```
(qemu) sendkey a
(qemu) sendkey b
(qemu) sendkey ret
```

If you also attached GDB and breakpointed `keyboard_push`, you'd see
each `sendkey` hit the breakpoint. If you don't (and 160 stays
DEFER), the IRQ chain is broken between sendkey and keyboard_push.

### 5.7 Mouse

Slot 11 (window stage 2 - requires mouse subsystem alive), 104
(mouse_register), 105 (click handler), 106 (move handler), 107
(click dispatches), 108 (move dispatches).

We register a *synthetic* mouse in kernel_selftest; no real PS/2
mouse driver is loaded. To get a real mouse you'd:

```bash
qemu-system-x86_64 ... -device i8042 ...
```

and then wire `mouse_system_load_static_drivers` to actually
register the PS/2 driver. That's port work, not test work.

### 5.8 Process / task / userland

Slot 9 (process system inited), 12 (isr80h registered), 53
(task_current NULL at kernel_main, sentinel), 61 (task_sleep API
surface), 69 (task_current confirmed NULL), 131 (task_get_next NULL).

Then the **user-side band** (slots 40-50, 80-84): each one is
written by blank.elf running in ring 3. If slot 48 (USER_INT80_REACH)
is value=1 in your dump, the user task ran and reached the int 0x80
syscall. From there, slots 40-46 tell you which userland syscalls
round-tripped.

### 5.9 ELF loader

Slot 62 (elf_load + close), 64 (entry ptr non-NULL), 65 (phnum > 0),
88 (entry in user range), 91 (load + close + load again).

This is one of the most reliable parts of the kernel; a regression
here means user programs stop running.

### 5.10 String / memory helpers

Slot 75 (strlen), 76 (strncpy), 77 (memset), 78 (memcpy), 79
(memcmp), 85 (itoa), 86 (strncmp), 132 (tolower), 135 (isdigit),
136 (tonumericdigit), 137 (strnlen_terminator).

---

## 6. Editing → rebuilding → relaunching loop

The 30-second iteration unit. You change one thing, see if it broke
or fixed something:

```bash
# 1. Edit (kernel side or user side)
vim SamOs64Bit/src/kernel.c       # or kernel_selftest.c
vim SamOs64Bit/programs/blank/blank.c

# 2. Rebuild
bash build.sh                     # ~1-2 minutes

# 3. Run one targeted test
bash SamOs64Bit/tests64/e2e/01-boot.sh        # 22 s, smoke
bash SamOs64Bit/tests64/e2e/13-kmalloc.sh     # 22 s, heap-specific

# 4. Or run interactively
bash run.sh                                    # eyes-on
```

For a single edit cycle, prefer step 3 (one e2e script) over step 4
(visual boot) - the e2e script gives you a hard PASS/FAIL with a
diagnostic line; the visual boot gives you a black framebuffer that's
hard to interpret.

Once you're satisfied with the targeted test, run the full sweep:

```bash
bash SamOs64Bit/tests64/e2e/run-all.sh     # ~45 minutes
```

---

## 7. Reading the e2e harness so it matches your interactive runs

When `01-boot.sh` passes but your interactive `run.sh` looks broken,
the difference is almost always one of these:

| What | e2e (`boot_and_dump`) | `run.sh` |
|---|---|---|
| Display | `-display none` | window |
| Monitor | `stdio` + scripted pmemsave | (nothing; you can attach) |
| TSC clock | not pinned | `tsc-frequency=2800000000` |
| Walltime | 20-25 s and quit | runs forever |
| Image | same `bin/os.img` | same `bin/os.img` |

The TSC frequency difference matters if you're testing tsc_microseconds
or udelay-based features - `run.sh` pins the frequency for stable
timing, e2e leaves it free.

To make `run.sh` mimic an e2e test exactly, add `-display none -monitor
stdio` and watch the kernel print to your terminal (after the same
debugcon hack from section 4.3).

---

## 8. Quick recipes for common scenarios

### 8.1 "I changed the heap. Did I break anything?"

```bash
bash build.sh
bash SamOs64Bit/tests64/e2e/13-kmalloc.sh         # 22 s
bash SamOs64Bit/tests64/e2e/57-kmalloc-align.sh
bash SamOs64Bit/tests64/e2e/58-kfree-reuse.sh
bash SamOs64Bit/tests64/e2e/68-kmalloc-1mb.sh
bash SamOs64Bit/tests64/e2e/88-kmalloc-stress.sh
bash SamOs64Bit/tests64/e2e/89-kmalloc-big-recycle.sh
bash SamOs64Bit/tests64/e2e/97-krealloc-shrink.sh
bash SamOs64Bit/tests64/e2e/115-multiheap-determinism.sh
bash SamOs64Bit/tests64/e2e/122-multiheap-oom.sh
```

### 8.2 "I touched graphics. Visual confirmation."

```bash
bash build.sh
bash SamOs64Bit/tests64/e2e/30-graphics.sh
bash SamOs64Bit/tests64/e2e/38-framebuffer-painted.sh
bash SamOs64Bit/tests64/e2e/78-framebuffer-front.sh
bash SamOs64Bit/tests64/e2e/79-qemu-screendump.sh
# Then visually:
bash run.sh
# Use the QEMU monitor: (qemu) screendump /tmp/visual-check.ppm
```

### 8.3 "I touched a gotcha source-anchor. Did the audit trail survive?"

```bash
bash SamOs64Bit/tests64/e2e/22-gotcha-fidelity.sh    # 50 ms, 30 anchors
```

If this fails, your cleanup edit removed a documented sic marker
from `src/`. Re-add the marker and update
`docs/64bit/debugging-history.md` if the docs need to follow.

### 8.4 "I want to see one specific marker slot interactively."

```bash
qemu-system-x86_64 -machine pc -cpu Skylake-Server \
    -drive file=bin/os.img,format=raw,if=ide -m 512M \
    -bios /usr/share/ovmf/OVMF.fd -display none -monitor stdio &

# Inside the (qemu) prompt that appears in your terminal:
(qemu) pmemsave 0x6000 1536 /tmp/markers.bin
(qemu) quit

# In another shell:
od -An -v -tx8 -w8 /tmp/markers.bin | nl -ba | sed -n "N,Np"
# where N = slot number + 1 (od line 1 = slot 0)
```

---

## 9. GDB - when "why is it crashing" needs more than markers

Two terminals, same machine.

**Terminal 1 (QEMU paused, gdbserver on 1234):**

```bash
qemu-system-x86_64 -machine pc -cpu Skylake-Server \
    -drive file=bin/os.img,format=raw,if=ide -m 512M \
    -bios /usr/share/ovmf/OVMF.fd -monitor stdio -s -S
```

**Terminal 2 (GDB connects, breakpoints, continues):**

```bash
x86_64-elf-gdb SamOs64Bit/bin/kernel.bin \
    -ex 'target remote :1234' \
    -ex 'set architecture i386:x86-64' \
    -ex 'break kernel_main' \
    -ex 'break kernel_selftest' \
    -ex 'continue'
```

When the breakpoint hits:

```
(gdb) info registers
(gdb) backtrace
(gdb) info locals
(gdb) print loaded_font
(gdb) next                 # step over
(gdb) step                 # step into
(gdb) continue             # resume
```

**To break on a specific marker probe:**

```
(gdb) break kernel_selftest.c:300
(gdb) continue
```

This is gold for understanding *why* probe N is failing - let it
break right before the `mark_pass / mark_fail` and inspect the
condition variable.

**Useful breakpoints to memorise:**

- `kernel_main` - has the kernel even started
- `panic` - did anything panic
- `multiheap_alloc` - heap path
- `disk_read_block` - disk path
- `paging_init` - paging path
- `process_load_data` - user-program load
- `task_run_first_ever_task` - last thing kernel does before ring 3

---

## 10. Driving a specific QEMU configuration (NVMe, multi-disk, etc.)

The e2e harness's `_lib.sh::boot_and_dump` honors `E2E_EXTRA_QEMU_ARGS`
so you can attach extra devices without editing the lib. For
interactive use, just put the args on the qemu command line:

```bash
# Attach NVMe (matches test 77)
dd if=/dev/zero of=/tmp/nvme.img bs=1M count=4 status=none
qemu-system-x86_64 -machine pc -cpu Skylake-Server \
    -drive file=bin/os.img,format=raw,if=ide -m 512M \
    -bios /usr/share/ovmf/OVMF.fd -monitor stdio \
    -drive file=/tmp/nvme.img,if=none,id=nvm,format=raw \
    -device nvme,drive=nvm,serial=samos-test
```

Then `(qemu) info pci` should list a class 0x01:0x08 device.

```bash
# Attach a second disk to test multi-disk discovery
qemu-system-x86_64 ... \
    -drive file=bin/os.img,format=raw,if=ide \
    -drive file=/tmp/scratch.img,format=raw,if=ide
```

---

## 11. When stuff is wrong - diagnostic decision tree

```
Cold-boot fail (nothing shows after `run.sh`)
├── OVMF doesn't load → check /usr/share/ovmf/OVMF.fd exists
├── EDK2 SamOs.efi missing → run bootstrap.sh, then build.sh
└── Kernel triple-faults early
    ├── attach GDB at -S, break before kernel_main
    ├── single-step through paging_init
    └── inspect cr3 / cr0 / EFER

Kernel boots but slot N is never reached
├── selftest probe is crashing → GDB break at the probe line, examine
├── earlier probe wrote 0 → that one needs investigation first
└── slot was already reached then overwritten by another path

run-all fails one test, passes when re-run alone
└── This is the cold-start race we fixed in commit 850b310
    (boot_and_dump retries once). If it recurs, see the retry loop
    in `_lib.sh`.

`sendkey` doesn't reach the user task
├── This is the DEFER case (test 116). Status: open.
├── Confirm IRQ1 is unmasked at the PIC via `(qemu) info pic`
├── GDB break at `classic_keyboard_handler` and see if it fires on sendkey
└── If yes, the per-process buffer routing is the bug; if no, the IRQ
    chain itself is broken between sendkey and the IDT vector.

Graphics blank/wrong after edit
├── pmemsave 0x6000 + check slot 39 (FB_PAINTED), 51 (FONT), 52 (BORDER)
├── (qemu) screendump and diff against a known-good PPM
└── G16 (graphics_redraw_region clip) is preserved in the source;
    re-introducing the bug would surface here

Heap returns NULL where it shouldn't
├── kmalloc-stress test 88 catches free-list corruption
├── e820 walk failure → slot 34 (entries), 35 (memory total), 72 (type 1)
└── Use `(qemu) info mem` to see the live page tree
```

---

## 12. Quick monitor command crib sheet

```
(qemu) help                  list all commands
(qemu) info <foo>            many things: registers / pci / pic / irq /
                             mem / tlb / kvm / mtree / version / status
(qemu) x/<count><format><size> <addr>   examine memory
                             count: integer
                             format: x b d u o c s i
                             size: b h w g (byte/half/word/giant)
(qemu) pmemsave addr len file      save guest physical memory to host file
(qemu) memsave addr len file       save guest virtual memory (current task's)
(qemu) screendump file             save VGA framebuffer as PPM
(qemu) sendkey <key>               inject PS/2 scancode for a key
(qemu) stop / cont                 halt / resume CPU
(qemu) system_reset                Ctrl+Alt+Del
(qemu) system_powerdown            ACPI shutdown signal
(qemu) device_add / device_del     hotplug
(qemu) drive_add                   attach a drive at runtime
(qemu) gdbserver                   start a gdb stub at runtime
(qemu) singlestep on / off         step-mode
(qemu) log <items>                 turn on QEMU logging
(qemu) quit                        leave
```

---

## 13. Getting an actual REPL (the SHELL.ELF path)

Today `kernel.c` loads BLANK.ELF. To get an interactive shell at
the L110 prompt, do this temporary edit:

```c
/* in SamOs64Bit/src/kernel.c, find:                              */
if(process_load_switch("@:/BLANK.ELF", &user_selftest_proc) == SAMOS_ALL_OK){

/* change to:                                                     */
if(process_load_switch("@:/SHELL.ELF", &user_selftest_proc) == SAMOS_ALL_OK){
```

Then `bash build.sh && bash run.sh`. You should see the L110 prompt
in the QEMU window. Type `blank` to run the BLANK program, `system`
to fire the L118 system command, and so on. Quit by closing QEMU.

Don't commit this swap - the e2e tests depend on BLANK.ELF being
the loaded program (slots 40-50, 80-84). It's a debugging override
only.

---

## 14. Recap - the iteration loop in one paragraph

`build.sh` builds. `run.sh` shows you the kernel running. The QEMU
monitor reads memory, dumps the framebuffer, injects keys, and pauses
the CPU. The bootmarker region at 0x6000 is the kernel-to-host
diagnostic channel; `pmemsave` reads it. `tests64/e2e/run-all.sh`
walks 122 scripts that automate that read-and-assert cycle. GDB
attached over port 1234 is the source-level debugger. Switch BLANK
for SHELL in `kernel.c` for a real interactive REPL.

The fastest iteration loop for a single feature is:

```bash
$EDITOR src/kernel_selftest.c
bash build.sh                                       # 90 s
bash SamOs64Bit/tests64/e2e/<the-targeted-test>.sh  # 22 s
```

Repeat until that one test passes, then `run-all.sh` as the
acceptance gate.
