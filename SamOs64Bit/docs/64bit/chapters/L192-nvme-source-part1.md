# Lecture 192 - NVME source part 1

Upstream: PeachOS64BitCourse 3ae73a8.

`src/disk/drivers/nvme.c` lands with:

- 32-bit / 64-bit MMIO accessors (`nvme_read32`, `nvme_write32`,
  `nvme_read64`, `nvme_write64`).
- `nvme_disk_driver_read_reg` / `_write_reg`: disk-side wrappers
  that resolve the private data and panic on NULL.
- `nvme_admin_cmd_raw`: copy a submission entry into the admin
  SQ, ring the SQ tail doorbell, spin-poll the CQ phase bit
  (pause hint), advance the CQ head doorbell. Returns -EIO on
  status != 0, -ETIMEOUT on poll exhaustion.
- `nvme_create_io_cq` / `nvme_create_io_sq`: queue-creation
  shortcuts that call `nvme_admin_cmd_raw` with the right
  CDW10/11.
- `nvme_pci_private_new` / `nvme_pci_device_private_free`:
  kzalloc/kfree the per-disk private.
- `nvme_pci_device`: PCI class/subclass match.
- `nvme_map_mmio_once`: identity-map BAR0 (or two pages if
  size is zero) with PCD into the kernel paging desc.
- Tail/head helper inlines.

`nvme.h` renames `nvme_compeltion_queue_entry` to
`nvme_completion_queue_entry`. SamOs lands the same fix in the
same commit.

The file is not added to the Makefile yet; L195 lands the
build entry. Until then nothing references the new symbols at
link time.

## BIOS test status

Source + link. Build links.
