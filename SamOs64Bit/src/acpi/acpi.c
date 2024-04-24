// ACPI table discovery, MADT/FADT/MCFG parsing, S5 shutdown
// and reboot. Not in the book.
//
// Tables are reached through the direct map: paging_map_mmio maps
// [phys, phys + len) at SAMOS_DIRECT_MAP_BASE + phys (ACPI memory is
// type 3/4 in the memory map, so the RAM direct map skips it). Every
// table length is bounded and every checksum checked before a field
// is read; a bad table is skipped, not trusted.

#include "acpi/acpi.h"
#include "arch/arch.h"
#include "memory/memory.h"
#include "io/serial.h"
#include "string/string.h"
#include "status.h"

#define ACPI_TABLE_MAX_LEN  (4u << 20)    // sanity bound for one table

struct acpi_rsdp {
    char     signature[8];         // "RSD PTR "
    uint8_t  checksum;             // first 20 bytes
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt;
    uint32_t length;               // revision 2+ from here
    uint64_t xsdt;
    uint8_t  ext_checksum;         // whole length
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header hdr;
    uint32_t lapic;
    uint32_t flags;
} __attribute__((packed));

// FADT up to X_PM1b_CNT_BLK; fields past hdr.length are not read.
struct acpi_fadt {
    struct acpi_sdt_header hdr;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved0;
    uint8_t  pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;         // offset 64
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;              // offset 108
    uint16_t iapc_boot_arch;
    uint8_t  reserved1;
    uint32_t flags;                // offset 112
    struct acpi_gas reset_reg;     // offset 116
    uint8_t  reset_value;          // offset 128
    uint16_t arm_boot_arch;
    uint8_t  minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;               // offset 140
    struct acpi_gas x_pm1a_evt_blk;
    struct acpi_gas x_pm1b_evt_blk;
    struct acpi_gas x_pm1a_cnt_blk; // offset 172
    struct acpi_gas x_pm1b_cnt_blk; // offset 184
} __attribute__((packed));

#define FADT_HAS(f, field) \
    ((f)->hdr.length >= __builtin_offsetof(struct acpi_fadt, field) + sizeof((f)->field))

#define FADT_FLAG_RESET_REG_SUP  (1u << 10)
#define PM1_SCI_EN               0x0001
#define PM1_SLP_TYP_SHIFT        10
#define PM1_SLP_TYP_MASK         (7u << PM1_SLP_TYP_SHIFT)
#define PM1_SLP_EN               (1u << 13)

static struct acpi_info info;
static struct acpi_sdt_header* tables[ACPI_MAX_TABLES];
static struct acpi_sdt_header* dsdt;

const struct acpi_info* acpi_info(void){
    return &info;
}

static void acpi_log(const char* s){
    serial_write(s);
}

static void acpi_log_dec(uint64_t v){
    char buf[21];
    int i = sizeof(buf) - 1;
    buf[i] = 0;
    do {
        buf[--i] = '0' + v % 10;
        v /= 10;
    } while(v && i > 0);
    serial_write(&buf[i]);
}

static uint8_t acpi_sum(const void* p, uint32_t len){
    const uint8_t* b = p;
    uint8_t s = 0;
    for(uint32_t i = 0; i < len; i++){
        s += b[i];
    }
    return s;
}

// Map a table: the header first for its length, then all of it.
// NULL when the length is out of bounds or the checksum is wrong.
static struct acpi_sdt_header* acpi_map_table(uint64_t phys){
    if(!phys){
        return NULL;
    }
    struct acpi_sdt_header* h = paging_map_mmio(phys, sizeof(*h), 0);
    if(!h || h->length < sizeof(*h) || h->length > ACPI_TABLE_MAX_LEN){
        return NULL;
    }
    h = paging_map_mmio(phys, h->length, 0);
    if(!h || acpi_sum(h, h->length) != 0){
        acpi_log("acpi: bad checksum, table at ");
        serial_write_hex(phys);
        acpi_log(" skipped\n");
        return NULL;
    }
    return h;
}

struct acpi_sdt_header* acpi_find_table(const char* signature, int index){
    if(strncmp(signature, "DSDT", 4) == 0){
        return index == 0 ? dsdt : NULL;
    }
    for(int i = 0; i < info.table_count; i++){
        if(strncmp(tables[i]->signature, signature, 4) == 0 && index-- == 0){
            return tables[i];
        }
    }
    return NULL;
}

