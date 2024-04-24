#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H
// ACPI tables: RSDP, XSDT/RSDT, MADT, MCFG, FADT, and S5
// shutdown and reboot.
//
// Not in the book: it hard-codes the 8259 PIC and the 0xCF8 PCI
// ports and has no power-off. The loader passes the RSDP the UEFI
// configuration table names in boot_info.acpi_rsdp. Every
// table is checksum-checked and mapped through the direct map with
// paging_map_mmio (ACPI memory is not part of the RAM direct map).
//
// The Arm parts as well: the MADT's GIC entries (GICC per CPU,
// GICD, GICR ranges), the SPCR console, the GTDT timer interrupts and
// the FADT's ARM_BOOT_ARCH (PSCI, HVC or SMC). x86 tables have none
// of them, so on x86 they stay zero and nothing more is printed.

#include <stdint.h>
#include <stdbool.h>

#define ACPI_MAX_TABLES    64
#define ACPI_MAX_CPUS      64      // Starts the others from this list
#define ACPI_MAX_IOAPICS   8
#define ACPI_MAX_OVERRIDES 16
#define ACPI_MAX_LAPIC_NMI 8
#define ACPI_MAX_GICR      8

// MADT GICC (type 0x0B): one per CPU.
struct acpi_gicc {
    uint32_t uid;                  // ACPI processor UID
    uint64_t mpidr;
    bool     enabled;              // flags bit 0 (or bit 3, online capable)
};

struct acpi_gicr {
    uint64_t base;                 // MADT GICR (type 0x0E) discovery range
    uint32_t length;
};

#define ACPI_SPCR_PL011        0x03 // SPCR interface types
#define ACPI_SPCR_SBSA_32BIT   0x0d
#define ACPI_SPCR_SBSA         0x0e
#define ACPI_ARM_PSCI          0x1  // FADT ARM_BOOT_ARCH
#define ACPI_ARM_PSCI_HVC      0x2

struct acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

// Generic address structure (FADT reset and PM1 registers).
struct acpi_gas {
    uint8_t  space_id;             // ACPI_GAS_*
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed));

#define ACPI_GAS_MEMORY  0
#define ACPI_GAS_IO      1
#define ACPI_GAS_PCI     2

// MADT entries, kept as plain records.
struct acpi_cpu {
    uint8_t  acpi_id;              // processor UID (ties LAPIC NMI entries to it)
    uint8_t  apic_id;
    bool     enabled;              // flags bit 0; bit 1 (online capable) counts too
};

struct acpi_ioapic {
    uint8_t  id;
    uint32_t phys;
    uint32_t gsi_base;
};

// Interrupt source override: ISA `irq` arrives on `gsi`. Flags are
// the MPS INTI flags: bits 0-1 polarity, bits 2-3 trigger mode.
struct acpi_override {
    uint8_t  irq;
    uint32_t gsi;
    uint16_t flags;
};

struct acpi_lapic_nmi {
    uint8_t  acpi_id;              // 0xFF = every processor
    uint8_t  lint;                 // LINT0 or LINT1
    uint16_t flags;
};

#define ACPI_INTI_POLARITY_MASK  0x3
#define ACPI_INTI_POLARITY_HIGH  0x1
#define ACPI_INTI_POLARITY_LOW   0x3
#define ACPI_INTI_TRIGGER_MASK   0xC
#define ACPI_INTI_TRIGGER_EDGE   0x4
#define ACPI_INTI_TRIGGER_LEVEL  0xC

struct acpi_info {
    bool     present;              // RSDP and root table valid
    uint8_t  rsdp_revision;        // 0 = ACPI 1.0 (RSDT), 2+ = XSDT
    int      table_count;

    // MADT
    bool     have_madt;
    uint64_t lapic_phys;           // after any type 5 override
    bool     pcat_compat;          // MADT flags bit 0: dual 8259s present
    int      cpu_count;
    struct acpi_cpu       cpus[ACPI_MAX_CPUS];
    int      ioapic_count;
    struct acpi_ioapic    ioapics[ACPI_MAX_IOAPICS];
    int      override_count;
    struct acpi_override  overrides[ACPI_MAX_OVERRIDES];
    int      lapic_nmi_count;
    struct acpi_lapic_nmi lapic_nmis[ACPI_MAX_LAPIC_NMI];

    // FADT
    bool     have_fadt;
    uint8_t  century_reg;          // CMOS index of the century, 0 = none
    uint16_t boot_arch;            // IAPC_BOOT_ARCH (bit 1: 8042 present)
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    struct acpi_gas pm1a_cnt;      // address 0 = absent
    struct acpi_gas pm1b_cnt;
    bool     have_reset_reg;
    struct acpi_gas reset_reg;
    uint8_t  reset_value;

    // \_S5 from the DSDT
    bool     s5_found;
    uint8_t  s5_slp_typ_a;
    uint8_t  s5_slp_typ_b;

    // Arm (MADT GIC entries, SPCR, GTDT, FADT ARM_BOOT_ARCH)
    uint64_t gicd_phys;            // 0 = no GICD entry
    uint8_t  gic_version;          // GICD entry: 0 = not given, else 1..4
    int      gicc_count;
    struct acpi_gicc gicc[ACPI_MAX_CPUS];
    int      gicr_count;
    struct acpi_gicr gicr[ACPI_MAX_GICR];
    bool     have_spcr;
    uint8_t  spcr_type;            // ACPI_SPCR_*
    struct acpi_gas spcr_base;
    uint32_t spcr_gsiv;            // 0 = polled
    bool     have_gtdt;
    uint32_t gtdt_gsiv[4];         // secure EL1, non-secure EL1, virtual, EL2 timer
    uint32_t gtdt_flags[4];        // bit 0 edge, bit 1 active low
    uint16_t arm_boot_arch;        // ACPI_ARM_PSCI*
};

// Parse everything from the RSDP's physical address. Returns 0 or a
// negative status; on failure acpi_info()->present is false and the
// kernel goes on with the legacy devices (PIC, 0xCF8, no S5).
int  acpi_init(uint64_t rsdp_phys);
const struct acpi_info* acpi_info(void);

// The index-th table with this signature ("APIC", "MCFG", "FACP",
// "DSDT", ...), mapped and checksum-valid, or NULL.
struct acpi_sdt_header* acpi_find_table(const char* signature, int index);

// The GSI and INTI flags an ISA IRQ arrives on (identity, flags 0,
// when no override names it).
uint32_t acpi_isa_irq_to_gsi(uint8_t irq, uint16_t* flags_out);

// Power off (S5) and reset. Neither returns; each tries the ACPI way
// first and then the fallbacks listed in acpi.c, printing which.
void acpi_shutdown(void) __attribute__((noreturn));
void acpi_reboot(void) __attribute__((noreturn));

#endif
