// LAPIC, IOAPIC and the LAPIC timer. Not in the book.
//
// Bring-up order (apic_init, BSP, interrupts off):
//   1. Every IOAPIC entry masked as each IOAPIC is mapped.
//   2. 8259s re-initialised to vectors 0x30-0x3F and fully masked, so
//      a spurious 8259 IRQ cannot alias an IOAPIC vector (0x20-0x2F).
//   3. LAPIC (lapic_init_cpu, also run by each AP): global enable in
//      IA32_APIC_BASE, spurious vector 0xFF with the software-enable
//      bit, TPR 0, timer, LINT0, LINT1 and error LVTs masked (no
//      ExtINT once the 8259 is gone). Then each MADT LAPIC NMI entry
//      for this CPU's UID (or 0xFF, all CPUs) sets its LINT pin to NMI
//      delivery with the entry's polarity and trigger; a pin with no
//      entry stays masked.
//   4. ISA IRQs 0-15 routed to vector 0x20 + irq on this CPU with the
//      override's polarity and trigger, still masked: IRQ_enable
//      unmasks one.
// MMIO goes through the direct map (paging_map_mmio, uncached).

#include "arch/x86_64/apic.h"
#include "arch/x86_64/io.h"
#include "io/serial.h"
#include "arch/x86_64/pit.h"
#include "acpi/acpi.h"
#include "arch/x86_64/paging.h"
#include "string/string.h"
#include "lib/spinlock/spinlock.h"
#include "status.h"

#define MSR_APIC_BASE         0x1B
#define APIC_BASE_ENABLE      (1u << 11)

#define LAPIC_ID              0x020
#define LAPIC_VERSION         0x030
#define LAPIC_TPR             0x080
#define LAPIC_EOI             0x0B0
#define LAPIC_SVR             0x0F0
#define LAPIC_ESR             0x280
#define LAPIC_LVT_TIMER       0x320
#define LAPIC_LVT_LINT0       0x350
#define LAPIC_LVT_LINT1       0x360
#define LAPIC_LVT_ERROR       0x370
#define LAPIC_TIMER_INIT      0x380
#define LAPIC_TIMER_CURRENT   0x390
#define LAPIC_TIMER_DIVIDE    0x3E0

#define LAPIC_SVR_ENABLE      (1u << 8)
#define LVT_MASKED            (1u << 16)
#define LVT_TIMER_PERIODIC    (1u << 17)
#define LVT_DELIVERY_NMI      (4u << 8)
#define LVT_ACTIVE_LOW        (1u << 13)
#define LVT_LEVEL             (1u << 15)
#define LAPIC_DIVIDE_16       0x3

#define IOAPIC_REGSEL         0x00
#define IOAPIC_WINDOW         0x10
#define IOAPIC_REG_VERSION    0x01
#define IOAPIC_REG_REDIR(n)   (0x10 + 2 * (n))

struct ioapic {
    volatile uint32_t* mmio;
    uint32_t gsi_base;
    uint32_t entries;
};

static volatile uint8_t* lapic_mmio;
static struct ioapic ioapics[ACPI_MAX_IOAPICS];
static int ioapic_count;
static bool active;
// IOREGSEL/IOWIN is a register pair: one CPU at a time
// once the disk waits steer their lines to the CPU that waits.
static struct spinlock ioapic_lock = SPINLOCK_INIT("ioapic", LOCK_LEVEL_IOAPIC, false);
static uint64_t timer_hz;              // LAPIC timer counts per second (divide 16)
static uint32_t irq_dest;              // APIC id the ISA IRQs are sent to