uint32_t acpi_isa_irq_to_gsi(uint8_t irq, uint16_t* flags_out){
    for(int i = 0; i < info.override_count; i++){
        if(info.overrides[i].irq == irq){
            if(flags_out){
                *flags_out = info.overrides[i].flags;
            }
            return info.overrides[i].gsi;
        }
    }
    if(flags_out){
        *flags_out = 0;
    }
    return irq;
}

// MADT entries: type, length, body. A length below 2 or past the end
// stops the walk (a zero length would loop forever).
static void acpi_parse_madt(struct acpi_madt* m){
    if(m->hdr.length < sizeof(*m)){
        return;
    }
    info.have_madt   = true;
    info.lapic_phys  = m->lapic;
    info.pcat_compat = m->flags & 1;
    uint8_t* p   = (uint8_t*)m + sizeof(*m);
    uint8_t* end = (uint8_t*)m + m->hdr.length;
    while(p + 2 <= end && p[1] >= 2 && p + p[1] <= end){
        uint8_t type = p[0], len = p[1];
        if(type == 0 && len >= 8 && info.cpu_count < ACPI_MAX_CPUS){
            uint32_t flags = *(uint32_t*)(p + 4);
            struct acpi_cpu* c = &info.cpus[info.cpu_count++];
            c->acpi_id = p[2];
            c->apic_id = p[3];
            c->enabled = (flags & 3) != 0;
        } else if(type == 1 && len >= 12 && info.ioapic_count < ACPI_MAX_IOAPICS){
            struct acpi_ioapic* io = &info.ioapics[info.ioapic_count++];
            io->id       = p[2];
            io->phys     = *(uint32_t*)(p + 4);
            io->gsi_base = *(uint32_t*)(p + 8);
        } else if(type == 2 && len >= 10 && info.override_count < ACPI_MAX_OVERRIDES){
            // bus p[2] is always 0 (ISA)
            struct acpi_override* o = &info.overrides[info.override_count++];
            o->irq   = p[3];
            o->gsi   = *(uint32_t*)(p + 4);
            o->flags = *(uint16_t*)(p + 8);
        } else if(type == 4 && len >= 6 && info.lapic_nmi_count < ACPI_MAX_LAPIC_NMI){
            struct acpi_lapic_nmi* n = &info.lapic_nmis[info.lapic_nmi_count++];
            n->acpi_id = p[2];
            n->flags   = *(uint16_t*)(p + 3);
            n->lint    = p[5];
        } else if(type == 5 && len >= 12){
            info.lapic_phys = *(uint64_t*)(p + 4);
        } else if(type == 0x0B && len >= 76 && info.gicc_count < ACPI_MAX_CPUS){
            // GICC: UID at 8, flags at 12, MPIDR at 68
            struct acpi_gicc* c = &info.gicc[info.gicc_count++];
            uint32_t flags = *(uint32_t*)(p + 12);
            c->uid     = *(uint32_t*)(p + 8);
            c->mpidr   = *(uint64_t*)(p + 68);
            c->enabled = (flags & 9) != 0;
        } else if(type == 0x0C && len >= 24){
            info.gicd_phys   = *(uint64_t*)(p + 8);
            info.gic_version = p[20];
        } else if(type == 0x0E && len >= 16 && info.gicr_count < ACPI_MAX_GICR){
            info.gicr[info.gicr_count].base     = *(uint64_t*)(p + 4);
            info.gicr[info.gicr_count++].length = *(uint32_t*)(p + 12);
        }
        p += len;
    }
}

// SPCR: the console UART (interface type at 36, its address
// as a GAS at 40, the GSIV at 54).
static void acpi_parse_spcr(struct acpi_sdt_header* t){
    uint8_t* p = (uint8_t*)t;
    if(t->length < 58){
        return;
    }
    info.have_spcr = true;
    info.spcr_type = p[36];
    memcpy(&info.spcr_base, p + 40, sizeof(info.spcr_base));
    info.spcr_gsiv = *(uint32_t*)(p + 54);
}

// GTDT: GSIV and flags of the four per-CPU timers, from 48.
static void acpi_parse_gtdt(struct acpi_sdt_header* t){
    uint8_t* p = (uint8_t*)t;
    if(t->length < 80){
        return;
    }
    info.have_gtdt = true;
    for(int i = 0; i < 4; i++){
        info.gtdt_gsiv[i]  = *(uint32_t*)(p + 48 + 8 * i);
        info.gtdt_flags[i] = *(uint32_t*)(p + 52 + 8 * i);
    }
}

