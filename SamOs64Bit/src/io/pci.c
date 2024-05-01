// Lecture 178 - PCI config-space access. Part one of four.
//
// This file owns:
//   * the ECAM (PCI Express memory-mapped config) range table
//     populated from the ACPI MCFG table,
//   * a legacy 0xCF8/0xCFC port-based config-space helper,
//   * the byte/word/dword config reads that prefer ECAM when a
//     matching range is installed and fall back to legacy.
//
// L179 will add bus enumeration; L180/L181 round it out.

#include "io/pci.h"
#include "memory/heap/kheap.h"
#include "lib/vector/vector.h"
#include "status.h"
#include "kernel.h"
#include "acpi/acpi.h"
#include "io/serial.h"
#include "memory/memory.h"
#include "arch/arch.h"
#include "string/string.h"
#include <stdbool.h>

// ECAM globals.
#define PCI_ECAM_MAX_RANGES  8
#define PCI_ECAM_BUS_SHIFT   20  // 1 MB per bus
#define PCI_ECAM_DEV_SHIFT   15  // 32 KB per device
#define PCI_ECAM_FUNC_SHIFT  12  // 4 KB per function

static struct pci_ecam_range g_ecam[PCI_ECAM_MAX_RANGES];
static int  g_ecam_count   = 0;
static bool g_ecam_enabled = false;

static void pci_scan_bus(uint8_t bus, struct pci_device* parent_bridge);
static void pci_size_bar(uint8_t bus, uint8_t dev, uint8_t func, struct pci_device* device);

struct vector* pci_device_vector = NULL;

static pci_ecam_map_fn_t g_ecam_mapper;

// A whole ECAM range is 1 MiB per bus (256 MiB on q35);
// mapping it all in 4 KiB pages costs 128 page tables. Each bus is
// mapped on its first access instead. The mapper must map linearly
// (the direct map does), so the range keeps one virt_base.
static bool pci_ecam_map_bus(struct pci_ecam_range* r, uint8_t bus){
    uint8_t i = bus - r->start_bus;
    if(r->mapped[i / 8] & (1 << (i % 8))){
        return true;
    }
    size_t off = (size_t)i << PCI_ECAM_BUS_SHIFT;
    void* v = g_ecam_mapper ? g_ecam_mapper(r->phys_base + off, 1u << PCI_ECAM_BUS_SHIFT) : NULL;
    if(v != (uint8_t*)r->virt_base + off){
        return false;
    }
    r->mapped[i / 8] |= 1 << (i % 8);
    return true;
}

// Lecture 178 - ECAM range lookup. Upstream uses
// `bus < r->start_bus || bus < r->end_bus`; the second half should
// be `bus > r->end_bus`, and with it every bus but the last missed
// its range. The platform work fixes it now that ECAM is really used.
// A bus outside every range falls back to the 0xCF8 ports.
static inline volatile uint8_t* pci_ecam_cfg_ptr(uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset){
    if(!g_ecam_enabled){
        return NULL;
    }
    if(offset >= 4096){
        return NULL;
    }
    for(int i = 0; i < g_ecam_count; ++i){
        struct pci_ecam_range* r = &g_ecam[i];
        if(bus < r->start_bus || bus > r->end_bus){
            continue;
        }
        if(!pci_ecam_map_bus(r, bus)){
            return NULL;
        }
        size_t bus_off  = ((size_t)(bus - r->start_bus)) << PCI_ECAM_BUS_SHIFT;
        size_t dev_off  = ((size_t)slot)                 << PCI_ECAM_DEV_SHIFT;
        size_t func_off = ((size_t)func)                 << PCI_ECAM_FUNC_SHIFT;
        return (volatile uint8_t*)r->virt_base + bus_off + dev_off + func_off + (size_t)offset;
    }
    return NULL;
}

int pci_ecam_install_range(uint16_t seg_group, uint64_t phys_base,
                           uint8_t start_bus, uint8_t end_bus, void* virt_base){
    if(g_ecam_count >= PCI_ECAM_MAX_RANGES){
        return -ENOMEM;
    }
    if(!virt_base || start_bus > end_bus){
        return -EINVAL;
    }
    g_ecam[g_ecam_count].seg_group = seg_group;
    g_ecam[g_ecam_count].phys_base = phys_base;
    g_ecam[g_ecam_count].start_bus = start_bus;
    g_ecam[g_ecam_count].end_bus   = end_bus;
    g_ecam[g_ecam_count].virt_base = virt_base;
    // A caller that installs a range has mapped all of it.
    memset(g_ecam[g_ecam_count].mapped, 0xFF, sizeof(g_ecam[g_ecam_count].mapped));
    g_ecam_count++;
    g_ecam_enabled = true;
    return 0;
}

