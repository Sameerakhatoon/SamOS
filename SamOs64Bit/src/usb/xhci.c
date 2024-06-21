// XHCI host controller driver (xHCI 1.x; PCI class 0C/03/30).
//
// Bring-up (spec 4.2): take the controller from the firmware (USB
// Legacy Support capability), stop and reset it, then the device
// context base array (with scratchpad pages when it asks for them),
// the command ring, one event ring segment on interrupter 0, run.
// Every ring is one 4 KiB page of 256 TRBs, the last a Link TRB back
// to the start; contexts are 32 or 64 bytes (HCCPARAMS1.CSZ).
//
// Ports: the Supported Protocol capabilities say which ports are USB 2
// and which USB 3. A connected port is reset when it is not already
// enabled (USB 3 ports enable themselves after link training), then
// Enable Slot, Address Device, the device descriptor (the first 8
// bytes, then the whole with the right EP0 packet size), the first
// configuration, SET_CONFIGURATION, and the first class driver that
// takes an interface. No hubs: a hub is enumerated and left unbound.
//
// Events: one consumer at a time under hc->lock (the event ring and
// every producer ring). Completion of a command or a transfer is a
// flag the waiter reads; nothing runs from the event loop itself.
// Commands are one at a time under hc->cmd_lock, waited for by polling.
//
// Interrupts, in this order: MSI-X, MSI (both need arch_irq_msi;
// steered to the waiting CPU like the IOAPIC lines), the INTx line
// through the IOAPIC where pci_intx_gsi knows the chipset (QEMU q35
// and pc), else none. They are a completion slot of the disk wait:
// the handler acks the interrupter, drains the event ring,
// signals a mass storage wait and hands HID reports and plug events
// to the BSP with ARCH_IPI_INPUT, as virtio-input does.
// Without any, the BSP's scheduler tick runs that work (polled).
// samos.usb_intx and samos.usb_poll force the last two (tests).

#include "usb.h"
#include "xhci.h"
#include "arch/arch.h"
#include "io/pci.h"
#include "io/serial.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "lib/spinlock/spinlock.h"
#include "disk/irqwait.h"
#include "cpu/smp.h"
#include "task/task.h"
#include "boot/cmdline.h"
#include "string/string.h"
#include "status.h"

#define XHCI_MAX_HCS        4
#define XHCI_IRQ_SLOT_BASE  12     // disk/irqwait.h: 0 AHCI, 2-3 PATA, 4-11 virtio
#define XHCI_IRQ_SLOTS      2

static struct xhci* xhci_hcs[XHCI_MAX_HCS];
static int xhci_hc_count;
static struct cpu* usb_bsp;

// ---- helpers ----------------------------------------------------------

static uint64_t dma_phys(void* p){ return paging_kernel_virt_to_phys(p); }

// Every page the controller reads or writes: rings, contexts,
// the DCBAA, scratchpads, transfer buffers. Without AC64 (HCCPARAMS1)
// it drives 32 address bits only, so a page at or above dma_limit is
// given back and NULL returned: the bring-up or the device then fails
// on its no-memory path instead of the controller getting a truncated
// address. kpage_zalloc has no below-4-GiB request; the heap hands out
// low frames first, so this only bites once those are gone.
// For controllers that do not snoop the caches (arch.h's
// arch_dma_*, no-ops elsewhere) the zeroes are written back at once,
// so no dirty line of them can later land on what the controller
// wrote; every structure below is cleaned after the CPU writes it and
// dropped before the CPU reads what the controller wrote.
static void* hc_dma_page(struct xhci* hc){
    void* p = kpage_zalloc(4096);
    if(p && hc->dma_limit && dma_phys(p) + 4096 > hc->dma_limit){
        kfree(p);
        p = NULL;
        if(!hc->dma_refused){
            hc->dma_refused = true;
            serial_write("xhci: a DMA page above what the 32-bit controller reaches, refused\n");
        }
    }
    if(p){
        arch_dma_clean(p, 4096);
    }
    return p;
}

void* usb_dma_page(struct usb_device* dev){
    return hc_dma_page(dev->hc);
}

static uint64_t xhci_deadline(uint32_t ms){
    return arch_counter() + arch_counter_hz() / 1000 * ms;
}

static bool xhci_expired(uint64_t deadline){
    return (int64_t)(arch_counter() - deadline) > 0;
}

static uint32_t op_read(struct xhci* hc, uint32_t off){ return mmio_read32(hc->op + off); }
static void op_write(struct xhci* hc, uint32_t off, uint32_t v){ mmio_write32(hc->op + off, v); }