// The 32-bit FADT block address as an I/O GAS, unless the X_ field
// is present and non-zero (ACPI 2.0+ says X_ wins).
static void acpi_pm1_reg(struct acpi_gas* out, uint32_t legacy, const struct acpi_gas* x, bool has_x){
    memset(out, 0, sizeof(*out));
    if(has_x && x->address){
        *out = *x;
    } else if(legacy){
        out->space_id = ACPI_GAS_IO;
        out->bit_width = 16;
        out->address = legacy;
    }
}

// Minimal AML scan for \_S5: NameOp (0x08), optional root prefix,
// "_S5_", PackageOp (0x12), PkgLength (1-4 bytes), NumElements, then
// SLP_TYPa and SLP_TYPb as ZeroOp, OneOp or BytePrefix + byte. Not an
// interpreter: an _S5 built by a method is not found, and the caller
// falls back (see acpi_shutdown).
static int acpi_aml_int(const uint8_t** pp, const uint8_t* end, uint8_t* out){
    const uint8_t* p = *pp;
    if(p >= end){
        return -1;
    }
    if(*p == 0x00 || *p == 0x01){       // ZeroOp, OneOp
        *out = *p;
        *pp = p + 1;
        return 0;
    }
    if(*p == 0x0A && p + 1 < end){      // BytePrefix
        *out = p[1];
        *pp = p + 2;
        return 0;
    }
    return -1;
}

static void acpi_find_s5(struct acpi_sdt_header* t){
    if(!t){
        return;
    }
    const uint8_t* start = (const uint8_t*)t + sizeof(*t);
    const uint8_t* end   = (const uint8_t*)t + t->length;
    for(const uint8_t* p = start + 1; p + 4 < end; p++){
        if(p[0] != '_' || p[1] != 'S' || p[2] != '5' || p[3] != '_'){
            continue;
        }
        bool named = p[-1] == 0x08 || (p[-1] == '\\' && p - 2 >= start && p[-2] == 0x08);
        if(!named || p[4] != 0x12){
            continue;
        }
        const uint8_t* q = p + 5;
        if(q >= end){
            return;
        }
        q += 1 + (*q >> 6);             // PkgLength
        q += 1;                         // NumElements
        uint8_t a, b;
        if(acpi_aml_int(&q, end, &a) < 0 || acpi_aml_int(&q, end, &b) < 0){
            return;
        }
        info.s5_found = true;
        info.s5_slp_typ_a = a & 7;
        info.s5_slp_typ_b = b & 7;
        return;
    }
}

static void acpi_parse_fadt(struct acpi_fadt* f){
    if(f->hdr.length < __builtin_offsetof(struct acpi_fadt, century)){
        return;
    }
    info.have_fadt   = true;
    info.smi_cmd     = f->smi_cmd;
    info.acpi_enable = f->acpi_enable;
    if(FADT_HAS(f, century)){
        info.century_reg = f->century;
    }
    if(FADT_HAS(f, iapc_boot_arch)){
        info.boot_arch = f->iapc_boot_arch;
    }
    acpi_pm1_reg(&info.pm1a_cnt, f->pm1a_cnt_blk, &f->x_pm1a_cnt_blk, FADT_HAS(f, x_pm1a_cnt_blk));
    acpi_pm1_reg(&info.pm1b_cnt, f->pm1b_cnt_blk, &f->x_pm1b_cnt_blk, FADT_HAS(f, x_pm1b_cnt_blk));
    if(f->hdr.revision >= 2 && FADT_HAS(f, reset_value) &&
       (f->flags & FADT_FLAG_RESET_REG_SUP) && f->reset_reg.address){
        info.have_reset_reg = true;
        info.reset_reg   = f->reset_reg;
        info.reset_value = f->reset_value;
    }
    if(FADT_HAS(f, arm_boot_arch)){
        info.arm_boot_arch = f->arm_boot_arch;
    }
    uint64_t dsdt_phys = f->dsdt;
    if(FADT_HAS(f, x_dsdt) && f->x_dsdt){
        dsdt_phys = f->x_dsdt;
    }
    dsdt = acpi_map_table(dsdt_phys);
    acpi_find_s5(dsdt);
}

