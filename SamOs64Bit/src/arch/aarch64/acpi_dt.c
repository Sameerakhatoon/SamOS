// ACPI tables to device tree nodes. See acpi_dt.h.
//
// A DTB writer just big enough for the nodes below: one structure
// block and one strings block, assembled behind a v17 header with an
// empty reservation map. Property names are shared in the strings
// block. Big-endian throughout, written byte by byte.
#include "arch/aarch64/acpi_dt.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/memmap.h"
#include "acpi/acpi.h"

#define STRUCT_MAX  8192
#define STRINGS_MAX 512
#define HDR_SIZE    40
#define RSV_SIZE    16

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE   2
#define FDT_PROP       3
#define FDT_END        9

// DT interrupt flags (the third cell) for the GTDT's.
#define IRQ_EDGE_RISING 1
#define IRQ_LEVEL_HIGH  4
#define IRQ_LEVEL_LOW   8

static uint8_t dt_struct[STRUCT_MAX];
static char dt_strings[STRINGS_MAX];
static uint32_t struct_len, strings_len;
static int overflow;
static uint8_t blob[HDR_SIZE + RSV_SIZE + STRUCT_MAX + STRINGS_MAX] __attribute__((aligned(8)));

static uint32_t str_len(const char* s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static int str_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void be32_at(uint8_t* p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void put32(uint32_t v)
{
    if (struct_len + 4 > STRUCT_MAX) {
        overflow = 1;
        return;
    }
    be32_at(dt_struct + struct_len, v);
    struct_len += 4;
}

// Bytes, then zeros up to the next 4-byte boundary.
static void put_bytes(const void* v, uint32_t len)
{
    const uint8_t* p = v;
    uint32_t padded = (len + 3) & ~3u;
    if (struct_len + padded > STRUCT_MAX) {
        overflow = 1;
        return;
    }
    for (uint32_t i = 0; i < padded; i++)
        dt_struct[struct_len++] = i < len ? p[i] : 0;
}

static uint32_t name_offset(const char* name)
{
    for (uint32_t off = 0; off < strings_len; off += str_len(dt_strings + off) + 1)
        if (str_eq(dt_strings + off, name))
            return off;
    uint32_t len = str_len(name) + 1, off = strings_len;
    if (off + len > STRINGS_MAX) {
        overflow = 1;
        return 0;
    }
    for (uint32_t i = 0; i < len; i++)
        dt_strings[strings_len++] = name[i];
    return off;
}

static void begin_node(const char* name)
{
    put32(FDT_BEGIN_NODE);
    put_bytes(name, str_len(name) + 1);
}

static void end_node(void)
{
    put32(FDT_END_NODE);
}

static void prop(const char* name, const void* value, uint32_t len)
{
    put32(FDT_PROP);
    put32(len);
    put32(name_offset(name));
    put_bytes(value, len);
}

static void prop_cells(const char* name, const uint32_t* cells, int n)
{
    put32(FDT_PROP);
    put32(4 * n);
    put32(name_offset(name));
    for (int i = 0; i < n; i++)
        put32(cells[i]);
}

static void prop_u32(const char* name, uint32_t v)
{
    prop_cells(name, &v, 1);
}

static void prop_str(const char* name, const char* s)
{
    prop(name, s, str_len(s) + 1);
}

// "name@hex", as DT unit addresses are written.
static const char* unit_name(char* out, const char* name, uint64_t addr)
{
    char digits[17];
    int n = 0, k = 0;
    do {
        digits[n++] = "0123456789abcdef"[addr & 0xf];
        addr >>= 4;
    } while (addr);
    while (*name)
        out[k++] = *name++;
    out[k++] = '@';
    while (n)
        out[k++] = digits[--n];
    out[k] = 0;
    return out;
}

static int psci_hvc(const struct acpi_info* a)
{
    return a->arm_boot_arch & ACPI_ARM_PSCI_HVC;
}

static void report(const struct acpi_info* a)
{
    int enabled = 0;
    for (int i = 0; i < a->gicc_count; i++)
        enabled += a->gicc[i].enabled;
    console_printf("acpi: %d tables, madt gicd 0x%lx (version %u), %d gicr range(s) at 0x%lx, %d gicc (%d enabled)\n",
                   a->table_count, a->gicd_phys, a->gic_version, a->gicr_count,
                   a->gicr_count ? a->gicr[0].base : 0, a->gicc_count, enabled);
    if (a->have_spcr)
        console_printf("acpi: spcr interface %u at 0x%lx (%s), gsiv %u\n", a->spcr_type, a->spcr_base.address,
                       a->spcr_base.space_id == ACPI_GAS_MEMORY ? "mmio" : "not mmio", a->spcr_gsiv);
    else
        console_printf("acpi: no spcr\n");
    if (a->have_gtdt)
        console_printf("acpi: gtdt el1 timer gsiv %u, virtual timer gsiv %u\n", a->gtdt_gsiv[1], a->gtdt_gsiv[2]);
    console_printf("acpi: fadt arm_boot_arch 0x%x (%s)\n", a->arm_boot_arch,
                   !(a->arm_boot_arch & ACPI_ARM_PSCI) ? "no psci" : psci_hvc(a) ? "psci via hvc" : "psci via smc");
}

static uint64_t spcr_uart(const struct acpi_info* a)
{
    if (!a->have_spcr || a->spcr_base.space_id != ACPI_GAS_MEMORY)
        return 0;
    if (a->spcr_type != ACPI_SPCR_PL011 && a->spcr_type != ACPI_SPCR_SBSA && a->spcr_type != ACPI_SPCR_SBSA_32BIT)
        return 0;
    return a->spcr_base.address;
}

static void build(const struct acpi_info* a, const char* bootargs)
{
    static const char gic_compat[] = "arm,gic-v3";
    static const char uart_compat[] = "arm,pl011\0arm,primecell";
    static const char psci_compat[] = "arm,psci-1.0\0arm,psci-0.2";
    static uint32_t cells[4 * (MEMMAP_MAX + ACPI_MAX_GICR + 1)];
    char name[40], path[48];
    struct acpi_sdt_header* madt = acpi_find_table("APIC", 0);
    uint64_t uart = spcr_uart(a);
    int psci = a->arm_boot_arch & ACPI_ARM_PSCI;
    int n;

    struct_len = strings_len = 0;
    overflow = 0;
    begin_node("");
    prop_u32("#address-cells", 2);
    prop_u32("#size-cells", 2);
    // QEMU's tables say BOCHS; arch_on_hypervisor reads the root node.
    int qemu = madt && madt->oem_id[0] == 'B' && madt->oem_id[1] == 'O' && madt->oem_id[2] == 'C' &&
               madt->oem_id[3] == 'H' && madt->oem_id[4] == 'S';
    prop_str("compatible", qemu ? "linux,dummy-virt" : "samos,acpi");

    begin_node("chosen");
    if (bootargs)
        prop_str("bootargs", bootargs);
    if (uart) {
        path[0] = '/';
        unit_name(path + 1, "pl011", uart);
        prop_str("stdout-path", path);
    }
    end_node();

    const struct mem_region* m = memmap_regions(&n);
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (m[i].type != MEM_REGION_USABLE)
            continue;
        cells[k++] = m[i].base >> 32;
        cells[k++] = (uint32_t)m[i].base;
        cells[k++] = m[i].length >> 32;
        cells[k++] = (uint32_t)m[i].length;
    }
    if (k) {
        begin_node(unit_name(name, "memory", ((uint64_t)cells[0] << 32) | cells[1]));
        prop_str("device_type", "memory");
        prop_cells("reg", cells, k);
        end_node();
    }

    begin_node("cpus");
    prop_u32("#address-cells", 2);
    prop_u32("#size-cells", 0);
    for (int i = 0; i < a->gicc_count; i++) {
        if (!a->gicc[i].enabled)
            continue;
        uint64_t mpidr = a->gicc[i].mpidr & 0xff00ffffffull;
        uint32_t reg[2] = { mpidr >> 32, (uint32_t)mpidr };
        begin_node(unit_name(name, "cpu", mpidr));
        prop_str("device_type", "cpu");
        prop_str("compatible", "arm,armv8");
        prop_cells("reg", reg, 2);
        if (psci)
            prop_str("enable-method", "psci");
        end_node();
    }
    end_node();

    if (psci) {
        begin_node("psci");
        prop("compatible", psci_compat, sizeof psci_compat);
        prop_str("method", psci_hvc(a) ? "hvc" : "smc");
        end_node();
    }

    // GICD, then each GICR range: the reg layout of the DT binding.
    begin_node(unit_name(name, "intc", a->gicd_phys));
    prop("compatible", gic_compat, sizeof gic_compat);
    prop("interrupt-controller", 0, 0);
    prop_u32("#interrupt-cells", 3);
    prop_u32("#redistributor-regions", a->gicr_count);
    k = 0;
    cells[k++] = a->gicd_phys >> 32;
    cells[k++] = (uint32_t)a->gicd_phys;
    cells[k++] = 0;
    cells[k++] = 0x10000;
    for (int i = 0; i < a->gicr_count; i++) {
        cells[k++] = a->gicr[i].base >> 32;
        cells[k++] = (uint32_t)a->gicr[i].base;
        cells[k++] = 0;
        cells[k++] = a->gicr[i].length;
    }
    prop_cells("reg", cells, k);
    end_node();

    // Secure, non-secure, virtual, hypervisor: the binding's order,
    // which is the GTDT's. All four are PPIs (16..31).
    begin_node("timer");
    prop_str("compatible", "arm,armv8-timer");
    k = 0;
    for (int i = 0; a->have_gtdt && i < 4; i++) {
        uint32_t f = a->gtdt_flags[i];
        cells[k++] = 1;
        cells[k++] = a->gtdt_gsiv[i] - 16;
        cells[k++] = f & 1 ? IRQ_EDGE_RISING : f & 2 ? IRQ_LEVEL_LOW : IRQ_LEVEL_HIGH;
        if (a->gtdt_gsiv[i] < 16 || a->gtdt_gsiv[i] > 31)
            k = -100;
    }
    if (k > 0)
        prop_cells("interrupts", cells, k);
    end_node();

    if (uart) {
        uint32_t reg[4] = { uart >> 32, (uint32_t)uart, 0, 0x1000 };
        uint32_t irq[3] = { 0, a->spcr_gsiv - 32, IRQ_LEVEL_HIGH };
        begin_node(path + 1);
        prop("compatible", uart_compat, sizeof uart_compat);
        prop_cells("reg", reg, 4);
        if (a->spcr_gsiv >= 32)
            prop_cells("interrupts", irq, 3);
        end_node();
    }
    end_node();
    put32(FDT_END);
}

int acpi_dt_build(uint64_t rsdp, const char* bootargs, struct fdt* out)
{
    if (acpi_init(rsdp) < 0) {
        console_printf("acpi: no valid RSDP and root table at 0x%lx\n", rsdp);
        return -1;
    }
    const struct acpi_info* a = acpi_info();
    report(a);
    if (!a->gicd_phys || !a->gicr_count) {
        console_printf("acpi: the MADT has no GICD or GICR entry (GICv2?)\n");
        return -1;
    }
    build(a, bootargs);
    if (overflow) {
        console_printf("acpi: device tree does not fit %u bytes\n", STRUCT_MAX);
        return -1;
    }
    uint32_t off_struct = HDR_SIZE + RSV_SIZE, off_strings = off_struct + struct_len;
    uint32_t total = off_strings + strings_len;
    for (uint32_t i = 0; i < HDR_SIZE + RSV_SIZE; i++)
        blob[i] = 0;
    be32_at(blob + 0, FDT_MAGIC);
    be32_at(blob + 4, total);
    be32_at(blob + 8, off_struct);
    be32_at(blob + 12, off_strings);
    be32_at(blob + 16, HDR_SIZE);       // the empty reservation map
    be32_at(blob + 20, 17);             // version
    be32_at(blob + 24, 16);             // last compatible version
    be32_at(blob + 32, strings_len);
    be32_at(blob + 36, struct_len);
    for (uint32_t i = 0; i < struct_len; i++)
        blob[off_struct + i] = dt_struct[i];
    for (uint32_t i = 0; i < strings_len; i++)
        blob[off_strings + i] = dt_strings[i];
    int r = fdt_init(out, blob, sizeof blob);
    if (r < 0) {
        console_printf("acpi: built device tree is invalid (error %d)\n", -r);
        return -1;
    }
    console_printf("acpi: device tree built from the tables, %u bytes\n", total);
    return 0;
}
