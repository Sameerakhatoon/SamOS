// Exception and IRQ dispatch (see trap.h, vectors.S).
//
// Synchronous exceptions at EL1 decode ESR_EL1 and dump the frame;
// only an SVC with a handler installed returns. IRQs are acknowledged
// at the GIC, handed to the INTID's handler, ended, and then the
// scheduler may pick another thread's frame to resume.
#include "arch/aarch64/trap.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/gicv3.h"
#include "arch/aarch64/sched.h"

#define IRQ_MAX 1020            // INTIDs 0-1019: SGI, PPI, SPI

// ESR_EL1.EC, bits [31:26]
#define EC_UNKNOWN      0x00    // also UDF and other undefined encodings
#define EC_SVC64        0x15
#define EC_IABT_LOWER   0x20
#define EC_IABT_CUR     0x21
#define EC_PC_ALIGN     0x22
#define EC_DABT_LOWER   0x24
#define EC_DABT_CUR     0x25
#define EC_SP_ALIGN     0x26
#define EC_BRK64        0x3c

static irq_handler irq_handlers[IRQ_MAX];
static void (*svc_handler)(struct trap_frame* f, uint32_t imm);

void aarch64_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr);

static const char* const vector_names[4] = { "sync", "irq", "fiq", "serror" };
static const char* const vector_groups[4] = {
    "current EL, SP_EL0", "current EL, SP_ELx", "lower EL, AArch64", "lower EL, AArch32",
};

void aarch64_halt(void)
{
    for (;;)
        __asm__ volatile("msr daifset, #0xf\n\twfi");
}

static const char* ec_name(uint32_t ec)
{
    switch (ec) {
    case EC_UNKNOWN:    return "undefined instruction";
    case EC_SVC64:      return "svc";
    case EC_IABT_LOWER: return "instruction abort, lower EL";
    case EC_IABT_CUR:   return "instruction abort";
    case EC_PC_ALIGN:   return "pc alignment fault";
    case EC_DABT_LOWER: return "data abort, lower EL";
    case EC_DABT_CUR:   return "data abort";
    case EC_SP_ALIGN:   return "sp alignment fault";
    case EC_BRK64:      return "brk";
    default:            return "other";
    }
}

// Data/instruction fault status code, ISS bits [5:0].
static const char* fsc_name(uint32_t fsc)
{
    if (fsc <= 0x03) return "address size fault";
    if (fsc <= 0x07) return "translation fault";
    if (fsc <= 0x0b) return "access flag fault";
    if (fsc <= 0x0f) return "permission fault";
    if (fsc == 0x10) return "synchronous external abort";
    if (fsc == 0x21) return "alignment fault";
    return "other";
}

static void print_cause(const struct trap_frame* f)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    uint32_t iss = f->esr & 0x1ffffff;
    switch (ec) {
    case EC_UNKNOWN:
        // MMU off: ELR is a physical RAM address, 4-byte aligned.
        console_printf("  cause: undefined instruction 0x%08x\n", *(const volatile uint32_t*)f->elr);
        break;
    case EC_SVC64:
    case EC_BRK64:
        console_printf("  cause: %s #0x%x\n", ec_name(ec), iss & 0xffff);
        break;
    case EC_DABT_LOWER:
    case EC_DABT_CUR:
        console_printf("  cause: %s, %s, DFSC 0x%x (%s), FAR 0x%lx\n", ec_name(ec),
                       (iss >> 6) & 1 ? "write" : "read", iss & 0x3f, fsc_name(iss & 0x3f), f->far);
        break;
    case EC_IABT_LOWER:
    case EC_IABT_CUR:
        console_printf("  cause: %s, IFSC 0x%x (%s), FAR 0x%lx\n", ec_name(ec), iss & 0x3f,
                       fsc_name(iss & 0x3f), f->far);
        break;
    default:
        console_printf("  cause: %s\n", ec_name(ec));
        break;
    }
}

void trap_dump(const struct trap_frame* f, uint64_t vector)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    console_printf("\nexception: %s (%s)\n", vector_names[vector & 3], vector_groups[(vector >> 2) & 3]);
    console_printf("  ESR 0x%lx (EC 0x%x, %s)\n", f->esr, ec, ec_name(ec));
    console_printf("  ELR 0x%lx\n  FAR 0x%lx\n  SPSR 0x%lx\n", f->elr, f->far, f->spsr);
    print_cause(f);
    for (int i = 0; i < 31; i++) {
        console_printf("  x%d%s 0x%016lx", i, i < 10 ? " " : "", f->x[i]);
        if (i % 3 == 2 || i == 30)
            console_printf("\n");
    }
    console_printf("  sp  0x%016lx\n", f->sp);
}

static struct trap_frame* sync_exception(struct trap_frame* f)
{
    uint32_t ec = (f->esr >> 26) & 0x3f;
    if (ec == EC_SVC64 && svc_handler) {
        svc_handler(f, f->esr & 0xffff);   // ELR is already past the svc
        return f;
    }
    trap_dump(f, VECTOR_EL1H_SYNC);
    console_printf("exception: halted\n");
    aarch64_halt();
}

static struct trap_frame* irq(struct trap_frame* f)
{
    uint32_t intid = gic_ack();
    if (intid >= GIC_INTID_SPURIOUS)
        return f;                           // nothing pending: no EOI
    if (intid < IRQ_MAX && irq_handlers[intid])
        irq_handlers[intid](f);
    else
        console_printf("irq: unexpected INTID %u\n", intid);
    gic_eoi(intid);
    return sched_switch(f);
}

struct trap_frame* aarch64_trap(struct trap_frame* f, uint64_t vector)
{
    struct trap_frame* next = vector == VECTOR_EL1H_IRQ ? irq(f) : sync_exception(f);
    // vectors.S puts SP just above the frame for an EL1 return.
    if ((next->spsr & 0xf) == SPSR_EL1H && next->sp != (uint64_t)next + TRAP_FRAME_SIZE) {
        console_printf("trap: frame at 0x%lx has sp 0x%lx\n", (uint64_t)next, next->sp);
        aarch64_halt();
    }
    return next;
}

int irq_register(uint32_t intid, irq_handler handler)
{
    if (intid >= IRQ_MAX)
        return -1;
    irq_handlers[intid] = handler;
    return 0;
}

void trap_set_svc_handler(void (*handler)(struct trap_frame* f, uint32_t imm))
{
    svc_handler = handler;
}

// The unexpected entries (vectors.S exception_common): no frame.
void aarch64_exception(uint64_t vector, uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr)
{
    console_printf("\nexception: %s (%s)\n", vector_names[vector & 3], vector_groups[(vector >> 2) & 3]);
    console_printf("  ESR 0x%lx (EC 0x%lx)\n  ELR 0x%lx\n  FAR 0x%lx\n  SPSR 0x%lx\n",
                   esr, esr >> 26, elr, far, spsr);
    console_printf("exception: unexpected vector %lu, halted\n", vector);
    aarch64_halt();
}
