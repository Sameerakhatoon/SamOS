#ifndef USB_XHCI_H
#define USB_XHCI_H
// XHCI registers, TRBs and the driver's state (xhci.c only;
// class drivers use usb.h). Numbers are from the xHCI 1.2 spec.

#include <stdint.h>
#include <stdbool.h>
#include "lib/spinlock/spinlock.h"
#include "disk/irqwait.h"

struct pci_device;
struct usb_device;

// Capability registers.
#define XHCI_CAP_CAPLENGTH      0x00
#define XHCI_CAP_HCSPARAMS1     0x04
#define XHCI_CAP_HCSPARAMS2     0x08
#define XHCI_CAP_HCCPARAMS1     0x10
#define XHCI_CAP_DBOFF          0x14
#define XHCI_CAP_RTSOFF         0x18
#define XHCI_HCC_AC64           (1u << 0)
#define XHCI_HCC_CSZ            (1u << 2)

// Operational registers.
#define XHCI_OP_USBCMD          0x00
#define XHCI_OP_USBSTS          0x04
#define XHCI_OP_PAGESIZE        0x08
#define XHCI_OP_CRCR            0x18
#define XHCI_OP_DCBAAP          0x30
#define XHCI_OP_CONFIG          0x38
#define XHCI_OP_PORTSC(p)       (0x400 + 0x10 * ((p) - 1))
#define USBCMD_RS               (1u << 0)
#define USBCMD_HCRST            (1u << 1)
#define USBCMD_INTE             (1u << 2)
#define USBSTS_HCH              (1u << 0)
#define USBSTS_EINT             (1u << 3)
#define USBSTS_CNR              (1u << 11)

#define PORTSC_CCS              (1u << 0)
#define PORTSC_PED              (1u << 1)
#define PORTSC_PR               (1u << 4)
#define PORTSC_PP               (1u << 9)
#define PORTSC_LWS              (1u << 16)
#define PORTSC_CSC              (1u << 17)
#define PORTSC_PRC              (1u << 21)
#define PORTSC_CHANGE           (0x7Fu << 17)   // CSC PEC WRC OCC PRC PLC CEC
#define PORTSC_WPR              (1u << 31)

// Interrupter 0 in the runtime registers.
#define XHCI_IR0_IMAN           0x20
#define XHCI_IR0_IMOD           0x24
#define XHCI_IR0_ERSTSZ         0x28
#define XHCI_IR0_ERSTBA         0x30
#define XHCI_IR0_ERDP           0x38
#define XHCI_IMAN_IP            (1u << 0)
#define XHCI_IMAN_IE            (1u << 1)
#define XHCI_IMOD_250US         1000            // 250 ns units
#define XHCI_ERDP_EHB           (1u << 3)

// Extended capabilities.
#define XHCI_XCAP_LEGACY        1
#define XHCI_XCAP_PROTOCOL      2
#define XHCI_LEGACY_BIOS_OWNED  (1u << 16)
#define XHCI_LEGACY_OS_OWNED    (1u << 24)
#define XHCI_LEGACY_SMI_KEEP    ((0x7u << 1) | (0xFFu << 5) | (0x7u << 17))
#define XHCI_LEGACY_SMI_EVENTS  (0x7u << 29)

// TRBs.
#define TRB_CYCLE               (1u << 0)
#define TRB_TC                  (1u << 1)
#define TRB_ISP                 (1u << 2)
#define TRB_CH                  (1u << 4)
#define TRB_IOC                 (1u << 5)
#define TRB_IDT                 (1u << 6)
#define TRB_DIR_IN              (1u << 16)
#define TRB_TRT_OUT             (2u << 16)
#define TRB_TRT_IN              (3u << 16)
#define TRB_TYPE(t)             ((uint32_t)(t) << 10)
#define TRB_GET_TYPE(c)         (((c) >> 10) & 0x3F)

