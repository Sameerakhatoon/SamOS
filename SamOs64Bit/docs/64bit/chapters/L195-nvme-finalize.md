# Lecture 195 - NVME source part 4 (compile + finalize)

Upstream: PeachOS64BitCourse fd23ff0.

## What landed

- `driver.c` includes `drivers/nvme.h` and registers the NVME
  driver in `disk_driver_system_load_drivers`.
- `nvme.c` upstream fixes: the `nvme_disk_driver_umount` typo
  call site is corrected to `nvme_disk_driver_unmount`; the
  dead `slba + nlb;` becomes `slba += nlb;`. SamOs lands both
  fixes and drops the L193 typo shim.
- `nvme.h` macro `NVME_COMPLETION_QUEUE_STATUS` collapses to a
  single line (was already single-line in SamOs since L190).
- `Makefile` gains `nvme.o` in FILES and a recipe.

Kernel build is green.

## Runtime caveat

In QEMU with `-drive file=os.bin,if=ide` the PATA driver
mounts disk0. NVME mount returns -ENOENT cleanly (no NVME
controller advertised by the PIIX3 SB). The kernel keeps
booting. Adding `-device nvme,drive=nvme0 -drive
file=...,if=none,id=nvme0` would exercise the NVME path.

## BIOS test status

Source + link. Build links.
