#ifndef DISK_IRQWAIT_H
#define DISK_IRQWAIT_H

// Disk completions by interrupt, through the IOAPIC.
//
// There is one kernel stack, so a syscall that reads the disk
// cannot sleep and let another task run: the driver still waits in
// place, but on the interrupt instead of the device. The handler acks
// the device and sets `pending`; the wait opens interrupts with
// `sti; pause; cli` and reads only that flag. The LAPIC TPR is
// APIC_TPR_DISK_WAIT meanwhile, so only the disk vectors (0x50-0x5F)
// are taken: the keyboard, the mouse and the scheduler tick stay
// pending in the LAPIC as they do with IF clear, and no other handler
// runs in the middle of a syscall.
//
// Each interrupt is checked against the device (`done`), so a late
// interrupt from an earlier command does not end a wait early, and the
// driver's own status checks still run afterwards. When the device is
// done but no interrupt came within one grace period the wait returns
// and counts as polled; DISK_IRQ_MAX_MISSES of those in a row and the
// driver polls for good. A device that never finishes runs into the
// usual disk_poll_deadline. Without the APIC (samos.legacy_pic, no
// MADT) nothing is enabled and the drivers poll as in the storage work.
//
// Header only, so no Makefile change.

#include <stdbool.h>
#include <stdint.h>
#include "disk/poll.h"
#include "io/apic.h"
#include "io/serial.h"
#include "acpi/acpi.h"
#include "bootmarker.h"
#include "boot/cmdline.h"
#include "string/string.h"

#define DISK_VECTOR_AHCI          (APIC_VECTOR_DISK_BASE + 0)
#define DISK_VECTOR_PATA(channel) (APIC_VECTOR_DISK_BASE + 2 + (channel))

// 2^24 cycles: 3 to 17 ms between 1 and 5 GHz.
#define DISK_IRQ_GRACE_CYCLES     (1ULL << 24)
#define DISK_IRQ_MAX_MISSES       3

struct disk_irq {
    volatile uint32_t   pending;    // set by the handler
    bool                enabled;
    uint8_t             misses;     // done without an interrupt, in a row
    const char*         name;
    boot_marker_stage_t marker;     // counts completions taken at the interrupt
};

// e2e/185: route the line but leave it masked, for the fallback.
static inline bool disk_irq_masked_by_cmdline(void){
    return boot_cmdline_has("samos.disk_irq_masked");
}

static inline void disk_irq_count(boot_marker_stage_t stage){
    volatile uint64_t* slots = (volatile uint64_t*)(0xFFFF800000000000ULL + BOOT_MARKER_BASE);
    uint64_t v = slots[stage];
    uint32_t n = (v >> 24) == ((BOOT_MARKER_MAGIC_PREFIX << 8) | stage) ? (uint32_t)(v & 0xFFFFFF) : 0;
    boot_marker_set(stage, n + 1);
}

// "disk: ahci completions by interrupt, GSI 16 level active-high, vector 80"
static inline void disk_irq_report(struct disk_irq* q, uint32_t gsi, uint16_t inti, uint8_t vector, bool masked){
    serial_write("disk: ");
    serial_write(q->name);
    serial_write(" completions by interrupt, GSI ");
    serial_write(itoa((int)gsi));
    serial_write((inti & ACPI_INTI_TRIGGER_MASK) == ACPI_INTI_TRIGGER_LEVEL ? " level" : " edge");
    serial_write((inti & ACPI_INTI_POLARITY_MASK) == ACPI_INTI_POLARITY_LOW ? " active-low" : " active-high");
    serial_write(", vector ");
    serial_write(itoa(vector));
    serial_write(masked ? ", line masked (samos.disk_irq_masked)\n" : "\n");
}

// Before issuing a command: drop a flag a handler set since the last wait.
static inline void disk_irq_arm(struct disk_irq* q){
    q->pending = 0;
}

static inline void disk_irq_signal(struct disk_irq* q){
    q->pending = 1;
}

// Wait for the command just issued. True when it completed at an
// interrupt; false when the caller has to poll (not enabled, no
// interrupt, or the device is not done by the deadline).
static inline bool disk_irq_wait(struct disk_irq* q, bool (*done)(void*), void* ctx){
    if(!q->enabled){
        return false;
    }
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    uint32_t tpr = lapic_set_tpr(APIC_TPR_DISK_WAIT);
    uint64_t end = disk_poll_deadline();
    uint64_t check = read_tsc() + DISK_IRQ_GRACE_CYCLES;
    bool seen_done = false, irq = false;
    while(!disk_poll_expired(end)){
        __asm__ volatile("sti; nop; cli; pause" ::: "memory");
        if(q->pending){
            q->pending = 0;
            if(done(ctx)){
                irq = true;
                break;
            }
        }
        if(disk_poll_expired(check)){
            if(seen_done){
                break;          // done a grace period ago, still no interrupt
            }
            seen_done = done(ctx);
            check = read_tsc() + DISK_IRQ_GRACE_CYCLES;
        }
    }
    lapic_set_tpr(tpr);
    if(rflags & 0x200){
        __asm__ volatile("sti");
    }
    if(irq){
        q->misses = 0;
        disk_irq_count(q->marker);
        return true;
    }
    disk_irq_count(BM_FEATURE_DISK_IRQ_POLLED);
    if(seen_done && ++q->misses >= DISK_IRQ_MAX_MISSES){
        q->enabled = false;
        serial_write("disk: ");
        serial_write(q->name);
        serial_write(" completion interrupt missing, polling from now on\n");
    }
    return false;
}

#endif
