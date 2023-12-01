# Lecture 194 - NVME source part 3

Upstream: PeachOS64BitCourse 1ded92e.

## What landed

- `nvme_io_submit_and_poll`: build a one-page or two-page PRP
  list, post to IO submission queue 1, ring the SQ doorbell,
  spin-poll the IO CQ for the phase flip, advance head.
- `nvme_disk_driver_mount`: enumerate PCI devices, call
  `nvme_disk_driver_mount_for_device` on each NVME class match.
- `nvme_disk_driver_unmount`: free the per-disk private.
- `nvme_disk_driver_read` / `_write`: chunk by 16-sector
  windows and forward to `nvme_io_submit_and_poll`.
- `nvme_disk_driver_mount_partition`: `disk_create_new` with
  SAMOS_DISK_TYPE_PARTITION.
- `nvme_driver` vtable + `nvme_driver_init` accessor.

## Upstream bug preserved

`nvme_disk_driver_read` advances the LBA with a dead expression
`slba + nlb;` instead of `slba += nlb;`. Every chunk after the
first re-reads sector 0. SamOs preserves the upstream line
verbatim under an explicit `(void)` cast so the project's
`-Werror=unused-value` build flag does not reject it. The write
path uses `slba += nlb` (correct) - upstream wrote it that way.

L195 adds the build wiring.

## BIOS test status

Source + link. Build links.