int acpi_init(uint64_t rsdp_phys){
    memset(&info, 0, sizeof(info));
    if(!rsdp_phys){
        acpi_log("acpi: no RSDP from the loader, legacy devices only\n");
        return -ENOENT;
    }
    struct acpi_rsdp* r = paging_map_mmio(rsdp_phys, sizeof(*r), 0);
    if(!r || strncmp(r->signature, "RSD PTR ", 8) != 0 || acpi_sum(r, 20) != 0){
        acpi_log("acpi: bad RSDP, legacy devices only\n");
        return -EINFORMAT;
    }
    // Revision 2+ has the XSDT; its length covers the extended part.
    uint64_t root = r->rsdt;
    int entry_size = 4;
    if(r->revision >= 2 && r->length >= sizeof(*r) && r->length <= 4096){
        r = paging_map_mmio(rsdp_phys, r->length, 0);
        if(r && acpi_sum(r, r->length) == 0 && r->xsdt){
            root = r->xsdt;
            entry_size = 8;
        }
    }
    info.rsdp_revision = r->revision;
    struct acpi_sdt_header* rt = acpi_map_table(root);
    if(!rt || strncmp(rt->signature, entry_size == 8 ? "XSDT" : "RSDT", 4) != 0){
        acpi_log("acpi: bad root table, legacy devices only\n");
        return -EINFORMAT;
    }

    uint32_t n = (rt->length - sizeof(*rt)) / entry_size;
    uint8_t* e = (uint8_t*)rt + sizeof(*rt);
    for(uint32_t i = 0; i < n && info.table_count < ACPI_MAX_TABLES; i++){
        uint64_t phys = entry_size == 8 ? *(uint64_t*)(e + i * 8) : *(uint32_t*)(e + i * 4);
        struct acpi_sdt_header* t = acpi_map_table(phys);
        if(t){
            tables[info.table_count++] = t;
        }
    }
    info.present = true;

    struct acpi_sdt_header* t;
    if((t = acpi_find_table("APIC", 0))){
        acpi_parse_madt((struct acpi_madt*)t);
    }
    if((t = acpi_find_table("FACP", 0))){
        acpi_parse_fadt((struct acpi_fadt*)t);
    }
    if((t = acpi_find_table("SPCR", 0))){
        acpi_parse_spcr(t);
    }
    if((t = acpi_find_table("GTDT", 0))){
        acpi_parse_gtdt(t);
    }

    acpi_log("acpi: rsdp revision ");
    acpi_log_dec(info.rsdp_revision);
    acpi_log(entry_size == 8 ? ", xsdt " : ", rsdt ");
    acpi_log_dec(info.table_count);
    acpi_log(" tables:");
    for(int i = 0; i < info.table_count; i++){
        char sig[6] = { ' ', tables[i]->signature[0], tables[i]->signature[1],
                        tables[i]->signature[2], tables[i]->signature[3], 0 };
        acpi_log(sig);
    }
    acpi_log(dsdt ? " DSDT\n" : "\n");
    if(info.have_madt){
        acpi_log("acpi: madt lapic ");
        serial_write_hex(info.lapic_phys);
        acpi_log(", ");
        acpi_log_dec(info.cpu_count);
        acpi_log(" cpu, ");
        acpi_log_dec(info.ioapic_count);
        acpi_log(" ioapic, ");
        acpi_log_dec(info.override_count);
        acpi_log(" overrides, 8259 ");
        acpi_log(info.pcat_compat ? "present\n" : "absent\n");
    }
    if(info.have_fadt){
        acpi_log("acpi: fadt pm1a_cnt ");
        serial_write_hex(info.pm1a_cnt.address);
        acpi_log(", reset ");
        if(info.have_reset_reg){
            serial_write_hex(info.reset_reg.address);
        } else {
            acpi_log("none");
        }
        acpi_log(", century ");
        acpi_log_dec(info.century_reg);
        acpi_log(info.s5_found ? ", _S5 " : ", _S5 not found\n");
        if(info.s5_found){
            acpi_log_dec(info.s5_slp_typ_a);
            acpi_log("/");
            acpi_log_dec(info.s5_slp_typ_b);
            acpi_log("\n");
        }
    }
    return 0;
}