enum {
    TRB_NORMAL = 1, TRB_SETUP = 2, TRB_DATA = 3, TRB_STATUS = 4, TRB_LINK = 6,
    TRB_ENABLE_SLOT = 9, TRB_DISABLE_SLOT = 10, TRB_ADDRESS_DEVICE = 11,
    TRB_CONFIGURE_ENDPOINT = 12, TRB_EVALUATE_CONTEXT = 13, TRB_RESET_ENDPOINT = 14,
    TRB_STOP_ENDPOINT = 15, TRB_SET_TR_DEQUEUE = 16,
    TRB_TRANSFER_EVENT = 32, TRB_COMMAND_COMPLETION = 33, TRB_PORT_STATUS_CHANGE = 34,
};

#define XHCI_CC_SUCCESS         1
#define XHCI_CC_STALL           6
#define XHCI_CC_SHORT_PACKET    13
#define XHCI_CC_GONE            0xFF            // ours: the device was unplugged

#define XHCI_EP_BULK_OUT        2
#define XHCI_EP_INT_OUT         3
#define XHCI_EP_CONTROL         4
#define XHCI_EP_BULK_IN         6
#define XHCI_EP_INT_IN          7

#define XHCI_RING_TRBS          256             // one page
#define XHCI_TD_MAX_PAGES       16              // 64 KiB per transfer
#define XHCI_CMD_TIMEOUT_MS     1000
#define XHCI_CTRL_TIMEOUT_MS    1000

struct xhci_trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

// A producer ring (command or transfer).
struct xhci_ring {
    volatile struct xhci_trb* trb;
    uint64_t                  phys;
    uint32_t                  enq;
    uint32_t                  cycle;
};

struct usb_endpoint {
    struct usb_device* dev;
    uint8_t            dci;
    uint8_t            addr;            // bEndpointAddress
    uint16_t           mps;
    bool               control;         // EP0: done at the status stage
    bool               input;           // a completion is BSP input work (HID)
    struct xhci_ring   ring;
    // The transfer in flight, written under hc->lock.
    volatile bool      busy;
    volatile bool      done;
    volatile uint8_t   cc;
    volatile uint32_t  actual;
    int                td_count;
    uint64_t           td_trb[XHCI_TD_MAX_PAGES + 2];
    uint32_t           td_len[XHCI_TD_MAX_PAGES + 2];
};

enum { XHCI_IRQ_NONE, XHCI_IRQ_MSIX, XHCI_IRQ_MSI, XHCI_IRQ_INTX };

struct xhci {
    struct pci_device*  pci;
    volatile uint8_t*   cap;
    volatile uint8_t*   op;
    volatile uint8_t*   rt;
    volatile uint8_t*   db;
    uint32_t            max_slots;
    uint32_t            max_ports;
    uint32_t            ctx_size;
    uint64_t*           dcbaa;
    uint64_t            dma_limit;      // 0 with AC64, else the first address it cannot reach
    bool                dma_refused;    // a page above dma_limit was refused (said once)
    struct spinlock     lock;           // the event ring and every producer ring
    struct spinlock     cmd_lock;       // one command at a time
    struct xhci_ring    cmd;
    volatile uint64_t   cmd_trb;
    volatile uint32_t   cmd_result;     // completion code | slot id << 24
    volatile bool       cmd_done;
    volatile struct xhci_trb* ev;
    uint64_t            ev_phys;
    uint32_t            ev_deq;
    uint32_t            ev_cycle;
    volatile bool       input_pending;  // HID completions or a port change for the BSP
    volatile bool       port_change;
    int                 irq_kind;
    int                 irq_slot;       // 0..XHCI_IRQ_SLOTS-1, -1 polled
    uint8_t             msi_cap;
    volatile uint8_t*   msix_entry;
    struct disk_irq     irq;            // mass storage waits; enabled = interrupts on
    uint8_t             port_major[256];
    struct usb_device*  port_dev[256];
    struct usb_device*  slot_dev[256];
};

#endif