static inline uint32_t lapic_read(uint32_t reg){
    return *(volatile uint32_t*)(lapic_mmio + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t v){
    *(volatile uint32_t*)(lapic_mmio + reg) = v;
}

static uint64_t rdmsr(uint32_t msr){
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t v){
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static uint32_t ioapic_read(struct ioapic* io, uint32_t reg){
    io->mmio[IOAPIC_REGSEL / 4] = reg;
    return io->mmio[IOAPIC_WINDOW / 4];
}

static void ioapic_write(struct ioapic* io, uint32_t reg, uint32_t v){
    io->mmio[IOAPIC_REGSEL / 4] = reg;
    io->mmio[IOAPIC_WINDOW / 4] = v;
}

static struct ioapic* ioapic_for_gsi(uint32_t gsi, uint32_t* pin){
    for(int i = 0; i < ioapic_count; i++){
        if(gsi >= ioapics[i].gsi_base && gsi < ioapics[i].gsi_base + ioapics[i].entries){
            *pin = gsi - ioapics[i].gsi_base;
            return &ioapics[i];
        }
    }
    return NULL;
}

bool apic_active(void){
    return active;
}

uint32_t lapic_id(void){
    return lapic_read(LAPIC_ID) >> 24;
}

void lapic_eoi(void){
    lapic_write(LAPIC_EOI, 0);
}

uint64_t lapic_timer_hz(void){
    return timer_hz;
}

// MPS INTI flags to LVT / redirection-entry bits. "Conforms to the
// bus" is ISA: active high, edge.
static uint32_t inti_bits(uint16_t flags){
    uint32_t bits = 0;
    if((flags & ACPI_INTI_POLARITY_MASK) == ACPI_INTI_POLARITY_LOW){
        bits |= LVT_ACTIVE_LOW;
    }
    if((flags & ACPI_INTI_TRIGGER_MASK) == ACPI_INTI_TRIGGER_LEVEL){
        bits |= LVT_LEVEL;
    }
    return bits;
}

void lapic_init_cpu(void){
    wrmsr(MSR_APIC_BASE, rdmsr(MSR_APIC_BASE) | APIC_BASE_ENABLE);
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | APIC_VECTOR_SPURIOUS);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED | APIC_VECTOR_TIMER);
    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    // LAPIC NMI entries name a processor UID (0xFF = all); map ours
    // through the MADT processor list.
    const struct acpi_info* a = acpi_info();
    uint32_t id = lapic_id();
    int uid = -1;
    for(int i = 0; i < a->cpu_count; i++){
        if(a->cpus[i].apic_id == id){
            uid = a->cpus[i].acpi_id;
        }
    }
    for(int i = 0; i < a->lapic_nmi_count; i++){
        const struct acpi_lapic_nmi* n = &a->lapic_nmis[i];
        if(n->lint > 1 || (n->acpi_id != 0xFF && n->acpi_id != uid)){
            continue;
        }
        lapic_write(n->lint ? LAPIC_LVT_LINT1 : LAPIC_LVT_LINT0,
                    LVT_DELIVERY_NMI | inti_bits(n->flags));
    }
    lapic_eoi();
}

// Re-run the 8259 init sequence with vectors 0x30/0x38 and mask
// every line. kernel.asm put them at 0x20/0x28, which the IOAPIC
// now uses.
static void pic_disable(void){
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, APIC_VECTOR_PIC_BASE);
    outb(0xA1, APIC_VECTOR_PIC_BASE + 8);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

// An ISA IRQ whose identity GSI another IRQ was moved onto (IRQ2
// when IRQ0 goes to GSI 2) has no line of its own.
static bool isa_irq_shadowed(uint8_t irq){
    const struct acpi_info* a = acpi_info();
    for(int i = 0; i < a->override_count; i++){
        if(a->overrides[i].irq == irq){
            return false;
        }
    }
    for(int i = 0; i < a->override_count; i++){
        if(a->overrides[i].gsi == irq){
            return true;
        }
    }
    return irq == 2;
}

