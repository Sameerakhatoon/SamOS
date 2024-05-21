#ifndef DISK_IRQWAIT_H
#define DISK_IRQWAIT_H

// Disk completions by interrupt, through the IOAPIC.
//
// There is one kernel stack per CPU, not one per task, so
// a syscall that reads the disk cannot sleep and let another task run
// on its CPU: the driver still waits in
// place, but on the interrupt instead of the device. The handler acks
// the device and sets `pending`; the wait opens interrupts for one
// instruction at a time (arch_irq_wait_window; on x86 `sti; nop; cli;
// pause`, the nop gives the one-instruction sti shadow something to
// cover) and reads only that flag. Between arch_irq_wait_begin
// and _end only the completion lines are taken (x86: the LAPIC TPR is
// APIC_TPR_DISK_WAIT, so only vectors 0x50-0x5F): the keyboard, the
// mouse and the scheduler tick stay pending as they do with
// interrupts off. Two other handlers can run there: a virtio-input
// line sharing a completion vector, which only sends an IPI the wait
// also holds off, and xHCI's (a completion slot of
// its own), which takes its controller's `usb` lock to drain the event
// ring, then signals a mass storage wait or sends that same IPI for
// HID reports and plug events.
//
// Each interrupt is checked against the device (`done`), so a late
// interrupt from an earlier command does not end a wait early, and the
// driver's own status checks still run afterwards. When the device is
// done but no interrupt came within one grace period the wait returns
// and counts as polled; DISK_IRQ_MAX_MISSES of those in a row and the
// driver polls for good. A device that never finishes runs into the
// usual disk_poll_deadline. Without a routable controller
// (samos.legacy_pic, no MADT) nothing is enabled and the drivers poll
// as in the storage work.
//
// Header only, so no Makefile change.

#include <stdbool.h>
#include <stdint.h>
#include "arch/arch.h"
#include "disk/poll.h"
#include "io/serial.h"
#include "acpi/acpi.h"
#include "bootmarker.h"
#include "boot/cmdline.h"
#include "string/string.h"
#include "cpu/percpu.h"

// With several CPUs a wait runs under the disk lock
// (interrupts off, opened only in the limited window below) on
// whichever CPU made the request. disk_irq_arm points the line at
// that CPU before the command goes out, so the completion is taken
// by the CPU that waits for it; the BSP may be spinning on the disk
// lock with interrupts off meanwhile. The disk lock is one for all
// disks, so one line has one waiter at a time.

// The drivers' completion line slots (arch_irq_attach_gsi).
// Slots 4-11 are virtio_pci.c's INTx lines (VIRTIO_SLOT_BASE).
#define DISK_SLOT_AHCI            0
#define DISK_SLOT_PATA(channel)   (2 + (channel))

// 2^24 cycles: 3 to 17 ms between 1 and 5 GHz.
#define DISK_IRQ_GRACE_CYCLES     (1ULL << 24)
#define DISK_IRQ_MAX_MISSES       3

struct disk_irq {
    volatile uint32_t   pending;    // set by the handler
    bool                enabled;
    uint8_t             misses;     // done without an interrupt, in a row
    const char*         name;
    boot_marker_stage_t marker;     // counts completions taken at the interrupt
    uint32_t            gsi;        // The routed line
    struct cpu*         dest;       // CPU it is sent to now
    volatile uint32_t   signal_cpu; // CPU index the handler ran on
};

// e2e/185: route the line but leave it masked, for the fallback.
static inline bool disk_irq_masked_by_cmdline(void){
    return boot_cmdline_has("samos.disk_irq_masked");
}

static inline void disk_irq_count(boot_marker_stage_t stage){
    volatile uint64_t* slots = boot_marker_slots();
    uint64_t v = slots[stage];
    uint32_t n = (v >> 24) == ((BOOT_MARKER_MAGIC_PREFIX << 8) | stage) ? (uint32_t)(v & 0xFFFFFF) : 0;
    boot_marker_set(stage, n + 1);
}

// "disk: ahci completions by interrupt, GSI 16 level active-high, vector 80"
// Called once the line is routed (to the BSP, by arch_irq_attach_gsi).
static inline void disk_irq_report(struct disk_irq* q, uint32_t gsi, uint16_t inti, int line, bool masked){
    q->gsi  = gsi;
    q->dest = this_cpu();
    serial_write("disk: ");
    serial_write(q->name);
    serial_write(" completions by interrupt, GSI ");
    serial_write(itoa((int)gsi));
    serial_write((inti & ACPI_INTI_TRIGGER_MASK) == ACPI_INTI_TRIGGER_LEVEL ? " level" : " edge");
    serial_write((inti & ACPI_INTI_POLARITY_MASK) == ACPI_INTI_POLARITY_LOW ? " active-low" : " active-high");
    serial_write(", vector ");
    serial_write(itoa(line));
    serial_write(masked ? ", line masked (samos.disk_irq_masked)\n" : "\n");
}

// Before issuing a command: drop a flag a handler set since the last wait.
// And steer the line to this CPU (see the top).
static inline void disk_irq_arm(struct disk_irq* q){
    q->pending = 0;
    if(q->enabled && smp_active()){
        struct cpu* me = this_cpu();
        if(me != q->dest && arch_irq_set_cpu(q->gsi, me) == 0){
            q->dest = me;
        }
    }
}

static inline void disk_irq_signal(struct disk_irq* q){
    q->signal_cpu = this_cpu()->index;
    q->pending = 1;
}

// Wait for the command just issued. True when it completed at an
// interrupt; false when the caller has to poll (not enabled, no
// interrupt, or the device is not done by the deadline).
static inline bool disk_irq_wait(struct disk_irq* q, bool (*done)(void*), void* ctx){
    if(!q->enabled){
        return false;
    }
    uint64_t state = arch_irq_wait_begin();
    uint64_t end = disk_poll_deadline();
    uint64_t check = arch_counter() + DISK_IRQ_GRACE_CYCLES;
    bool seen_done = false, irq = false;
    while(!disk_poll_expired(end)){
        arch_irq_wait_window();
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
            check = arch_counter() + DISK_IRQ_GRACE_CYCLES;
        }
    }
    arch_irq_wait_end(state);
    if(irq){
        q->misses = 0;
        disk_irq_count(q->marker);
        // (e2e/194): taken on the AP that waited, or elsewhere.
        uint32_t me = this_cpu()->index;
        if(q->signal_cpu != me){
            disk_irq_count(BM_FEATURE_DISK_IRQ_OTHER_CPU);
        } else if(me != 0){
            disk_irq_count(BM_FEATURE_DISK_IRQ_AP);
        }
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
