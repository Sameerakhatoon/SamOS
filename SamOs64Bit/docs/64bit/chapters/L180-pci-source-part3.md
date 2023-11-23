# Lecture 180 - PCI source part 3

Upstream: PeachOS64BitCourse 6e0edb5.

## What landed

`pci.c`:

- `pci_scan_bus`: recursive bus walk. For each (bus, dev,
  func) with a valid vendor id, allocate a `pci_device`,
  capture vendor/device/class, size BARs, push into the global
  vector. Type-1 PCI-PCI bridges recurse into their secondary
  bus.
- `pci_init`: allocate `pci_device_vector` and scan bus 0.
- `pci_device_count`, `pci_device_get`,
  `pci_device_base_class`, `pci_device_subclass`: trivial
  accessors.
- `pci_enable_upstream_path` (static): walk parent bridges and
  OR `BME|MSE|IOSE` into each command register so the path
  passes IO/MEM/master cycles down to the leaf device.
- `pci_enable_bus_master`: enable BME plus the IO/MEM bits the
  device's BARs actually need, then propagate upstream.
- `bus_enable_bus_master`: forwarder so the L177 header decl
  links. Upstream's decl uses `bus_enable_bus_master` while
  the body is `pci_enable_bus_master`; SamOs keeps both names
  and links them with a one-line wrapper.

`pci.c` cleanups upstream did this lecture:

- Removed the `pci_cfg_reaD_dword` typo at the call site (the
  L179 shim is gone now).

`pci.h`: the duplicate `pci_cfg_write_dword` decl is corrected
to `pci_cfg_write_word`.

`kernel.c`: includes `io/pci.h` and calls `pci_init()` after
`idt_init()`.

`isr80h/window.c`: upstream renames the L173 typo
`isr90h_command24_update_window_title` back to
`isr80h_command24_update_window_title`. SamOs follows the fix
and updates the call site cast to `(uintptr_t)`.

`graphics/window.c`: upstream fixes
`keybaord_register_handler(NULL, ...)` (the L175 typo plus
missing semicolon). SamOs already shipped the correct call at
L175, so the source is already there. The upstream commit
also adds a `struct keyboard*` arg to
`window_keyboard_event_listener_on_event_capslock_change`;
SamOs already wrote the three-arg signature at L175.

`keyboard.c`: upstream fixes `#include "heap/kheap.h"` to
`memory/heap/kheap.h` and adds `memory/memory.h`. SamOs
already shipped both at L174.

`Makefile`: `pci.o` joins FILES and gets a recipe.

## BIOS test status

Source + link. Build links.