static int ioapic_route(uint8_t irq, bool masked){
    if(isa_irq_shadowed(irq)){
        return -EINVARG;
    }
    uint16_t flags;
    uint32_t pin;
    struct ioapic* io = ioapic_for_gsi(acpi_isa_irq_to_gsi(irq, &flags), &pin);
    if(!io){
        return -ENOENT;
    }
    // Fixed delivery, physical destination mode.
    uint32_t low = (0x20 + irq) | inti_bits(flags) | (masked ? LVT_MASKED : 0);
    ioapic_write(io, IOAPIC_REG_REDIR(pin) + 1, irq_dest << 24);
    ioapic_write(io, IOAPIC_REG_REDIR(pin), low);
    return 0;
}

int ioapic_set_masked(uint8_t isa_irq, bool masked){
    if(!active || isa_irq > 15){
        return -EINVARG;
    }
    spin_lock(&ioapic_lock);
    int res = ioapic_route(isa_irq, masked);
    spin_unlock(&ioapic_lock);
    return res;
}

// The disk interrupts: a PCI INTx GSI, or an ATA
// channel's ISA IRQ moved off its 0x20 + n vector.
int ioapic_route_gsi(uint32_t gsi, uint8_t vector, uint16_t inti_flags, bool masked){
    uint32_t pin;
    struct ioapic* io = active ? ioapic_for_gsi(gsi, &pin) : NULL;
    if(!io){
        return -ENOENT;
    }
    spin_lock(&ioapic_lock);
    ioapic_write(io, IOAPIC_REG_REDIR(pin) + 1, irq_dest << 24);
    ioapic_write(io, IOAPIC_REG_REDIR(pin), vector | inti_bits(inti_flags) | (masked ? LVT_MASKED : 0));
    spin_unlock(&ioapic_lock);
    return 0;
}

// Send a GSI to another CPU. Only the destination
// changes, so the vector, trigger and mask stay as routed.
int ioapic_set_dest(uint32_t gsi, uint32_t apic_id){
    uint32_t pin;
    struct ioapic* io = active ? ioapic_for_gsi(gsi, &pin) : NULL;
    if(!io){
        return -ENOENT;
    }
    spin_lock(&ioapic_lock);
    ioapic_write(io, IOAPIC_REG_REDIR(pin) + 1, apic_id << 24);
    spin_unlock(&ioapic_lock);
    return 0;
}

uint32_t lapic_set_tpr(uint32_t tpr){
    uint32_t old = lapic_read(LAPIC_TPR);
    lapic_write(LAPIC_TPR, tpr);
    (void)lapic_read(LAPIC_TPR);    // in effect before a following sti
    return old;
}

int apic_init(void){
    const struct acpi_info* a = acpi_info();
    if(!a->have_madt || !a->lapic_phys || !a->ioapic_count){
        serial_write("apic: no MADT LAPIC/IOAPIC, staying on the 8259s and the PIT\n");
        return -ENOENT;
    }
    lapic_mmio = paging_map_mmio(a->lapic_phys, 0x1000, PAGING_CACHE_DISABLED);
    if(!lapic_mmio){
        return -ENOMEM;
    }
    for(int i = 0; i < a->ioapic_count; i++){
        struct ioapic* io = &ioapics[ioapic_count];
        io->mmio = paging_map_mmio(a->ioapics[i].phys, 0x20, PAGING_CACHE_DISABLED);
        if(!io->mmio){
            continue;
        }
        io->gsi_base = a->ioapics[i].gsi_base;
        io->entries  = ((ioapic_read(io, IOAPIC_REG_VERSION) >> 16) & 0xFF) + 1;
        for(uint32_t pin = 0; pin < io->entries; pin++){
            ioapic_write(io, IOAPIC_REG_REDIR(pin), LVT_MASKED);
        }
        ioapic_count++;
    }
    if(!ioapic_count){
        return -ENOMEM;
    }

    pic_disable();
    lapic_init_cpu();
    irq_dest = lapic_id();
    active = true;
    for(int irq = 0; irq < 16; irq++){
        ioapic_route(irq, true);
    }

    serial_write("apic: lapic id ");
    serial_write(itoa(irq_dest));
    serial_write(" version ");
    serial_write_hex(lapic_read(LAPIC_VERSION) & 0xFF);
    serial_write(", ioapic gsi ");
    serial_write(itoa(ioapics[0].gsi_base));
    serial_write("-");
    serial_write(itoa(ioapics[0].gsi_base + ioapics[0].entries - 1));
    serial_write(", 8259 masked ");
    serial_write_hex((insb(0xA1) << 8) | insb(0x21));
    serial_write("\n");
    return 0;
}

