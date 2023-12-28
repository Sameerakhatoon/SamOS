# SamOs Module 2 - end-to-end runtime testing

This is the third layer of testing in the project, the one
`debugging-history.md` G45 called out as missing.

| Layer | Coverage | Lives in |
|---|---|---|
| Per-lecture source audits | grep + build | `tests64/Lxx-*.sh` |
| Manual feature walkthrough | eyeball under QEMU | `end-to-end-feature-tests.md` |
| **Automated runtime e2e** | **kernel actually runs each subsystem init** | **`tests64/e2e/`** |

The e2e tests build the kernel, boot it under QEMU (UEFI when
available, BIOS fallback otherwise), and verify each subsystem
checkpoint via boot markers the kernel writes to fixed physical
memory.

---

## TL;DR

```bash
cd SamOs64Bit
bash build.sh

# Run a single e2e check
bash tests64/e2e/01-boot.sh

# Run the full sweep (~90 seconds)
bash tests64/e2e/run-all.sh
```

Each test prints `PASS: e2e/NN ...` on success.

---

## How it works

### 1. The kernel writes boot markers

`src/bootmarker.h` defines a small set of stage ids:

```
BM_STAGE_KERNEL_MAIN      = 1
BM_STAGE_PAGING_SWITCHED  = 2
BM_STAGE_MULTIHEAP_READY  = 3
BM_STAGE_IDT_LIVE         = 4
BM_STAGE_PCI_SCANNED      = 5  (value = device count)
BM_STAGE_DISK_INITED      = 6  (value = primary_fs_disk->id + 1)
BM_STAGE_FS_RESOLVED      = 7
BM_STAGE_GRAPHICS_UP      = 8  (value = framebuffer base low 24 bits)
BM_STAGE_PROCESS_INIT     = 9
BM_STAGE_KEYBOARD_INIT    = 10
BM_STAGE_WINDOW_STAGE2    = 11
BM_STAGE_ISR80H_READY     = 12 (value = syscall count)
```

`kernel.c` calls `boot_marker_set(stage, value)` at each
subsystem checkpoint. The marker is one 64-bit MMIO write to
`0x6000 + stage*8`, costing about 100 ns each.

Magic layout per slot:

```
bits 63..32: 0xB007C0DE  (constant prefix - "Boot Code")
bits 31..24: stage id    (echoes the slot, so a missed write
                          is distinguishable from a different
                          stage's value)
bits 23..0 : caller value (0 if the stage is just "I got here")
```

A test for "did stage 5 (PCI) run, and were >0 devices found?"
verifies:

1. Bytes 4..7 at `0x6000 + 5*8` equal `0xB007C0DE`
2. Byte 3 at the same slot equals `0x05`
3. Bytes 0..2 (the value) are > 0

If the kernel triple-faulted before stage 5, the slot still
holds whatever was there when the kernel handed off (usually
zeros or UEFI scratch). The high half check fails immediately.

### 2. The test harness scrapes the markers

After kernel boot, QEMU monitor's `pmemsave 0x6000 128 "/tmp/..."`
dumps the marker region to a file. The test reads the file with
`od -tx4` (32-bit hex words) and compares against the expected
pattern.

`tests64/e2e/_lib.sh` provides:

| Function | What it does |
|---|---|
| `boot_and_dump <seconds>` | Boot QEMU, run kernel for N seconds, pmemsave the marker region |
| `marker_high <stage>` | High 32 bits of slot[stage] (the magic prefix) |
| `marker_low <stage>` | Low 32 bits of slot[stage] (top byte = stage id, bottom 24 = value) |
| `marker_value <stage>` | Just the value (bottom 24 bits as decimal) |
| `expect_stage_reached <stage> [name]` | Assert magic + stage id are correct |
| `expect_stage_value <stage> <op> <expected> [name]` | Assert value passes the bash `[ a OP b ]` test |

### 3. Boot mode auto-detection

`e2e_select_image` prefers the UEFI image (`../bin/os.img`,
built by the top-level `build.sh`) when both it and OVMF
firmware exist. It falls back to the BIOS image
(`SamOs64Bit/bin/os.bin`).

The boot mode is exposed as `$E2E_BOOT_MODE` (`uefi` or
`bios`) so individual tests can tighten or relax assertions:

```bash
if [ "$E2E_BOOT_MODE" = "uefi" ]; then
    expect_stage_value 8 -gt 0 "framebuffer base non-zero (UEFI GOP)"
fi
```

---

## Setting up the UEFI boot path (one time)

Prerequisites:

