# Lecture 177 - PCI header file

Upstream: PeachOS64BitCourse 40aec19.

## What landed

A header-only commit that lands `src/io/pci.h`:

- Configuration-space byte offsets (vendor, device, command,
  status, class, bars, bridge bus fields, interrupt line/pin).
- IO ports `PCI_CFG_ADDRESS` (0xCF8) and `PCI_DATA_ADDRESS`
  (0xCFC).
- `PCI_BASE_CLASS` / `PCI_BASE_SUBCLASS` macros (preserved
  verbatim, see below).
- BAR-type and BAR-flag enums (`PCI_DEVICE_IO_MEMORY` /
  `PCI_DEVICE_IO_PORT`, `PCI_DEVICE_BAR_FLAG_*`).
- Records: `struct pci_device_bar`, `struct pci_address`,
  `struct pci_class_code`, `struct pci_device`.
- PCI Express ECAM range descriptor plus the ECAM init/install
  API.
- Config-space read helpers (`pci_cfg_read_byte/_word/_dword`)
  and a double-declared `pci_cfg_write_dword`.
- Device-enumeration API: `pci_init`, `pci_device_count`,
  `pci_device_get`, `pci_device_base_class`,
  `pci_device_subclass`, `bus_enable_bus_master`.

## Upstream quirks preserved

- `PCI_HEADER_PROG_IF_OFFSET` comment reads
  "Programming interfac e8 bits" (sic: stray space).
- `PCI_BASE_CLASS(bus, slot, func)` omits the `func` argument
  when calling `pci_cfg_read_byte`, while
  `PCI_BASE_SUBCLASS(bus, slot, func)` includes it. Preserved
  verbatim.
- `pci_cfg_write_dword` is declared twice in upstream's header,
  once taking `uint32_t value` and once taking `uint16_t value`.
  Preserved verbatim; the L178+ source files will define one or
  both.

These will compile as no-ops because the symbols are never
called yet; later lectures land the bodies.

## BIOS test status

Source + link. Build links.