// Divide by 16, one-shot from 0xFFFFFFFF, read as an up-counter
// across PIT channel 2 windows (pit_measure_hz, median of three 50 ms
// windows). Not against the TSC: under QEMU TCG the TSC follows
// the host clock while the PIT and the LAPIC timer follow QEMU's
// virtual clock, and the TSC route came out 10 to 25 % fast.
static uint64_t lapic_timer_elapsed(void){
    return 0xFFFFFFFFu - lapic_read(LAPIC_TIMER_CURRENT);
}

int lapic_timer_calibrate(void){
    lapic_write(LAPIC_TIMER_DIVIDE, LAPIC_DIVIDE_16);
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED | APIC_VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
    timer_hz = pit_measure_hz(lapic_timer_elapsed);
    lapic_write(LAPIC_TIMER_INIT, 0);
    serial_write("apic: lapic timer ");
    serial_write(itoa((int)(timer_hz / 1000)));
    serial_write(" kHz (divide 16), measured against PIT channel 2\n");
    return timer_hz ? 0 : -EIO;
}

// Interprocessor interrupts through the ICR. The high
// half (destination) first: writing the low half sends.
#define LAPIC_ICR_LOW         0x300
#define LAPIC_ICR_HIGH        0x310
#define ICR_PENDING           (1u << 12)
#define ICR_ASSERT            (1u << 14)
#define ICR_DELIVERY_NMI      (4u << 8)
#define ICR_DELIVERY_INIT     (5u << 8)
#define ICR_DELIVERY_STARTUP  (6u << 8)
#define ICR_ALL_BUT_SELF      (3u << 18)

static void lapic_icr_wait(void){
    for(int i = 0; i < 1000000 && (lapic_read(LAPIC_ICR_LOW) & ICR_PENDING); i++){
        __asm__ volatile("pause");
    }
}

static void lapic_icr_send(uint32_t apic_id, uint32_t low){
    lapic_icr_wait();
    lapic_write(LAPIC_ICR_HIGH, apic_id << 24);
    lapic_write(LAPIC_ICR_LOW, low);
    lapic_icr_wait();
}

void lapic_send_ipi(uint32_t apic_id, uint8_t vector){
    lapic_icr_send(apic_id, ICR_ASSERT | vector);
}

void lapic_send_init(uint32_t apic_id){
    lapic_icr_send(apic_id, ICR_ASSERT | ICR_DELIVERY_INIT);
}

void lapic_send_startup(uint32_t apic_id, uint32_t page){
    lapic_icr_send(apic_id, ICR_ASSERT | ICR_DELIVERY_STARTUP | (page & 0xFF));
}

void lapic_send_nmi_all_others(void){
    lapic_icr_wait();
    lapic_write(LAPIC_ICR_LOW, ICR_ALL_BUT_SELF | ICR_ASSERT | ICR_DELIVERY_NMI);
}

void lapic_timer_start(uint32_t hz){
    uint64_t count = (timer_hz + hz / 2) / hz;
    if(!count || count > 0xFFFFFFFFu){
        count = count ? 0xFFFFFFFFu : 1;
    }
    lapic_write(LAPIC_TIMER_DIVIDE, LAPIC_DIVIDE_16);
    lapic_write(LAPIC_LVT_TIMER, LVT_TIMER_PERIODIC | APIC_VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_INIT, (uint32_t)count);
}