#pragma pack(push, 1)
typedef struct {
    char     signature[4]; // MCFG signature
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t asl_compiler_id;
    uint32_t asl_compiler_revision;
} acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t hdr;
    uint64_t          reserved;
} acpi_mcfg_t;

typedef struct {
    uint64_t base_address;        // ECAM base
    uint16_t pci_segment_group;   // PCI segment
    uint8_t  start_bus_number;
    uint8_t  end_bus_number;
    uint32_t reserved;
} acpi_mcfg_entry_t;
#pragma pack(pop)

int pci_ecam_init_from_mcfg(void* mcfg_table_virt, pci_ecam_map_fn_t mapper){
    if(!mcfg_table_virt || !mapper){
        return -EINVAL;
    }
    acpi_mcfg_t* mcfg = (acpi_mcfg_t*)mcfg_table_virt;
    if(mcfg->hdr.length < sizeof(acpi_mcfg_t)){
        return -EINVAL;
    }

    size_t entries_bytes = (size_t)mcfg->hdr.length - sizeof(acpi_mcfg_t);
    size_t count = entries_bytes / sizeof(acpi_mcfg_entry_t);
    acpi_mcfg_entry_t* ent = (acpi_mcfg_entry_t*)((uint8_t*)mcfg + sizeof(acpi_mcfg_t));
    int installed = 0;
    g_ecam_mapper = mapper;
    for(size_t i = 0; i < count; i++){
        uint8_t start = ent[i].start_bus_number;
        uint8_t end   = ent[i].end_bus_number;
        // Only segment 0 is reachable through the
        // (bus, slot, func) API.
        if(start > end || ent[i].pci_segment_group != 0){
            continue;
        }
        uint64_t phys = ent[i].base_address + ((uint64_t)start << PCI_ECAM_BUS_SHIFT);
        // Map the first bus now, the rest on demand
        // (pci_ecam_map_bus).
        void* v = mapper(phys, 1u << PCI_ECAM_BUS_SHIFT);
        if(!v){
            continue;
        }
        if(pci_ecam_install_range(ent[i].pci_segment_group, phys, start, end, v) == 0){
            struct pci_ecam_range* r = &g_ecam[g_ecam_count - 1];
            memset(r->mapped, 0, sizeof(r->mapped));
            r->mapped[0] = 1;
            installed++;
        }
    }
    return (installed > 0) ? 0 : -ENOENT;
}

bool pci_ecam_available(void){
    return g_ecam_enabled;
}

// Lecture 178 - dword read. Prefer ECAM; fall back to 0xCF8/CFC.
uint32_t pci_cfg_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset){
    uint8_t off = offset & ~0x3u;
    volatile uint8_t* p = pci_ecam_cfg_ptr(bus, slot, func, off);
    if(p){
        return *(volatile uint32_t*)p;
    }
    // The legacy mechanism is the arch's (x86: 0xCF8/0xCFC).
    return arch_pci_cfg_read32(bus, slot, func, off);
}

uint16_t pci_cfg_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset){
    uint8_t off = offset & ~0x3u;
    volatile uint8_t* p = pci_ecam_cfg_ptr(bus, slot, func, off);
    if(p){
        uint32_t v = *(volatile uint32_t*)p;
        return (uint16_t)((v >> ((offset & 2u) * 8)) & 0xFFFFu);
    }
    uint32_t v = arch_pci_cfg_read32(bus, slot, func, off);
    return (uint16_t)((v >> ((offset & 2u) * 8)) & 0xFFFFu);
}

uint8_t pci_cfg_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset){
    uint32_t v = pci_cfg_read_dword(bus, slot, func, offset & ~0x3u);
    return (uint8_t)(v >> ((offset & 3u) * 8));
}

// Lecture 179 - dword write. ECAM preferred, legacy fallback.
void pci_cfg_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value){
    uint8_t off = offset & ~0x3u;
    volatile uint8_t* p = pci_ecam_cfg_ptr(bus, slot, func, off);
    if(p){
        *(volatile uint32_t*)p = value;
        return;
    }
    arch_pci_cfg_write32(bus, slot, func, off, value);
}

