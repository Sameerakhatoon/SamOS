# Lecture 71 - restructure for UEFI boot path

**Source commit (PeachOS64BitCourse):** `34656ee`
**SamOs commit:** L71 on `module1-64bit` branch
**Regression test:** `tests64/L71-restructure-uefi-stub.sh`

## Note on numbering

PeachOS64 upstream skips L69 and L70 (no commits). The
sequence is L68 -> L71 -> L73 -> L74 -> ... L72 is also
skipped. Presumably those were discussion / overview lectures
with no code.

## Why this chapter exists

Upstream begins a parallel UEFI boot path. The existing
kernel + everything else moves into a `PeachOS64Bit/`
subdirectory, and the top level becomes an EDK2 UEFI
application source tree.

The eventual goal (L73 onward) is that the same kernel.asm
boots from either BIOS (boot.bin -> kernel.bin -> kernel.asm)
or UEFI (PeachOS.efi -> kernel.asm).

## What we moved

```
src/ programs/ Makefile build.sh hello.txt tests64/ docs/
    -> SamOs64Bit/
```

The inner `SamOs64Bit/build.sh` is unchanged; it still
produces `SamOs64Bit/bin/os.bin` via the BIOS pipeline.

`PROGRESS.md`, `README.md`, and the original 32-bit `tests/`
stay at top level (they're project-meta, not kernel code).

## What we added at top level

| File | Role |
|---|---|
| `SamOs.c` | UEFI application entry point. Prints "SamOs UEFI bootloader." in a forever-loop. |
| `SamOs.inf` | EDK2 module manifest. UEFI_APPLICATION type, X64 architecture. |
| `SamOs.uni` | Module-level localised strings. |
| `SamOsStr.uni` | Help string for `-?` from the UEFI Shell. |
| `SamOsExtra.uni` | Vendor/extra metadata block. |
| `build.sh` | Top-level orchestrator. Runs EDK2 build if available, then the inner `SamOs64Bit/build.sh`, then composes a 700 MiB GPT-partitioned disk image (UEFI path only). |
| `run.sh` | QEMU launcher with `-bios /usr/share/ovmf/OVMF.fd` for the UEFI boot path. |

## What's NOT done

The `.efi` build requires:
- EDK2 cloned at `$HOME/edk2/` with BaseTools built
- This project symlinked under `MdeModulePkg/Application/SamOs/`
- OVMF firmware (Debian/Ubuntu: `apt install ovmf`)
- The disk image composition steps use `sudo losetup`, `parted`,
  `mkfs.vfat`, so the build needs root

None of these are CI-friendly. The top-level `build.sh`
detects whether EDK2 is set up and falls back to building
only the inner BIOS image when it isn't.

## SamOs vs upstream renames

- Subdirectory: `PeachOS64Bit/` -> `SamOs64Bit/`
- UEFI sources: `PeachOS*.c|inf|uni` -> `SamOs*.c|inf|uni`
- Filesystem volume label: `PEACH` -> `SAMOS`
- The .efi filename: `PeachOS.efi` -> `SamOs.efi`
- The `bin/PeachOS.efi` artefact -> `bin/SamOs.efi`

## How we verified

`tests64/L71-restructure-uefi-stub.sh` (lives at
`SamOs64Bit/tests64/`):
1. The top-level UEFI files exist.
2. `SamOs64Bit/build.sh` still produces a working BIOS
   image that boots to "user enter".

Both pass.

## Forward look

L73 - UEFI bootloader loads kernel.asm. The .efi reads
kernel.asm from the FAT16 ESP and jumps to it. This needs
kernel.asm changes (L74) to survive the UEFI hand-off
(different machine state than what BIOS leaves).

L75 - re-detect free memory regions. Under UEFI, the E820
BIOS call doesn't exist; the UEFI memory map serves the
same purpose but has a different format. The kernel needs
to handle both.

L76 - the "unmapped kernel" problem. UEFI loads the kernel
to wherever it wants (not necessarily 0x100000). The
kernel's paging setup needs to map its own RIP.

L77 - GPT/MBR partition tables. UEFI disks use GPT instead
of MBR. The kernel grows a partition-table reader.

L78 - virtual disks. Abstraction so disk_search_and_init
can treat each partition as a virtual disk.