```bash
sudo apt install -y ovmf uuid-dev acpica-tools nasm build-essential
```

Then:

```bash
cd ~
git clone --depth=1 --recurse-submodules --shallow-submodules \
    https://github.com/tianocore/edk2.git
cd edk2
make -C BaseTools                 # ~3 - 5 minutes
ln -s ~/projects/samos MdeModulePkg/Application/SamOs

# Register the SamOs module in MdeModulePkg.dsc (one-line patch):
sed -i 's|MdeModulePkg/Application/HelloWorld/HelloWorld.inf|&\n  MdeModulePkg/Application/SamOs/SamOs.inf|' \
    MdeModulePkg/MdeModulePkg.dsc

. edksetup.sh BaseTools
build -p MdeModulePkg/MdeModulePkg.dsc -m MdeModulePkg/Application/SamOs/SamOs.inf -a X64
```

Then back in the SamOs project root:

```bash
cd ~/projects/samos
./build.sh        # composes the 700 MiB GPT image at bin/os.img
./run.sh          # qemu with OVMF
```

If the `build.sh` step needs `sudo` (loop-mount + parted), run
it once interactively. The e2e tests do not need to re-run
`build.sh`; they re-use whatever `bin/os.img` is already there.

---

## Writing a new e2e test

Template:

```bash
#!/usr/bin/env bash
# e2e/NN-name.sh - what this confirms.
. "$(dirname "$0")/_lib.sh"

boot_and_dump 6

expect_stage_reached <STAGE_ID> "human-readable check"
expect_stage_value   <STAGE_ID> -gt 0 "value sanity check"

echo "PASS: e2e/NN name"
```

Numbering: keep the prefix sorted (`run-all.sh` walks
`[0-9]*-*.sh` in glob order, which sorts lexically). The
existing 01..12 mirror the subsystem boot order so the sweep
output reads top-down through the kernel's init path.

If a new test needs a fresh marker stage, add the constant in
`src/bootmarker.h` AND a `boot_marker_set(...)` call at the
relevant point in `kernel.c` (or wherever the subsystem
initializes). Bump `BM_STAGE_MAX` and the dump size in
`_lib.sh` if you exceed `BOOT_MARKER_BASE + 128 bytes`.

---

## Reading a failure

Sweep output for a failing test:

```
FAIL 05-pci.sh
    [e2e] kernel_main: reached (raw hi=b007c0de lo=01000000)
    [e2e] paging: reached (raw hi=b007c0de lo=02000000)
    [e2e] multiheap: reached (raw hi=b007c0de lo=03000000)
    [e2e] IDT live: reached (raw hi=b007c0de lo=04000000)
    FAIL: PCI scanned: high half is 00000000, expected b007c0de
```

That tells you the kernel reached IDT init but triple-faulted
inside `pci_init`. Re-run with a longer timeout if you suspect
the test was just impatient:

```bash
# Edit the boot_and_dump argument in the test, or override
# in a one-off:
E2E_BOOT_TIMEOUT=15 bash tests64/e2e/05-pci.sh
```

For deeper diagnosis, manually:

```bash
qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive file=bin/os.img,format=raw,if=ide \
    -m 512M -accel tcg -display gtk \
    -d int,cpu_reset -D /tmp/qemu.log
```

Watch the framebuffer; check `/tmp/qemu.log` for register
dumps at faults.

---

## What this catches that source-level tests miss

| Class of bug | Per-lecture grep tests | e2e tests |
|---|---|---|
| Missing source file |  | (implicit, build fails) |
| Symbol not in source |  | (implicit, link fails) |
| Build link error |  |  |
| Symbol in kernel.bin |  (via `grep -a`) | (implicit) |
| Subsystem init triple-faults |  |  |
| `if(!driver)` fallback path broken |  |  |
| Preserved-typo regression |  |  |
| Boot-order bug (G36) |  |  |
| `task_sleep(*x)` style logic bug |  (build still passes) |  |

The full sweep is the first thing that actually runs the
kernel end-to-end since the L74 BIOS deviation was introduced.

---

## See also

- `src/bootmarker.h` - the magic / layout reference
- `tests64/e2e/_lib.sh` - shared helper functions
- `docs/64bit/debugging-history.md` - the gotchas e2e is now
  catching, in particular G36 (keyboard boot order) and the
  G43/G44/G45 cluster (BIOS rescue rationale)
- `docs/64bit/running-tests.md` - per-lecture source-level test runner
- `docs/64bit/end-to-end-feature-tests.md` - manual walkthrough
  for features e2e cannot easily automate (interactive
  shell, mouse, etc)