// Lecture 179 - word write. Reads the surrounding dword, masks
// in the new 16 bits, writes the dword back. Word lives in the
// low half when offset is dword-aligned, high half otherwise.
void pci_cfg_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value){
    uint8_t off = offset & ~0x3u;
    volatile uint8_t* p = pci_ecam_cfg_ptr(bus, slot, func, off);
    if(p){
        volatile uint32_t* pdw = (volatile uint32_t*)p;
        uint32_t tmp = *pdw;
        if((offset & 2u) == 0){
            tmp = (tmp & 0xFFFF0000u) | (uint32_t)value;
        } else {
            tmp = (tmp & 0x0000FFFFu) | ((uint32_t)value << 16);
        }
        *pdw = tmp;
        return;
    }
    // Legacy PCI.
    uint32_t tmp = arch_pci_cfg_read32(bus, slot, func, off);
    if((offset & 2u) == 0){
        tmp = (tmp & 0xFFFF0000u) | (uint32_t)value;
    } else {
        tmp = (tmp & 0x0000FFFFu) | ((uint32_t)value << 16);
    }
    arch_pci_cfg_write32(bus, slot, func, off, tmp);
}

// Lecture 180 - upstream L179 had a `pci_cfg_reaD_dword` typo.
// L180 fixes it; SamOs drops the shim now that the call site
// uses the correct identifier.

// Lecture 179 - size every BAR on a (bus, dev, func). Disable
// IO/MEM/master decode during the size probe, write all-ones,
// read back the writeable bits, restore. Handles type-0 (6
// BARs) and type-1 (2 BARs) headers, 64-bit BAR pairs, and IO
// vs memory BARs.
//
// Upstream typo'd the function name plural: the L178 forward
// decl was `pci_size_bar` (singular) while the L179 body lands
// as `pci_size_bars`. Preserved verbatim.
static void pci_size_bars(uint8_t bus, uint8_t dev, uint8_t func, struct pci_device* device){
    // Note: upstream literal uses `0X7Fu` (capital X). gcc
    // accepts both. Preserved verbatim.
    uint8_t hdr = pci_cfg_read_byte(bus, dev, func, PCI_HEADER_HEADER_TYPE_OFFSET) & 0X7Fu;
    int bar_count = (hdr == 0x00) ? 6 : (hdr == 0x01 ? 2 : 0);

    uint16_t cmd_orig = pci_cfg_read_word(bus, dev, func, PCI_HEADER_COMMAND_OFFSET);
    // Disable IO, MEM and bus mastering during bar sizing.
    pci_cfg_write_word(bus, dev, func, PCI_HEADER_COMMAND_OFFSET,
                       (uint16_t)(cmd_orig & ~(uint16_t)0x0007));

    for(int i = 0; i < bar_count; ++i){
        struct pci_device_bar* bar = &device->bars[i];
        bar->flags = 0;
        bar->addr  = 0;
        bar->size  = 0;
        uint8_t off = PCI_HEADER_BAR0_OFFSET + i * 4;

        uint32_t lo_orig = pci_cfg_read_dword(bus, dev, func, off);
        bool is_io = (lo_orig & 0x1u) != 0;
        bar->type = is_io ? PCI_DEVICE_IO_PORT : PCI_DEVICE_IO_MEMORY;
        if(!is_io && (lo_orig & 0x8u)){
            bar->flags |= PCI_DEVICE_BAR_FLAG_PREFETCHABLE;
        }

        if(is_io){
            uint32_t base = lo_orig & ~0x3u;
            pci_cfg_write_dword(bus, dev, func, off, 0xFFFFFFFFu);
            uint32_t szv = pci_cfg_read_dword(bus, dev, func, off);
            pci_cfg_write_dword(bus, dev, func, off, lo_orig);
            uint32_t masked = (szv & ~0x3u);
            if(!masked){
                bar->size = 0;
                continue;
            }
            uint64_t mask = (uint64_t)masked;
            bar->addr = (uint64_t)base;
            bar->size = (~mask) + 1u;
        } else {
            uint32_t type_bits = (lo_orig >> 1) & 0x3u;
            bool is64 = (type_bits == 0x2u);
            if(is64 && (i + 1) < bar_count){
                uint32_t hi_orig = pci_cfg_read_dword(bus, dev, func, off + 4);
                pci_cfg_write_dword(bus, dev, func, off,     0xFFFFFFFFu);
                pci_cfg_write_dword(bus, dev, func, off + 4, 0xFFFFFFFFu);
                uint32_t lo_sz = pci_cfg_read_dword(bus, dev, func, off);
                uint32_t hi_sz = pci_cfg_read_dword(bus, dev, func, off + 4);
                pci_cfg_write_dword(bus, dev, func, off,     lo_orig);
                pci_cfg_write_dword(bus, dev, func, off + 4, hi_orig);

                uint64_t base64 = ((uint64_t)hi_orig << 32) | (uint64_t)(lo_orig & ~0xFu);
                uint64_t mask64 = ((uint64_t)hi_sz   << 32) | (uint64_t)(lo_sz   & ~0xFu);
                if(mask64 == 0){
                    bar->size = 0;
                    goto restore64;
                }
                bar->addr = base64;
                bar->size = (~mask64) + 1u;
                bar->flags |= PCI_DEVICE_BAR_FLAG_64BIT;
restore64:
                ++i;
                if(i < bar_count){
                    struct pci_device_bar* ext = &device->bars[i];
                    ext->type  = bar->type;
                    ext->flags = PCI_DEVICE_BAR_FLAG_IS_EXT;
                    ext->addr  = base64 >> 32;
                    ext->size  = 0;
                }
            } else {
                uint32_t base = lo_orig & ~0xFu;
                pci_cfg_write_dword(bus, dev, func, off, 0xFFFFFFFFu);
                // Lecture 180 - upstream fixed the
                // pci_cfg_reaD_dword typo here.
                uint32_t szv = pci_cfg_read_dword(bus, dev, func, off);
                pci_cfg_write_dword(bus, dev, func, off, lo_orig);

                uint32_t masked = (szv & ~0xFu);
                if(!masked){
                    bar->size = 0;
                    continue;
                }
                uint64_t mask = (uint64_t)masked;
                bar->addr = (uint64_t)base;
                bar->size = (~mask) + 1u;
            }
        }
    }

    pci_cfg_write_word(bus, dev, func, PCI_HEADER_COMMAND_OFFSET, cmd_orig);
}