// 64-bit registers as two dwords, low first (spec 5.1).
static void write64(volatile uint8_t* p, uint64_t v){
    mmio_write32(p, (uint32_t)v);
    mmio_write32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t portsc_read(struct xhci* hc, uint8_t port){
    return op_read(hc, XHCI_OP_PORTSC(port));
}

// A PORTSC write that changes nothing: PED and the change bits are
// write-1-to-clear, PR and WPR start resets.
static uint32_t portsc_neutral(uint32_t v){
    return v & ~(PORTSC_PED | PORTSC_PR | PORTSC_LWS | PORTSC_CHANGE | PORTSC_WPR);
}

static void ring_init(struct xhci_ring* r, void* page){
    r->trb   = page;
    r->phys  = dma_phys(page);
    r->enq   = 0;
    r->cycle = 1;
}

// Write one TRB at the enqueue pointer, cycle bit last; returns its
// physical address. The Link TRB at the end keeps the chain bit of
// the TRB before it, so a TD may cross it.
static uint64_t ring_push(struct xhci_ring* r, uint64_t param, uint32_t status, uint32_t control){
    volatile struct xhci_trb* t = &r->trb[r->enq];
    uint64_t phys = r->phys + r->enq * sizeof(struct xhci_trb);
    t->param  = param;
    t->status = status;
    smp_wmb();
    t->control = (control & ~TRB_CYCLE) | r->cycle;
    arch_dma_clean(t, sizeof(*t));
    if(++r->enq == XHCI_RING_TRBS - 1){
        volatile struct xhci_trb* link = &r->trb[r->enq];
        link->param  = r->phys;
        link->status = 0;
        smp_wmb();
        link->control = TRB_TYPE(TRB_LINK) | TRB_TC | (control & TRB_CH) | r->cycle;
        arch_dma_clean(link, sizeof(*link));
        r->enq = 0;
        r->cycle ^= 1;
    }
    return phys;
}

static void doorbell(struct xhci* hc, uint8_t slot, uint32_t target){
    smp_wmb();
    mmio_write32(hc->db + 4u * slot, target);
}

static uint8_t* in_ctx(struct usb_device* d, int index){
    return (uint8_t*)d->in_page + (index + 1) * d->hc->ctx_size;
}

static uint8_t* dev_ctx(struct usb_device* d, int index){
    return (uint8_t*)d->ctx_page + index * d->hc->ctx_size;
}

// ---- events -------------------------------------------------------------

static void xhci_transfer_event(struct xhci* hc, volatile struct xhci_trb* e){
    uint8_t  slot = e->control >> 24;
    uint8_t  dci  = (e->control >> 16) & 0x1F;
    uint8_t  cc   = e->status >> 24;
    uint32_t residual = e->status & 0xFFFFFF;
    struct usb_device* d = hc->slot_dev[slot];
    struct usb_endpoint* ep = d ? d->ep[dci] : NULL;
    if(!ep || !ep->busy || ep->done){
        return;
    }
    int idx = -1;
    for(int i = 0; i < ep->td_count; i++){
        if(ep->td_trb[i] == e->param){
            idx = i;
        }
    }
    if(idx < 0){
        return;                     // not this transfer's TRB: stale
    }
    bool last = idx == ep->td_count - 1;
    if(ep->control){
        // Setup, data, status: a short data stage still ends with the
        // status stage's event.
        if(!last && cc == XHCI_CC_SHORT_PACKET){
            ep->actual = ep->td_len[idx] - residual;
            return;
        }
        if(!last && cc == XHCI_CC_SUCCESS){
            return;
        }
    } else {
        uint32_t n = 0;
        for(int i = 0; i < idx; i++){
            n += ep->td_len[i];
        }
        ep->actual = n + ep->td_len[idx] - (residual > ep->td_len[idx] ? ep->td_len[idx] : residual);
        if(!last && cc == XHCI_CC_SUCCESS){
            return;
        }
    }
    ep->cc = cc;
    smp_wmb();
    ep->done = true;
    if(ep->input){
        hc->input_pending = true;
    }
}

// Drain the event ring; hc->lock held.
static void xhci_drain_locked(struct xhci* hc){
    bool any = false;
    for(int guard = 0; guard < XHCI_RING_TRBS * 4; guard++){
        volatile struct xhci_trb* e = &hc->ev[hc->ev_deq];
        arch_dma_inval(e, sizeof(*e));
        uint32_t control = e->control;
        if((control & TRB_CYCLE) != hc->ev_cycle){
            break;
        }
        smp_rmb();
        switch(TRB_GET_TYPE(control)){
            case TRB_TRANSFER_EVENT:
                xhci_transfer_event(hc, e);
                break;
            case TRB_COMMAND_COMPLETION:
                if(e->param == hc->cmd_trb){
                    hc->cmd_result = e->status >> 24 | (control & 0xFF000000);
                    smp_wmb();
                    hc->cmd_done = true;
                }
                break;
            case TRB_PORT_STATUS_CHANGE:
                hc->port_change = true;
                hc->input_pending = true;
                break;
            default:
                break;
        }
        if(++hc->ev_deq == XHCI_RING_TRBS){
            hc->ev_deq = 0;
            hc->ev_cycle ^= 1;
        }
        any = true;
    }
    if(any){
        write64(hc->rt + XHCI_IR0_ERDP, (hc->ev_phys + hc->ev_deq * sizeof(struct xhci_trb)) | XHCI_ERDP_EHB);
    }
}

static void xhci_poll(struct xhci* hc){
    spin_lock(&hc->lock);
    xhci_drain_locked(hc);
    spin_unlock(&hc->lock);
    if(hc->input_pending){
        usb_input_kick(hc);
    }
}

// ---- interrupts ----------------------------------------------------------

// Possibly inside a disk wait (any CPU): ack, drain, signal, hand over.
static void xhci_irq(struct xhci* hc){
    op_write(hc, XHCI_OP_USBSTS, USBSTS_EINT);
    mmio_write32(hc->rt + XHCI_IR0_IMAN, XHCI_IMAN_IE | XHCI_IMAN_IP);
    xhci_poll(hc);
    disk_irq_signal(&hc->irq);
}

// The frame carries no line number, so each slot has its own handler.
static void xhci_irq_slot(int n){
    for(int i = 0; i < xhci_hc_count; i++){
        if(xhci_hcs[i]->irq_slot == n){
            xhci_irq(xhci_hcs[i]);
        }
    }
}

static void xhci_irq0(struct trap_frame* f){ xhci_irq_slot(0); }
static void xhci_irq1(struct trap_frame* f){ xhci_irq_slot(1); }
static INTERRUPT_CALLBACK_FUNCTION const xhci_irq_handler[XHCI_IRQ_SLOTS] = { xhci_irq0, xhci_irq1 };

// The ports without one (the Pi and VisionFive 2 ports until they wire MSI): no message
// interrupts, so INTx or polling.
__attribute__((weak)) int arch_irq_msi(struct pci_device* dev, unsigned slot, struct cpu* c,
                                       INTERRUPT_CALLBACK_FUNCTION handler, uint64_t* addr, uint32_t* data){
    return -EUNIMP;
}

static uint8_t pci_find_cap(struct pci_device* d, uint8_t id){
    uint8_t b = d->addr.bus, s = d->addr.slot, f = d->addr.func;
    if(!(pci_cfg_read_word(b, s, f, PCI_HEADER_STATUS_OFFSET) & 0x10)){
        return 0;
    }
    uint8_t ptr = pci_cfg_read_byte(b, s, f, 0x34) & 0xFC;
    for(int guard = 0; ptr && guard < 48; guard++){
        if(pci_cfg_read_byte(b, s, f, ptr) == id){
            return ptr;
        }
        ptr = pci_cfg_read_byte(b, s, f, ptr + 1) & 0xFC;
    }
    return 0;
}

// Point the message at CPU `c` (MSI-X: masked while it changes).
// The arch's line number, or < 0.
static int xhci_msg_target(struct xhci* hc, struct cpu* c, INTERRUPT_CALLBACK_FUNCTION handler){
    uint64_t addr;
    uint32_t data;
    int line = arch_irq_msi(hc->pci, XHCI_IRQ_SLOT_BASE + hc->irq_slot, c, handler, &addr, &data);
    if(line < 0){
        return -EIO;
    }
    struct pci_address* a = &hc->pci->addr;
    if(hc->irq_kind == XHCI_IRQ_MSIX){
        mmio_write32(hc->msix_entry + 12, 1);
        mmio_write32(hc->msix_entry + 0, (uint32_t)addr);
        mmio_write32(hc->msix_entry + 4, (uint32_t)(addr >> 32));
        mmio_write32(hc->msix_entry + 8, data);
        mmio_write32(hc->msix_entry + 12, 0);
    } else {
        uint16_t ctrl = pci_cfg_read_word(a->bus, a->slot, a->func, hc->msi_cap + 2);
        pci_cfg_write_dword(a->bus, a->slot, a->func, hc->msi_cap + 4, (uint32_t)addr);
        if(ctrl & 0x80){
            pci_cfg_write_dword(a->bus, a->slot, a->func, hc->msi_cap + 8, (uint32_t)(addr >> 32));
            pci_cfg_write_word(a->bus, a->slot, a->func, hc->msi_cap + 12, (uint16_t)data);
        } else {
            pci_cfg_write_word(a->bus, a->slot, a->func, hc->msi_cap + 8, (uint16_t)data);
        }
    }
    hc->irq.dest = c;
    return line;
}

static int xhci_setup_msi(struct xhci* hc, INTERRUPT_CALLBACK_FUNCTION handler){
    struct pci_device* d = hc->pci;
    uint8_t b = d->addr.bus, s = d->addr.slot, f = d->addr.func;
    uint8_t cap = pci_find_cap(d, 0x11);
    if(cap){
        uint32_t table = pci_cfg_read_dword(b, s, f, cap + 4);
        uint8_t bir = table & 7;
        if(bir < 6 && d->bars[bir].type == PCI_DEVICE_IO_MEMORY && d->bars[bir].addr){
            uint64_t base = (d->bars[bir].addr & ~0xFULL) + (table & ~7u);
            hc->msix_entry = paging_map_mmio(base, 16, PAGING_CACHE_DISABLED);
            hc->irq_kind = XHCI_IRQ_MSIX;
            int line = hc->msix_entry ? xhci_msg_target(hc, usb_bsp, handler) : -EIO;
            if(line >= 0){
                uint16_t ctrl = pci_cfg_read_word(b, s, f, cap + 2);
                pci_cfg_write_word(b, s, f, cap + 2, (ctrl & ~0x4000) | 0x8000);
                return line;
            }
        }
    }
    cap = pci_find_cap(d, 0x05);
    if(cap){
        hc->msi_cap  = cap;
        hc->irq_kind = XHCI_IRQ_MSI;
        int line = xhci_msg_target(hc, usb_bsp, handler);
        if(line >= 0){
            uint16_t ctrl = pci_cfg_read_word(b, s, f, cap + 2);
            pci_cfg_write_word(b, s, f, cap + 2, (ctrl & ~0x70) | 1);
            return line;
        }
    }
    hc->irq_kind = XHCI_IRQ_NONE;
    return -ENOENT;
}

static void xhci_setup_irq(struct xhci* hc){
    struct disk_irq* q = &hc->irq;
    q->name   = "xhci";
    q->marker = BM_FEATURE_DISK_IRQ_USB;
    if(boot_cmdline_has("samos.usb_poll") || xhci_hc_count > XHCI_IRQ_SLOTS || !arch_irq_routable()){
        return;
    }
    hc->irq_slot = xhci_hc_count - 1;
    INTERRUPT_CALLBACK_FUNCTION handler = xhci_irq_handler[hc->irq_slot];
    // A platform (mmio) controller has no MSI capability to read.
    int msi = !hc->pci || boot_cmdline_has("samos.usb_intx") ? -ENOENT : xhci_setup_msi(hc, handler);
    if(msi >= 0){
        q->enabled = true;
        serial_write(hc->irq_kind == XHCI_IRQ_MSIX ? "xhci: interrupts by MSI-X" : "xhci: interrupts by MSI");
        serial_write(", vector ");
        serial_write(itoa(msi));
        serial_write("\n");
        return;
    }
    uint16_t inti = hc->mmio_inti;
    int gsi = hc->pci ? pci_intx_gsi(hc->pci, &inti) : hc->mmio_gsi ? (int)hc->mmio_gsi : -1;
    if(gsi < 0){
        return;
    }
    if(hc->pci){
        pci_intx_enable(hc->pci);
    }
    int line = arch_irq_attach_gsi((uint32_t)gsi, inti, XHCI_IRQ_SLOT_BASE + hc->irq_slot, false, handler);
    if(line < 0){
        return;
    }
    hc->irq_kind = XHCI_IRQ_INTX;
    q->enabled = true;
    disk_irq_report(q, (uint32_t)gsi, inti, line, false);
}

void usb_irq_arm(struct usb_device* dev){
    struct xhci* hc = dev->hc;
    if(hc->irq_kind == XHCI_IRQ_INTX){
        disk_irq_arm(&hc->irq);
        return;
    }
    hc->irq.pending = 0;
    if((hc->irq_kind == XHCI_IRQ_MSIX || hc->irq_kind == XHCI_IRQ_MSI) && hc->irq.enabled &&
       smp_active() && hc->irq.dest != this_cpu()){
        xhci_msg_target(hc, this_cpu(), NULL);
    }
}

// ---- commands -------------------------------------------------------------

// One command; its completion code, or -ETIMEOUT. *slot_out: the slot
// id the completion names (Enable Slot).
static int xhci_command(struct xhci* hc, uint64_t param, uint32_t control, uint8_t* slot_out){
    spin_lock(&hc->cmd_lock);
    spin_lock(&hc->lock);
    hc->cmd_done = false;
    hc->cmd_trb = ring_push(&hc->cmd, param, 0, control);
    spin_unlock(&hc->lock);
    doorbell(hc, 0, 0);
    int res = -ETIMEOUT;
    for(uint64_t end = xhci_deadline(XHCI_CMD_TIMEOUT_MS); !xhci_expired(end); ){
        xhci_poll(hc);
        if(hc->cmd_done){
            smp_rmb();
            res = hc->cmd_result & 0xFF;
            if(slot_out){
                *slot_out = hc->cmd_result >> 24;
            }
            break;
        }
        cpu_relax();
    }
    spin_unlock(&hc->cmd_lock);
    if(res != XHCI_CC_SUCCESS){
        serial_write("xhci: command ");
        serial_write(itoa(TRB_GET_TYPE(control)));
        serial_write(res < 0 ? " timed out\n" : " failed, code ");
        if(res >= 0){
            serial_write(itoa(res));
            serial_write("\n");
        }
    }
    return res;
}

// ---- transfers ------------------------------------------------------------

static struct usb_endpoint* ep_new(struct usb_device* d, uint8_t dci){
    struct usb_endpoint* ep = kzalloc(sizeof(*ep));
    void* page = hc_dma_page(d->hc);
    if(!ep || !page){
        kfree(ep);
        kfree(page);
        return NULL;
    }
    ep->dev = d;
    ep->dci = dci;
    ring_init(&ep->ring, page);
    return ep;
}

static void ep_begin(struct usb_endpoint* ep){
    ep->done = false;
    ep->cc = 0;
    ep->actual = 0;
    ep->td_count = 0;
    ep->busy = true;
}

static void ep_push(struct usb_endpoint* ep, uint64_t param, uint32_t len, uint32_t control){
    ep->td_trb[ep->td_count] = ring_push(&ep->ring, param, len, control);
    ep->td_len[ep->td_count] = len & 0x1FFFF;
    ep->td_count++;
}

bool usb_transfer_done(struct usb_endpoint* ep){
    if(!ep->done){
        xhci_poll(ep->dev->hc);
    }
    // An IN transfer's pages are the controller's until here.
    if(ep->done && ep->in_bytes){
        smp_rmb();
        for(uint32_t done = 0; done < ep->in_bytes; done += 4096){
            arch_dma_inval(ep->in_pages[done / 4096], ep->in_bytes - done > 4096 ? 4096 : ep->in_bytes - done);
        }
        ep->in_bytes = 0;
    }
    return ep->done;
}

static bool ep_done_cb(void* ctx){
    return usb_transfer_done(ctx);
}

int usb_transfer_wait(struct usb_endpoint* ep, bool irq, uint32_t ms, bool* stalled){
    struct xhci* hc = ep->dev->hc;
    if(stalled){
        *stalled = false;
    }
    if(irq){
        disk_irq_wait(&hc->irq, ep_done_cb, ep);
    }
    for(uint64_t end = xhci_deadline(ms); !usb_transfer_done(ep); ){
        if(xhci_expired(end)){
            return -ETIMEOUT;       // the TD stays on the ring: busy for good
        }
        cpu_relax();
    }
    smp_rmb();
    ep->busy = false;
    if(ep->cc == XHCI_CC_SUCCESS || ep->cc == XHCI_CC_SHORT_PACKET){
        return (int)ep->actual;
    }
    if(ep->cc == XHCI_CC_STALL && stalled){
        *stalled = true;
    }
    return -EIO;
}

int usb_transfer_start(struct usb_endpoint* ep, void** pages, uint32_t len){
    struct usb_device* d = ep->dev;
    if(d->gone || ep->busy || len > XHCI_TD_MAX_PAGES * 4096u){
        return -EIO;
    }
    struct xhci* hc = d->hc;
    // OUT data to memory; IN pages flushed now, dropped when the
    // transfer is done (usb_transfer_done).
    bool in = ep->addr & USB_EP_IN;
    for(uint32_t done = 0; done < len; done += 4096){
        uint32_t chunk = len - done > 4096 ? 4096 : len - done;
        if(in){
            arch_dma_flush(pages[done / 4096], chunk);
            ep->in_pages[done / 4096] = pages[done / 4096];
        } else {
            arch_dma_clean(pages[done / 4096], chunk);
        }
    }
    spin_lock(&hc->lock);
    ep_begin(ep);
    ep->in_bytes = in ? len : 0;
    uint32_t done = 0;
    do {
        uint32_t chunk = len - done > 4096 ? 4096 : len - done;
        bool last = done + chunk >= len;
        // TD Size: packets still to come after this TRB (spec 4.11.2.4).
        uint32_t left = (len - done - chunk + ep->mps - 1) / (ep->mps ? ep->mps : 1);
        // ISP: a short packet ends the TD with an event; IOC on the last.
        ep_push(ep, dma_phys(pages[done / 4096]), chunk | (left > 31 ? 31 : left) << 17,
                TRB_TYPE(TRB_NORMAL) | TRB_ISP | (last ? TRB_IOC : TRB_CH));
        done += chunk;
    } while(done < len);
    spin_unlock(&hc->lock);
    doorbell(hc, d->slot, ep->dci);
    return 0;
}

// Reset Endpoint (Stop Endpoint when it is not halted: a timed-out
// transfer) and Set TR Dequeue Pointer: the endpoint runs again from
// the enqueue pointer, the stalled TD dropped.
static int ep_reset(struct usb_endpoint* ep){
    struct usb_device* d = ep->dev;
    struct xhci* hc = d->hc;
    uint32_t sel = (uint32_t)d->slot << 24 | (uint32_t)ep->dci << 16;
    if(xhci_command(hc, 0, TRB_TYPE(TRB_RESET_ENDPOINT) | sel, NULL) != XHCI_CC_SUCCESS){
        xhci_command(hc, 0, TRB_TYPE(TRB_STOP_ENDPOINT) | sel, NULL);
    }
    spin_lock(&hc->lock);
    uint64_t deq = (ep->ring.phys + ep->ring.enq * sizeof(struct xhci_trb)) | ep->ring.cycle;
    spin_unlock(&hc->lock);
    ep->busy = false;
    return xhci_command(hc, deq, TRB_TYPE(TRB_SET_TR_DEQUEUE) | sel, NULL) == XHCI_CC_SUCCESS ? 0 : -EIO;
}

int usb_control(struct usb_device* d, uint8_t type, uint8_t req, uint16_t value,
                uint16_t index, void* data, uint16_t len){
    struct usb_endpoint* ep = d->ep[1];
    if(d->gone || !ep || len > 4096){
        return -EIO;
    }
    struct xhci* hc = d->hc;
    bool in = type & USB_RT_IN;
    if(!in && len){
        memcpy(d->buf_page, data, len);
        arch_dma_clean(d->buf_page, len);
    } else if(len){
        arch_dma_flush(d->buf_page, len);
    }
    uint64_t setup = type | (uint32_t)req << 8 | (uint32_t)value << 16 |
                     (uint64_t)index << 32 | (uint64_t)len << 48;
    spin_lock(&hc->lock);
    ep_begin(ep);
    ep_push(ep, setup, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT |
            (len ? (in ? TRB_TRT_IN : TRB_TRT_OUT) : 0));
    if(len){
        ep_push(ep, dma_phys(d->buf_page), len, TRB_TYPE(TRB_DATA) | TRB_ISP | (in ? TRB_DIR_IN : 0));
    }
    ep_push(ep, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | (len && in ? 0 : TRB_DIR_IN));
    ep->actual = len;
    spin_unlock(&hc->lock);
    doorbell(hc, d->slot, 1);
    bool stalled = false;
    int res = usb_transfer_wait(ep, false, XHCI_CTRL_TIMEOUT_MS, &stalled);
    if(stalled){
        ep_reset(ep);
    }
    if(res > 0 && in){
        arch_dma_inval(d->buf_page, res);
        memcpy(data, d->buf_page, res);
    }
    return res;
}

int usb_endpoint_clear_halt(struct usb_endpoint* ep){
    int res = ep_reset(ep);
    if(res < 0){
        return res;
    }
    return usb_control(ep->dev, USB_RT_ENDPOINT, USB_REQ_CLEAR_FEATURE, 0, ep->addr, NULL, 0) < 0 ? -EIO : 0;
}

uint8_t usb_endpoint_address(struct usb_endpoint* ep){
    return ep->addr;
}

// ---- endpoints --------------------------------------------------------------

// Endpoint context Interval: 2^n x 125 us. Full and low speed give
// bInterval in frames (ms), high and super speed as 2^(bInterval-1).
static uint8_t ep_interval(struct usb_device* d, uint8_t b_interval){
    if(d->speed == USB_SPEED_HIGH || d->speed >= USB_SPEED_SUPER){
        return b_interval ? b_interval - 1 : 0;
    }
    uint8_t n = 3;
    while(n < 10 && (1u << (n + 1)) <= (uint32_t)b_interval * 8){
        n++;
    }
    return n;
}

struct usb_endpoint* usb_endpoint_open(struct usb_device* d, const struct usb_endpoint_descriptor* ed){
    bool in = ed->bEndpointAddress & USB_EP_IN;
    uint8_t xfer = ed->bmAttributes & 3;
    uint8_t dci = (ed->bEndpointAddress & 0xF) * 2 + (in ? 1 : 0);
    if(dci < 2 || (xfer != USB_EP_BULK && xfer != USB_EP_INTERRUPT)){
        return NULL;
    }
    struct usb_endpoint* ep = ep_new(d, dci);
    if(!ep){
        return NULL;
    }
    ep->addr = ed->bEndpointAddress;
    ep->mps  = ed->wMaxPacketSize & 0x7FF;
    uint8_t burst = 0;
    const uint8_t* comp = usb_config_next(d, (const uint8_t*)ed, USB_DT_SS_COMPANION, true);
    if(d->speed >= USB_SPEED_SUPER && comp && comp == (const uint8_t*)ed + ed->bLength){
        burst = comp[2];
    }
    uint8_t type = xfer == USB_EP_BULK ? (in ? XHCI_EP_BULK_IN : XHCI_EP_BULK_OUT)
                                      : (in ? XHCI_EP_INT_IN : XHCI_EP_INT_OUT);
    memset(d->in_page, 0, 4096);
    uint32_t* icc = d->in_page;
    icc[1] = 1u | (1u << dci);                      // add the slot and this endpoint
    if(dci > d->max_dci){
        d->max_dci = dci;
    }
    arch_dma_inval(dev_ctx(d, 0), d->hc->ctx_size);
    memcpy(in_ctx(d, 0), dev_ctx(d, 0), d->hc->ctx_size);
    uint32_t* slot = (uint32_t*)in_ctx(d, 0);
    slot[0] = (slot[0] & ~(0x1Fu << 27)) | (uint32_t)d->max_dci << 27;
    uint32_t* e = (uint32_t*)in_ctx(d, dci);
    uint32_t esit = xfer == USB_EP_INTERRUPT ? ep->mps * (burst + 1u) : 0;
    e[0] = (xfer == USB_EP_INTERRUPT ? (uint32_t)ep_interval(d, ed->bInterval) << 16 : 0);
    e[1] = 3u << 1 | (uint32_t)type << 3 | (uint32_t)burst << 8 | (uint32_t)ep->mps << 16;
    e[2] = (uint32_t)ep->ring.phys | 1;
    e[3] = (uint32_t)(ep->ring.phys >> 32);
    e[4] = (xfer == USB_EP_INTERRUPT ? ep->mps : 512u) | esit << 16;
    d->ep[dci] = ep;
    arch_dma_clean(d->in_page, 4096);
    if(xhci_command(d->hc, dma_phys(d->in_page),
                    TRB_TYPE(TRB_CONFIGURE_ENDPOINT) | (uint32_t)d->slot << 24, NULL) != XHCI_CC_SUCCESS){
        d->ep[dci] = NULL;
        return NULL;                // ep and its ring leak: rare, small
    }
    return ep;
}

// ---- descriptors --------------------------------------------------------------

const uint8_t* usb_config_next(struct usb_device* d, const uint8_t* from, uint8_t type, bool in_iface){
    const uint8_t* end = d->config + d->config_len;
    const uint8_t* p = from ? from + from[0] : d->config;
    while(p + 2 <= end && p[0] >= 2 && p + p[0] <= end){
        if(p[1] == type){
            return p;
        }
        if(in_iface && p[1] == USB_DT_INTERFACE){
            return NULL;
        }
        p += p[0];
    }
    return NULL;
}

static void usb_read_product(struct usb_device* d){
    uint8_t buf[64];
    if(!d->desc.iProduct || usb_control(d, USB_RT_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_STRING << 8, 0, buf, 4) < 4){
        return;
    }
    uint16_t lang = buf[2] | buf[3] << 8;
    int n = usb_control(d, USB_RT_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_STRING << 8 | d->desc.iProduct,
                        lang, buf, sizeof(buf));
    int o = 0;
    for(int i = 2; i + 1 < n && o < (int)sizeof(d->product) - 1; i += 2){
        d->product[o++] = buf[i + 1] || buf[i] < 0x20 || buf[i] > 0x7E ? '?' : (char)buf[i];
    }
    d->product[o] = 0;
}

// ---- devices ----------------------------------------------------------------------

static const char* speed_name(uint8_t s){
    switch(s){
        case USB_SPEED_LOW:   return "low";
        case USB_SPEED_FULL:  return "full";
        case USB_SPEED_HIGH:  return "high";
        case USB_SPEED_SUPER: return "super";
        default:              return "super+";
    }
}

static void write_hex4(uint16_t v){
    char s[5];
    for(int i = 0; i < 4; i++){
        s[i] = "0123456789abcdef"[(v >> (12 - 4 * i)) & 0xF];
    }
    s[4] = 0;
    serial_write(s);
}

static int usb_address(struct xhci* hc, struct usb_device* d){
    uint8_t slot = 0;
    if(xhci_command(hc, 0, TRB_TYPE(TRB_ENABLE_SLOT), &slot) != XHCI_CC_SUCCESS || !slot ||
       slot > hc->max_slots){
        return -EIO;
    }
    d->slot = slot;
    d->ctx_page = hc_dma_page(hc);
    d->in_page  = hc_dma_page(hc);
    d->buf_page = hc_dma_page(hc);
    struct usb_endpoint* ep0 = ep_new(d, 1);
    if(!d->ctx_page || !d->in_page || !d->buf_page || !ep0){
        return -ENOMEM;
    }
    ep0->control = true;
    d->ep[1] = ep0;
    d->max_dci = 1;
    hc->dcbaa[slot] = dma_phys(d->ctx_page);
    arch_dma_clean(&hc->dcbaa[slot], 8);
    hc->slot_dev[slot] = d;
    // Packet size before the descriptor says: 8 at low and full speed
    // (full speed corrected below), 64 at high, 512 at super speed.
    d->mps0 = d->speed == USB_SPEED_HIGH ? 64 : d->speed >= USB_SPEED_SUPER ? 512 : 8;

    uint32_t* icc = d->in_page;
    icc[1] = 3;                                     // A0 slot, A1 EP0
    uint32_t* s = (uint32_t*)in_ctx(d, 0);
    s[0] = 1u << 27 | (uint32_t)d->speed << 20;
    s[1] = (uint32_t)d->port << 16;
    uint32_t* e = (uint32_t*)in_ctx(d, 1);
    e[1] = 3u << 1 | (uint32_t)XHCI_EP_CONTROL << 3 | (uint32_t)d->mps0 << 16;
    e[2] = (uint32_t)ep0->ring.phys | 1;
    e[3] = (uint32_t)(ep0->ring.phys >> 32);
    e[4] = 8;
    arch_dma_clean(d->in_page, 4096);
    if(xhci_command(hc, dma_phys(d->in_page), TRB_TYPE(TRB_ADDRESS_DEVICE) | (uint32_t)slot << 24, NULL)
       != XHCI_CC_SUCCESS){
        return -EIO;
    }
    if(usb_control(d, USB_RT_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, &d->desc, 8) < 8){
        return -EIO;
    }
    uint16_t mps = d->speed >= USB_SPEED_SUPER ? 1u << d->desc.bMaxPacketSize0 : d->desc.bMaxPacketSize0;
    if(mps && mps != d->mps0){
        d->mps0 = mps;
        icc[0] = 0;
        icc[1] = 2;                                 // Evaluate Context: EP0
        e[1] = (e[1] & 0xFFFF) | (uint32_t)mps << 16;
        arch_dma_clean(d->in_page, 4096);
        if(xhci_command(hc, dma_phys(d->in_page), TRB_TYPE(TRB_EVALUATE_CONTEXT) | (uint32_t)slot << 24, NULL)
           != XHCI_CC_SUCCESS){
            return -EIO;
        }
    }
    if(usb_control(d, USB_RT_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, &d->desc, 18) < 18){
        return -EIO;
    }
    uint8_t head[9];
    if(usb_control(d, USB_RT_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_CONFIG << 8, 0, head, 9) < 9){
        return -EIO;
    }
    uint16_t total = head[2] | head[3] << 8;
    if(total < 9 || total > 4096 || !(d->config = kzalloc(total))){
        return -EIO;
    }
    int n = usb_control(d, USB_RT_IN, USB_REQ_GET_DESCRIPTOR, USB_DT_CONFIG << 8, 0, d->config, total);
    if(n < 9){
        return -EIO;
    }
    d->config_len = n;
    if(usb_control(d, 0, USB_REQ_SET_CONFIGURATION, head[5], 0, NULL, 0) < 0){
        return -EIO;
    }
    usb_read_product(d);
    return 0;
}

static void usb_bind(struct usb_device* d){
    for(const uint8_t* p = usb_config_next(d, NULL, USB_DT_INTERFACE, false); p;
        p = usb_config_next(d, p, USB_DT_INTERFACE, false)){
        const struct usb_interface_descriptor* intf = (const void*)p;
        if(intf->bAlternateSetting){
            continue;
        }
        if(usb_hid_probe(d, intf) == 0 || usb_msc_probe(d, intf) == 0){
            return;
        }
    }
    serial_write("usb: no driver for slot ");
    serial_write(itoa(d->slot));
    serial_write("\n");
}

// Free the slot of a device that failed or went away. The struct stays:
// a class driver may still hold it (gone is set).
static void usb_release(struct xhci* hc, struct usb_device* d){
    if(d->slot){
        xhci_command(hc, 0, TRB_TYPE(TRB_DISABLE_SLOT) | (uint32_t)d->slot << 24, NULL);
        hc->dcbaa[d->slot] = 0;
        arch_dma_clean(&hc->dcbaa[d->slot], 8);
        hc->slot_dev[d->slot] = NULL;
    }
    hc->port_dev[d->port] = NULL;
}

// Reset the port if it is not enabled yet; its speed, or < 0.
static int xhci_port_reset(struct xhci* hc, uint8_t port){
    uint32_t v = portsc_read(hc, port);
    if(!(v & PORTSC_PED)){
        op_write(hc, XHCI_OP_PORTSC(port), portsc_neutral(v) | PORTSC_PR);
        uint64_t end = xhci_deadline(500);
        while(!(portsc_read(hc, port) & PORTSC_PRC)){
            if(xhci_expired(end)){
                return -ETIMEOUT;
            }
            cpu_relax();
        }
        udelay(10000);                              // TRSTRCY, USB 2.0 7.1.7.5
    }
    v = portsc_read(hc, port);
    op_write(hc, XHCI_OP_PORTSC(port), portsc_neutral(v) | (v & PORTSC_CHANGE));
    if(!(v & PORTSC_PED) || !(v & PORTSC_CCS)){
        return -EIO;
    }
    return (v >> 10) & 0xF;
}

static void xhci_port_attach(struct xhci* hc, uint8_t port){
    int speed = xhci_port_reset(hc, port);
    if(speed <= 0){
        serial_write("usb: port ");
        serial_write(itoa(port));
        serial_write(" did not enable\n");
        return;
    }
    struct usb_device* d = kzalloc(sizeof(*d));
    if(!d){
        return;
    }
    d->hc = hc;
    d->port = port;
    d->speed = speed;
    hc->port_dev[port] = d;
    int res = usb_address(hc, d);
    serial_write("usb: port ");
    serial_write(itoa(port));
    serial_write(hc->port_major[port] == 3 ? " (USB 3) " : " (USB 2) ");
    serial_write(speed_name(d->speed));
    serial_write(" speed");
    if(res < 0){
        serial_write(", enumeration failed\n");
        d->gone = true;
        usb_release(hc, d);
        return;
    }
    serial_write(", slot ");
    serial_write(itoa(d->slot));
    serial_write(", ");
    write_hex4(d->desc.idVendor);
    serial_write(":");
    write_hex4(d->desc.idProduct);
    serial_write(" ");
    serial_write(d->product);
    serial_write("\n");
    usb_bind(d);
}

static void xhci_port_detach(struct xhci* hc, struct usb_device* d){
    d->gone = true;
    spin_lock(&hc->lock);
    for(int i = 1; i < 32; i++){
        struct usb_endpoint* ep = d->ep[i];
        if(ep && ep->busy && !ep->done){
            ep->cc = XHCI_CC_GONE;
            ep->done = true;
        }
    }
    spin_unlock(&hc->lock);
    serial_write("usb: port ");
    serial_write(itoa(d->port));
    serial_write(" disconnected, slot ");
    serial_write(itoa(d->slot));
    serial_write("\n");
    usb_hid_disconnect(d);
    usb_msc_disconnect(d);
    usb_release(hc, d);
}

// Connect and disconnect changes (on the BSP, or at init).
static void xhci_ports_scan(struct xhci* hc, bool all){
    for(uint8_t port = 1; port <= hc->max_ports; port++){
        uint32_t v = portsc_read(hc, port);
        if(!all && !(v & PORTSC_CSC)){
            continue;
        }
        op_write(hc, XHCI_OP_PORTSC(port), portsc_neutral(v) | PORTSC_CSC);
        struct usb_device* d = hc->port_dev[port];
        if(d && (!(v & PORTSC_CCS) || (v & PORTSC_CSC))){
            xhci_port_detach(hc, d);
            d = NULL;
        }
        if(!d && (v & PORTSC_CCS)){
            xhci_port_attach(hc, port);
        }
    }
}

// ---- controller ------------------------------------------------------------------

static void xhci_caps(struct xhci* hc){
    uint32_t off = (mmio_read32(hc->cap + XHCI_CAP_HCCPARAMS1) >> 16) << 2;
    for(int guard = 0; off && guard < 64; guard++){
        volatile uint8_t* c = hc->cap + off;
        uint32_t v = mmio_read32(c);
        if((v & 0xFF) == XHCI_XCAP_LEGACY){
            // Take the controller from the firmware's SMM driver.
            mmio_write32(c, v | XHCI_LEGACY_OS_OWNED);
            uint64_t end = xhci_deadline(1000);
            while((mmio_read32(c) & XHCI_LEGACY_BIOS_OWNED) && !xhci_expired(end)){
                cpu_relax();
            }
            if(mmio_read32(c) & XHCI_LEGACY_BIOS_OWNED){
                serial_write("xhci: firmware did not release the controller\n");
            }
            uint32_t ctl = mmio_read32(c + 4);
            mmio_write32(c + 4, (ctl & XHCI_LEGACY_SMI_KEEP) | XHCI_LEGACY_SMI_EVENTS);
        } else if((v & 0xFF) == XHCI_XCAP_PROTOCOL){
            uint8_t major = v >> 24;
            uint32_t ports = mmio_read32(c + 8);
            uint8_t first = ports & 0xFF, count = (ports >> 8) & 0xFF;
            for(int p = first; p < first + count && p <= hc->max_ports; p++){
                hc->port_major[p] = major;
            }
        }
        uint32_t next = (v >> 8) & 0xFF;
        off = next ? off + (next << 2) : 0;
    }
}

static void write_port_ranges(struct xhci* hc, uint8_t major){
    int first = 0;
    for(int p = 1; p <= hc->max_ports + 1; p++){
        bool is = p <= hc->max_ports && hc->port_major[p] == major;
        if(is && !first){
            first = p;
        } else if(!is && first){
            serial_write(major == 3 ? ", USB 3: " : ", USB 2: ");
            serial_write(itoa(first));
            serial_write("-");
            serial_write(itoa(p - 1));
            first = 0;
        }
    }
}

// Intel 7- to 9-series PCHs share their USB 2 ports with an EHCI and
// hand them to it until the OS switches them (XUSB2PR) and turns the
// USB 3 pins on (USB3_PSSEN), each to the mask the chipset allows.
// The device ids are Linux's (usb_enable_intel_xhci_ports).
static void xhci_intel_port_switch(struct pci_device* dev){
    static const uint16_t ids[] = { 0x1E31, 0x8C31, 0x9C31, 0x8CB1, 0x9CB1 };
    uint8_t b = dev->addr.bus, s = dev->addr.slot, f = dev->addr.func;
    for(unsigned i = 0; dev->vendor == 0x8086 && i < sizeof(ids) / sizeof(ids[0]); i++){
        if(dev->device_id == ids[i]){
            pci_cfg_write_dword(b, s, f, 0xD8, pci_cfg_read_dword(b, s, f, 0xDC));
            pci_cfg_write_dword(b, s, f, 0xD0, pci_cfg_read_dword(b, s, f, 0xD4));
            serial_write("xhci: Intel PCH ports switched from EHCI\n");
        }
    }
}

static int xhci_start(struct xhci* hc){
    struct pci_device* dev = hc->pci;
    uint64_t base = hc->mmio_phys, size = hc->mmio_size ? hc->mmio_size : 0x10000;
    if(dev){
        base = dev->bars[0].addr & ~0xFULL;
        size = dev->bars[0].size ? dev->bars[0].size : 0x10000;
        if(dev->bars[0].type != PCI_DEVICE_IO_MEMORY){
            return -EIO;
        }
    }
    if(!base){
        return -EIO;
    }
    hc->cap = paging_map_mmio(base, size, PAGING_CACHE_DISABLED);
    if(!hc->cap){
        return -EIO;
    }
    if(dev){
        pci_enable_bus_master(dev);
    }
    uint32_t hcs1 = mmio_read32(hc->cap + XHCI_CAP_HCSPARAMS1);
    uint32_t hcs2 = mmio_read32(hc->cap + XHCI_CAP_HCSPARAMS2);
    uint32_t hcc1 = mmio_read32(hc->cap + XHCI_CAP_HCCPARAMS1);
    hc->op = hc->cap + mmio_read8(hc->cap + XHCI_CAP_CAPLENGTH);
    hc->rt = hc->cap + (mmio_read32(hc->cap + XHCI_CAP_RTSOFF) & ~0x1Fu);
    hc->db = hc->cap + (mmio_read32(hc->cap + XHCI_CAP_DBOFF) & ~0x3u);
    hc->max_slots = hcs1 & 0xFF;
    hc->max_ports = hcs1 >> 24;
    hc->ctx_size  = hcc1 & XHCI_HCC_CSZ ? 64 : 32;
    uint32_t scratch = ((hcs2 >> 21) & 0x1F) << 5 | hcs2 >> 27;
    xhci_caps(hc);

    // Stop, then reset (spec 4.22.1).
    op_write(hc, XHCI_OP_USBCMD, op_read(hc, XHCI_OP_USBCMD) & ~USBCMD_RS);
    uint64_t end = xhci_deadline(100);
    while(!(op_read(hc, XHCI_OP_USBSTS) & USBSTS_HCH) && !xhci_expired(end)){
        cpu_relax();
    }
    op_write(hc, XHCI_OP_USBCMD, USBCMD_HCRST);
    end = xhci_deadline(1000);
    while((op_read(hc, XHCI_OP_USBCMD) & USBCMD_HCRST) || (op_read(hc, XHCI_OP_USBSTS) & USBSTS_CNR)){
        if(xhci_expired(end)){
            serial_write("xhci: reset timed out\n");
            return -ETIMEOUT;
        }
        cpu_relax();
    }
    if(!(op_read(hc, XHCI_OP_PAGESIZE) & 1)){
        serial_write("xhci: no 4 KiB page size\n");
        return -EIO;
    }

    // Without AC64 every structure below 4 GiB (hc_dma_page).
    // samos.usb_dma32[=MiB] (tests; QEMU's controllers all have AC64)
    // treats it as 32-bit, with a lower limit when given.
    hc->dma_limit = hcc1 & XHCI_HCC_AC64 ? 0 : 1ull << 32;
    const char* limit = boot_cmdline_value("samos.usb_dma32");
    if(limit){
        uint64_t mib = 0;
        for(; isdigit(*limit); limit++){
            mib = mib * 10 + tonumericdigit(*limit);
        }
        hc->dma_limit = mib << 20;
    } else if(boot_cmdline_has("samos.usb_dma32")){
        hc->dma_limit = 1ull << 32;
    }
    if(hc->dma_limit){
        serial_write("xhci: 32-bit addressing, DMA below ");
        serial_write(itoa((int)(hc->dma_limit >> 20)));
        serial_write(" MiB\n");
    }
    void* dcbaa = hc_dma_page(hc);
    void* cmd = hc_dma_page(hc);
    void* ev = hc_dma_page(hc);
    void* erst = hc_dma_page(hc);
    if(!dcbaa || !cmd || !ev || !erst){
        return -ENOMEM;
    }
    hc->dcbaa = dcbaa;
    if(scratch){
        uint64_t* array = hc_dma_page(hc);
        if(!array || scratch > 512){
            return -ENOMEM;
        }
        for(uint32_t i = 0; i < scratch; i++){
            void* p = hc_dma_page(hc);
            if(!p){
                return -ENOMEM;
            }
            array[i] = dma_phys(p);
        }
        hc->dcbaa[0] = dma_phys(array);
        arch_dma_clean(array, 4096);
        arch_dma_clean(hc->dcbaa, 8);
    }
    op_write(hc, XHCI_OP_CONFIG, hc->max_slots);
    write64(hc->op + XHCI_OP_DCBAAP, dma_phys(dcbaa));
    ring_init(&hc->cmd, cmd);
    write64(hc->op + XHCI_OP_CRCR, hc->cmd.phys | 1);

    hc->ev = ev;
    hc->ev_phys = dma_phys(ev);
    hc->ev_cycle = 1;
    uint64_t* seg = erst;
    seg[0] = hc->ev_phys;
    seg[1] = XHCI_RING_TRBS;
    arch_dma_clean(seg, 16);
    mmio_write32(hc->rt + XHCI_IR0_ERSTSZ, 1);
    write64(hc->rt + XHCI_IR0_ERDP, hc->ev_phys);
    write64(hc->rt + XHCI_IR0_ERSTBA, dma_phys(erst));

    xhci_setup_irq(hc);
    mmio_write32(hc->rt + XHCI_IR0_IMOD, XHCI_IMOD_250US);
    mmio_write32(hc->rt + XHCI_IR0_IMAN, XHCI_IMAN_IE | XHCI_IMAN_IP);
    op_write(hc, XHCI_OP_USBCMD, USBCMD_RS | (hc->irq.enabled ? USBCMD_INTE : 0));
    end = xhci_deadline(100);
    while(op_read(hc, XHCI_OP_USBSTS) & USBSTS_HCH){
        if(xhci_expired(end)){
            serial_write("xhci: did not start\n");
            return -EIO;
        }
        cpu_relax();
    }

    serial_write("xhci: ");
    if(dev){
        serial_write(itoa(dev->addr.bus));
        serial_write(":");
        serial_write(itoa(dev->addr.slot));
        serial_write(".");
        serial_write(itoa(dev->addr.func));
        serial_write(" ");
        write_hex4(dev->vendor);
        serial_write(":");
        write_hex4(dev->device_id);
    } else {
        serial_write("mmio 0x");
        for(int i = 12; i >= 0; i -= 4){
            write_hex4((uint16_t)(base >> (4 * i)));
        }
    }
    serial_write(", ");
    serial_write(itoa(hc->max_slots));
    serial_write(" slots, ");
    serial_write(itoa(hc->max_ports));
    serial_write(" ports");
    write_port_ranges(hc, 3);
    write_port_ranges(hc, 2);
    serial_write(", ");
    serial_write(itoa(hc->ctx_size));
    serial_write("-byte contexts, ");
    serial_write(itoa(scratch));
    serial_write(" scratchpads, running");
    serial_write(hc->irq.enabled ? "\n" : ", polled\n");

    if(dev){
        xhci_intel_port_switch(dev);
    }
    // Port power, where the controller leaves it to software (PPC).
    bool powered = false;
    for(uint8_t p = 1; p <= hc->max_ports; p++){
        uint32_t v = portsc_read(hc, p);
        if(!(v & PORTSC_PP)){
            op_write(hc, XHCI_OP_PORTSC(p), portsc_neutral(v) | PORTSC_PP);
            powered = true;
        }
    }
    if(powered){
        udelay(20000);
    }
    return 0;
}

// The BSP's half of every controller: plug events, then HID reports.
static void usb_service(void){
    for(int i = 0; i < xhci_hc_count; i++){
        struct xhci* hc = xhci_hcs[i];
        hc->input_pending = false;
        xhci_poll(hc);
        hc->input_pending = false;
        if(hc->port_change){
            hc->port_change = false;
            xhci_ports_scan(hc, false);
        }
    }
    usb_hid_service();
}

void usb_input_kick(struct xhci* hc){
    if(hc->irq.enabled && usb_bsp){
        arch_send_ipi(usb_bsp, ARCH_IPI_INPUT);
    }
}

// Polled controllers: their input work runs on the BSP's tick.
static void usb_tick(void){
    for(int i = 0; i < xhci_hc_count; i++){
        if(!xhci_hcs[i]->irq.enabled){
            usb_service();
            return;
        }
    }
}

// Platform controllers (usb.h), started after the PCI ones.
static struct xhci xhci_mmio[XHCI_MAX_HCS];
static int xhci_mmio_count;

void usb_xhci_add_mmio(uint64_t phys, uint64_t size, uint32_t gsi, uint16_t inti){
    if(xhci_mmio_count < XHCI_MAX_HCS){
        struct xhci* m = &xhci_mmio[xhci_mmio_count++];
        m->mmio_phys = phys;
        m->mmio_size = size;
        m->mmio_gsi  = gsi;
        m->mmio_inti = inti;
    }
}

// `pci` or the platform controller `m`.
static void xhci_add(struct pci_device* pci, const struct xhci* m){
    struct xhci* hc = kzalloc(sizeof(*hc));
    if(!hc){
        return;
    }
    if(m){
        *hc = *m;
    }
    hc->pci = pci;
    hc->irq_slot = -1;
    spin_lock_init(&hc->lock, "usb", LOCK_LEVEL_USB, false);
    spin_lock_init(&hc->cmd_lock, "usb_cmd", LOCK_LEVEL_USB_CMD, false);
    xhci_hcs[xhci_hc_count++] = hc;
    if(xhci_start(hc) < 0){
        serial_write("xhci: controller not started\n");
        xhci_hc_count--;
        return;                                     // its pages leak: rare
    }
    xhci_ports_scan(hc, true);
}

void usb_init(void){
    static bool started;
    if(started){
        return;
    }
    started = true;
    usb_bsp = this_cpu();
    // Test hook: samos.usb_as_mmio drives each PCI xHCI as a
    // platform one (BAR 0 and its INTx GSI), the path a device-tree
    // xHCI takes, which no QEMU machine of the ports has.
    bool as_mmio = boot_cmdline_has("samos.usb_as_mmio");
    size_t total = pci_device_count();
    for(size_t i = 0; i < total && xhci_hc_count < XHCI_MAX_HCS; i++){
        struct pci_device* dev = NULL;
        if(pci_device_get(i, &dev) < 0 || !dev || dev->class.base != 0x0C || dev->class.subclass != 0x03 ||
           pci_cfg_read_byte(dev->addr.bus, dev->addr.slot, dev->addr.func, PCI_HEADER_PROG_IF_OFFSET) != 0x30){
            continue;
        }
        if(as_mmio){
            uint16_t inti = 0;
            int gsi = pci_intx_gsi(dev, &inti);
            pci_enable_bus_master(dev);
            pci_intx_enable(dev);
            usb_xhci_add_mmio(dev->bars[0].addr & ~0xFULL, dev->bars[0].size, gsi < 0 ? 0 : (uint32_t)gsi, inti);
            continue;
        }
        xhci_add(dev, NULL);
    }
    for(int i = 0; i < xhci_mmio_count && xhci_hc_count < XHCI_MAX_HCS; i++){
        xhci_add(NULL, &xhci_mmio[i]);
    }
    if(xhci_hc_count){
        smp_input_register(usb_service);
        task_clock_register(usb_tick);
    }
}
