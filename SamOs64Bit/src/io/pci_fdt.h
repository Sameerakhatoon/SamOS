#ifndef KERNEL_PCI_FDT_H
#define KERNEL_PCI_FDT_H
// S-PAR - a device tree PCIe host bridge for io/pci.c: the
// "pci-host-ecam-generic" node QEMU virt has on aarch64 and riscv64.
// Arch neutral (firmware/fdt.c); the ports that have a device tree link
// it (Makefile PORT_OBJS).
//
// pci_fdt_host_init, from arch_firmware_init (before pci_init):
//   - reg and bus-range: the ECAM window, mapped whole and installed
//     (pci_ecam_install_range);
//   - ranges: the first I/O, 32-bit and 64-bit memory window become
//     pci_set_host_windows, so pci_init assigns the BARs nobody did
//     (pci.h), with the device tree's CPU and bus addresses;
//   - interrupt-map, interrupt-map-mask: pci_intx_gsi's route
//     (pci_set_intx_router). A function's pin is swizzled up to bus 0
//     through each bridge, looked up in the map, and the parent's
//     interrupt specifier handed to `parent_irq`, which turns it into
//     the arch's GSI and MPS INTI flags (aarch64: a GIC SPI's INTID;
//     riscv64: a PLIC source). NULL: no INTx.
// Returns 0, or -ENOENT without such a node (nothing set up).
//
// pci_fdt_msi_id: the MSI specifier (GICv3 ITS: the DeviceID) of a
// function, its requester id through the host's msi-map, or -ENOENT
// (no msi-map entry covers it).

#include <stdint.h>
#include "firmware/fdt.h"
#include "io/pci.h"

typedef int (*pci_fdt_irq_fn)(const struct fdt* f, int parent, const uint32_t* cells, uint32_t ncells,
                              uint16_t* inti_flags);

int pci_fdt_host_init(const struct fdt* f, pci_fdt_irq_fn parent_irq);
int pci_fdt_msi_id(const struct pci_device* d, uint32_t* id);

#endif