// Lecture 180 - recursive bus scan. For each slot at bus, read
// the vendor id; if valid, walk function 0..7 (multi-function
// when the L141 bit is set in the header type), allocate a
// pci_device record per function, capture vendor/device/class,
// size the BARs, and push into the global device vector.
//
// A type-1 (PCI-PCI bridge) header has primary/secondary/
// subordinate bus numbers we read and chase by recursing into
// the secondary bus.
static void pci_scan_bus(uint8_t bus, struct pci_device* parent_bridge){
    for(int dev = 0; dev < 32; ++dev){
        uint16_t vendor0 = pci_cfg_read_word(bus, dev, 0, PCI_HEADER_VENDOR_OFFSET);
        if(vendor0 == 0xFFFF){
            continue;
        }
        uint8_t hdr0 = pci_cfg_read_byte(bus, dev, 0, PCI_HEADER_HEADER_TYPE_OFFSET);
        int func_limit = (hdr0 & 0x80) ? 8 : 1;
        for(int func = 0; func < func_limit; ++func){
            uint16_t vendor = pci_cfg_read_word(bus, dev, func, PCI_HEADER_VENDOR_OFFSET);
            if(vendor == 0xFFFF){
                continue;
            }
            struct pci_device* device = kzalloc(sizeof(*device));
            if(!device){
                panic("Out of memory for PCI devices\n");
            }
            device->addr.bus  = (uint8_t)bus;
            device->addr.slot = (uint8_t)dev;
            device->addr.func = (uint8_t)func;
            device->vendor    = vendor;
            device->device_id = pci_cfg_read_word(bus, dev, func, PCI_HEADER_DEVICE_OFFSET);

            uint32_t classreg = pci_cfg_read_dword(bus, dev, func, PCI_HEADER_REVISION_ID_OFFSET);
            device->class.base     = (classreg >> 24) & 0xFF;
            device->class.subclass = (classreg >> 16) & 0xFF;

            device->parent_bridge   = parent_bridge;
            device->is_bridge       = false;
            device->primary_bus     = bus;
            device->secondary_bus   = 0;
            device->subordinate_bus = 0;
            pci_size_bars(bus, dev, func, device);

            vector_push(pci_device_vector, &device);
            uint8_t hdr = pci_cfg_read_byte(bus, dev, func, PCI_HEADER_HEADER_TYPE_OFFSET) & 0x7Fu;
            if(hdr == 0x01){
                device->is_bridge       = true;
                device->primary_bus     = pci_cfg_read_byte(bus, dev, func, PCI_BRIDGE_PRIMARY_BUS_OFFSET);
                device->secondary_bus   = pci_cfg_read_byte(bus, dev, func, PCI_BRIDGE_SECONDARY_BUS_OFFSET);
                device->subordinate_bus = pci_cfg_read_byte(bus, dev, func, PCI_BRIDGE_SUBORDINATE_BUS_OFFSET);
                if(device->secondary_bus != 0 && device->secondary_bus <= device->subordinate_bus){
                    pci_scan_bus(device->secondary_bus, device);
                }
            }
        }
    }
}