// Short wait without the timer or a calibrated counter: port 0x80 is
// the POST port, a write takes about 1 us on real hardware. The
// ACPI I/O space is the arch's port space (arch_pio_*).
static void acpi_io_wait(uint32_t n){
    while(n--){
        arch_pio_write8(0x80, 0);
    }
}

static uint32_t acpi_gas_read(const struct acpi_gas* g){
    if(g->space_id == ACPI_GAS_IO){
        return g->bit_width == 8 ? arch_pio_read8(g->address) : arch_pio_read16(g->address);
    }
    if(g->space_id == ACPI_GAS_MEMORY){
        volatile uint16_t* p = paging_map_mmio(g->address, 2, PAGING_CACHE_DISABLED);
        return p ? mmio_read16(p) : 0;
    }
    return 0;
}

static void acpi_gas_write(const struct acpi_gas* g, uint32_t v){
    if(g->space_id == ACPI_GAS_IO){
        if(g->bit_width == 8){
            arch_pio_write8(g->address, v);
        } else {
            arch_pio_write16(g->address, v);
        }
    } else if(g->space_id == ACPI_GAS_MEMORY){
        volatile uint16_t* p = paging_map_mmio(g->address, 2, PAGING_CACHE_DISABLED);
        if(p){
            mmio_write16(p, v);
        }
    }
}

// Firmware that boots in legacy mode wants ACPI_ENABLE written to
// SMI_CMD before PM1 control works; OVMF has SCI_EN set already.
static void acpi_enable_mode(void){
    if(!info.smi_cmd || !info.acpi_enable || !info.pm1a_cnt.address ||
       (acpi_gas_read(&info.pm1a_cnt) & PM1_SCI_EN)){
        return;
    }
    arch_pio_write8(info.smi_cmd, info.acpi_enable);
    for(int i = 0; i < 3000 && !(acpi_gas_read(&info.pm1a_cnt) & PM1_SCI_EN); i++){
        acpi_io_wait(100);
    }
}

static void acpi_halt(void){
    acpi_log("acpi: still running, halting\n");
    arch_halt();
}

// S5: SLP_TYPx | SLP_EN into PM1a (and PM1b) control. Without an _S5
// object the type is 0, which is what QEMU's PIIX4 and ICH9 PM blocks
// take as soft-off (and what their DSDTs say). If the machine is
// still up after that, the arch's own ways (x86: QEMU's fixed PM1
// ports).
void acpi_shutdown(void){
    arch_irq_disable();
    if(info.have_fadt && info.pm1a_cnt.address){
        uint8_t a = info.s5_found ? info.s5_slp_typ_a : 0;
        uint8_t b = info.s5_found ? info.s5_slp_typ_b : 0;
        acpi_log(info.s5_found ? "acpi: shutdown via S5\n" : "acpi: shutdown via S5 (no _S5, SLP_TYP 0)\n");
        acpi_enable_mode();
        if(info.pm1b_cnt.address){
            uint32_t v = acpi_gas_read(&info.pm1b_cnt) & ~(PM1_SLP_TYP_MASK | PM1_SLP_EN);
            acpi_gas_write(&info.pm1b_cnt, v | (b << PM1_SLP_TYP_SHIFT) | PM1_SLP_EN);
        }
        uint32_t v = acpi_gas_read(&info.pm1a_cnt) & ~(PM1_SLP_TYP_MASK | PM1_SLP_EN);
        acpi_gas_write(&info.pm1a_cnt, v | (a << PM1_SLP_TYP_SHIFT) | PM1_SLP_EN);
        acpi_io_wait(100000);
    }
    arch_shutdown_fallback();
    acpi_halt();
    __builtin_unreachable();
}

// Reset: the FADT reset register, then the arch's own ways (x86: the
// 0xCF9 reset control register, the 8042 pulse, a triple fault).
void acpi_reboot(void){
    arch_irq_disable();
    if(info.have_reset_reg && info.reset_reg.space_id != ACPI_GAS_PCI){
        acpi_log("acpi: reboot via reset register\n");
        if(info.reset_reg.space_id == ACPI_GAS_IO){
            arch_pio_write8(info.reset_reg.address, info.reset_value);
        } else {
            volatile uint8_t* p = paging_map_mmio(info.reset_reg.address, 1, PAGING_CACHE_DISABLED);
            if(p){
                mmio_write8(p, info.reset_value);
            }
        }
        acpi_io_wait(100000);
    }
    arch_reboot_fallback();
    acpi_halt();
    __builtin_unreachable();
}
