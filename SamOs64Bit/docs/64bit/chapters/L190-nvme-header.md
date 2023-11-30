# Lecture 190 - NVME header

Upstream: PeachOS64BitCourse bf62277.

A header-only commit that lands `src/disk/drivers/nvme.h`:

- PCI class/subclass for NVME (0x01 / 0x08).
- MMIO register offsets (CAP, VS, INTMS/C, CC, CSTS, AQA, ASQ,
  ACQ).
- Doorbell-offset macros parameterised on queue id and the
  doorbell stride from the capability register.
- Admin queue depth constants (64 entries).
- Read / write opcode constants.
- `NVME_COMMAND_BITS` and a builder macro that packs opcode,
  fused, PSDT, and command id into 32 bits.
- Submission / completion queue entry structs (packed).
- `struct nvme_disk_driver_private`: PCI device handle, BAR0
  base, doorbell stride, namespace id, admin + IO submission /
  completion queue rings.
- `nvme_driver_init` forward decl.

## Upstream bugs preserved

1. The completion-queue entry struct is defined under the typo
   `nvme_compeltion_queue_entry` (note the missing `l`), but
   `nvme_disk_driver_private` references
   `nvme_completion_queue_entry*` (correctly spelled). The
   correctly-spelled pointer stays as an incomplete-type
   pointer for now. SamOs adds an explicit forward decl with
   a comment so the audit trail records the gap.
2. `NVME_COMPLETION_QUEUE_STATUS` was written with a trailing
   space after the line-continuation backslash, which is
   rejected under `-Werror`. SamOs strips the stray space so
   the file compiles, with a comment explaining the upstream
   bug.

L192-L195 land the source.

## BIOS test status

Source + link. Build links.