static void* pci_ecam_map(uint64_t phys, size_t size){
    return paging_map_mmio(phys, size, PAGING_CACHE_DISABLED);
}

// Lecture 180 - boot-time entry point. Allocates the device
// vector and scans bus 0.
// Config space goes through ECAM when ACPI has an MCFG
// (q35), else through the 0xCF8/0xCFC ports (pc has no MCFG).
int pci_init(void){
    pci_device_vector = vector_new(sizeof(struct pci_device*), 16, 0);
    if(!pci_device_vector){
        return -ENOMEM;
    }
    struct acpi_sdt_header* mcfg = acpi_find_table("MCFG", 0);
    // Or the arch installed the device tree's ECAM window.
    if(pci_ecam_available() || (mcfg && pci_ecam_init_from_mcfg(mcfg, pci_ecam_map) == 0)){
        serial_write("pci: config access ECAM, base ");
        serial_write_hex(g_ecam[0].phys_base);
        serial_write(", buses ");
        serial_write(itoa(g_ecam[0].start_bus));
        serial_write("-");
        serial_write(itoa(g_ecam[0].end_bus));
        serial_write("\n");
    } else {
        serial_write(mcfg ? "pci: MCFG unusable, config access ports 0xcf8/0xcfc\n"
                          : "pci: no MCFG, config access ports 0xcf8/0xcfc\n");
    }
    pci_scan_bus(0, NULL);
    return 0;
}

size_t pci_device_count(){
    return vector_count(pci_device_vector);
}

int pci_device_base_class(struct pci_device* device){
    return device->class.base;
}

int pci_device_subclass(struct pci_device* device){
    return device->class.subclass;
}

int pci_device_get(size_t index, struct pci_device** device_out){
    if(index >= pci_device_count()){
        return -EOUTOFRANGE;
    }
    return vector_at(pci_device_vector, index, device_out, sizeof(*device_out));
}

// Lecture 180 - walk up from a device toward the root, OR'ing
// BME/MSE/IOSE into each bridge's command register so the
// physical path between root complex and device is enabled to
// pass IO/MEM cycles and bus-master DMA.
static void pci_enable_upstream_path(struct pci_device* bridge, bool need_io, bool need_mem){
    for(struct pci_device* p = bridge; p; p = p->parent_bridge){
        uint16_t cmd = pci_cfg_read_word(p->addr.bus, p->addr.slot, p->addr.func,
                                         PCI_HEADER_COMMAND_OFFSET);
        cmd |= 0x0004;  // BME (bus master enable)
        if(need_mem){
            cmd |= 0x0002;  // MSE
        }
        if(need_io){
            cmd |= 0x0001;  // IOSE
        }
        pci_cfg_write_word(p->addr.bus, p->addr.slot, p->addr.func,
                           PCI_HEADER_COMMAND_OFFSET, cmd);
    }
}

// Lecture 180 - enable bus mastering + matching IO/MEM decode on
// the device, then propagate the same enables up through every
// bridge on the path to the root complex.
//
// Upstream's header decl is `bus_enable_bus_master`, the body
// is `pci_enable_bus_master`. Preserved verbatim per the project
// rule on upstream identifiers; a static wrapper under the
// header-side name links the two.
void pci_enable_bus_master(struct pci_device* d){
    bool need_mem = false;
    bool need_io  = false;
    for(int i = 0; i < 6; ++i){
        if(d->bars[i].size == 0){
            continue;
        }
        if(d->bars[i].flags & PCI_DEVICE_BAR_FLAG_IS_EXT){
            continue;
        }
        if(d->bars[i].type == PCI_DEVICE_IO_MEMORY){
            need_mem = true;
        } else {
            need_io = true;
        }
    }
    uint16_t cmd = pci_cfg_read_word(d->addr.bus, d->addr.slot, d->addr.func,
                                     PCI_HEADER_COMMAND_OFFSET);
    if(need_mem){
        cmd |= 0x0002;
    }
    if(need_io){
        cmd |= 0x0001;
    }
    cmd |= 0x0004;  // BME
    pci_cfg_write_word(d->addr.bus, d->addr.slot, d->addr.func,
                       PCI_HEADER_COMMAND_OFFSET, cmd);
    if(d->parent_bridge){
        pci_enable_upstream_path(d->parent_bridge, need_io, need_mem);
    }
}

