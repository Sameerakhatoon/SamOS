# Lecture 193 - NVME source part 2

Upstream: PeachOS64BitCourse f6f5eb5.

## What landed in nvme.c

- `nvme_admin_submission_queue_init` /
  `nvme_admin_completion_queue_init`: trivial readiness checks.
- `nvme_cap_doorbell_stride`: read the CAP[DSTRD] field.
- `nvme_admin_send_command`: admin command path that takes an
  LBA + block count and copies into a single SQE then polls the
  CQE phase.
- `nvme_pci_mmio_base`: assemble BAR0+BAR1 into the 64-bit MMIO
  base.
- `nvme_disk_driver_mount_for_device`: per-controller mount.
  Enable bus mastering, allocate private data and the disk
  record, reset the controller (CC.EN=0, wait CSTS.RDY=0),
  allocate admin SQ/CQ in kheap, program AQA/ASQ/ACQ, set CC
  (MPS, CSS, IOSQES/IOCQES, EN=1), wait CSTS.RDY=1, then
  allocate IO queues and create IO CQ+SQ via admin commands.

## pci.h

Public decl renamed from `bus_enable_bus_master` to
`pci_enable_bus_master`. The legacy forwarder body in `pci.c`
stays in place so any leftover caller still links.

## Upstream bugs preserved

1. `nvme_admin_completion_queue_init` checks
   `p->submission_queue.ptr` rather than the completion queue.
2. `nvme_disk_driver_umount` is called inside the mount path
   (missing `n`); the L194 body defines
   `nvme_disk_driver_unmount`. SamOs adds a static inline
   forwarder under the typo'd name so the file builds.
3. `nvme_pci_mmio_base` masks BAR0 with `0xFFFFFFFF0ULL`
   (one stray trailing 0 in the constant) instead of the
   alignment mask the BAR architecture calls for.

All preserved verbatim with code comments.

## BIOS test status

Source + link. Build links.