// Lecture 180 - the L177 header decl is `bus_enable_bus_master`.
// Forward to the real implementation so callers using either
// spelling link cleanly.
void bus_enable_bus_master(struct pci_device* device){
    pci_enable_bus_master(device);
}

// The GSI a device's INTx pin arrives on in APIC mode.
// The general answer is the DSDT's _PRT, which needs an AML
// interpreter. Until there is one only the machine SamOS boots on is
// known: QEMU q35 (host bridge 8086:29C0) routes the ICH9 functions at
// 00:1f.x (LPC, AHCI, SMBus) INTA-INTD to PIRQ A-D, GSI 16-19 (its
// D31IR default and its _PRT). The GSIx link devices' _CRS says level,
// active high: QEMU models PCI INTx as an active-high level, and KVM's
// in-kernel IOAPIC honours the polarity bit, so active low would
// storm there. Anything else returns -ENOENT and the driver polls.
// Two more cases, for the slots QEMU puts -device on:
//   q35, bus 0 slot n < 25: QEMU's ich9_cc_init sets the D<n>IR default
//   to INTA-D -> PIRQ E-H rotated by the slot, the same as its _PRT,
//   so pin p (1-4) is GSI 20 + ((n + p - 1) & 3);
//   pc (i440FX 8086:1237), bus 0: the PIIX3 swizzle sends pin p of
//   slot n to PIRQ (n - 1 + p - 1) & 3, and the firmware put the ISA
//   IRQ for it in PIRQRC A-D (00:01.0, 0x60-0x63); QEMU's MADT has a
//   level, active-high override for those IRQs (5, 9, 10, 11).
int pci_intx_gsi(struct pci_device* d, uint16_t* inti_flags){
    uint8_t pin = pci_cfg_read_byte(d->addr.bus, d->addr.slot, d->addr.func,
                                    PCI_HEADER_INTERRUPT_PIN_OFFSET);
    if(pin < 1 || pin > 4 || d->addr.bus != 0 ||
       pci_cfg_read_word(0, 0, 0, PCI_HEADER_VENDOR_OFFSET) != 0x8086){
        return -ENOENT;
    }
    uint16_t host = pci_cfg_read_word(0, 0, 0, PCI_HEADER_DEVICE_OFFSET);
    if(host == 0x29C0){
        *inti_flags = ACPI_INTI_POLARITY_HIGH | ACPI_INTI_TRIGGER_LEVEL;
        if(d->addr.slot == 0x1F){
            return 16 + pin - 1;
        }
        return d->addr.slot < 25 ? 20 + ((d->addr.slot + pin - 1) & 3) : -ENOENT;
    }
    if(host == 0x1237 && d->addr.slot > 1){
        uint8_t irq = pci_cfg_read_byte(0, 1, 0, 0x60 + ((d->addr.slot - 1 + pin - 1) & 3));
        if((irq & 0x80) || irq > 15){
            return -ENOENT;
        }
        uint16_t flags = 0;
        uint32_t gsi = acpi_isa_irq_to_gsi(irq, &flags);
        if((flags & ACPI_INTI_TRIGGER_MASK) != ACPI_INTI_TRIGGER_LEVEL){
            return -ENOENT;
        }
        *inti_flags = flags;
        return (int)gsi;
    }
    return -ENOENT;
}

// Clear the command register's INTx Disable bit (10).
void pci_intx_enable(struct pci_device* d){
    uint16_t cmd = pci_cfg_read_word(d->addr.bus, d->addr.slot, d->addr.func,
                                     PCI_HEADER_COMMAND_OFFSET);
    pci_cfg_write_word(d->addr.bus, d->addr.slot, d->addr.func,
                       PCI_HEADER_COMMAND_OFFSET, cmd & ~0x0400);
}
