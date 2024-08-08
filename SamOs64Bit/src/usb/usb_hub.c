// USB hub class driver (USB 2.0 chapter 11, USB 3.2 chapter 10).
//
// A hub (interface class 9) is told to the controller as one (the slot
// context's Hub, Number of Ports and TT Think Time fields, set with the
// Configure Endpoint of its status change endpoint; xhci.c), a USB 3
// hub also learns its depth (SET_HUB_DEPTH). Every port is powered,
// and after bPwrOn2PwrGood each connected port is debounced, reset
// (SET_FEATURE(PORT_RESET) until C_PORT_RESET), and the device on it
// handed to usb_device_attach with the speed the port reports: USB 2
// hubs say low, full or high in wPortStatus, USB 3 hubs carry super
// speed devices only. usb_device_attach gives it a route string and,
// at low or full speed under a high-speed hub, that hub's transaction
// translator. A multi-TT hub runs in its alternate setting 0, single
// TT.
//
// After that the status change endpoint (interrupt IN, one bit per
// port, bit 0 the hub itself) is the plug event: it is an input
// endpoint, so its completion is BSP work (usb_service, xhci.c) like a
// HID report. Each port with its bit set is read with GET_STATUS, its
// change bits cleared, and a device gone or newly there is detached or
// attached. A hub that goes away detaches everything behind it first
// (usb_hub_disconnect, from usb_device_detach).
//
// Route strings have five tiers of four bits, so a hub five deep takes
// no devices, and ports above 15 are not used.

#include "usb.h"
#include "xhci.h"
#include "arch/arch.h"
#include "io/serial.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"
#include "status.h"

#define HUB_MAX_DEVICES         16
#define HUB_MAX_PORTS           15              // one route string nibble
#define HUB_MAX_DEPTH           5               // route string tiers

#define HUB_DT_HUB              0x29
#define HUB_DT_SS_HUB           0x2A
#define HUB_REQ_GET_STATUS      0
#define HUB_REQ_CLEAR_FEATURE   1
#define HUB_REQ_SET_FEATURE     3
#define HUB_REQ_SET_HUB_DEPTH   12
#define HUB_RT_OTHER            0x03            // recipient: a port

// Port features (USB 2.0 table 11-17, USB 3.2 table 10-9).
#define HUB_PORT_ENABLE         1
#define HUB_PORT_RESET          4
#define HUB_PORT_POWER          8
#define HUB_C_PORT_CONNECTION   16
#define HUB_C_HUB_LOCAL_POWER   0
#define HUB_C_HUB_OVER_CURRENT  1

// wPortStatus.
#define HUB_PS_CONNECTION       (1u << 0)
#define HUB_PS_ENABLE           (1u << 1)
#define HUB_PS_RESET            (1u << 4)
#define HUB_PS_LOW_SPEED        (1u << 9)       // USB 2 hubs
#define HUB_PS_HIGH_SPEED       (1u << 10)
// wPortChange.
#define HUB_PC_CONNECTION       (1u << 0)
#define HUB_PC_RESET            (1u << 4)

#define HUB_DEBOUNCE_MS         100             // TATTDB, USB 2.0 7.1.7.3
#define HUB_RESET_MS            500
#define HUB_RESET_RECOVERY_MS   10              // TRSTRCY

struct usb_hub {
    struct usb_device*   dev;
    struct usb_endpoint* ep;
    void*                page;                  // the status change bitmap
    uint8_t              len;
    uint8_t              ports;
    bool                 ss;                    // a USB 3 hub
    struct usb_device*   child[HUB_MAX_PORTS + 1];
};

static struct usb_hub* hubs[HUB_MAX_DEVICES];
static int hub_count;

static void hub_delay_ms(uint32_t ms){
    while(ms--){
        udelay(1000);
    }
}

static int hub_port_feature(struct usb_hub* h, uint8_t req, uint16_t feature, uint8_t port){
    return usb_control(h->dev, USB_RT_CLASS | HUB_RT_OTHER, req, feature, port, NULL, 0);
}

// wPortStatus | wPortChange << 16, or < 0.
static int64_t hub_port_status(struct usb_hub* h, uint8_t port){
    uint8_t s[4];
    if(usb_control(h->dev, USB_RT_IN | USB_RT_CLASS | HUB_RT_OTHER, HUB_REQ_GET_STATUS, 0, port, s, 4) < 4){
        return -EIO;
    }
    return (uint32_t)(s[0] | s[1] << 8) | (uint32_t)(s[2] | s[3] << 8) << 16;
}

// Clear every change bit `change` holds: C_PORT_* is the status bit's
// feature plus 16 (connection, enable, suspend, over-current, reset;
// USB 3: link state 25, config error 26, BH reset 29).
static void hub_port_clear_changes(struct usb_hub* h, uint8_t port, uint16_t change){
    static const uint8_t usb2[] = { 16, 17, 18, 19, 20 };
    static const uint8_t usb3[] = { 16, 0, 0, 19, 20, 29, 25, 26 };
    const uint8_t* map = h->ss ? usb3 : usb2;
    int n = h->ss ? (int)sizeof(usb3) : (int)sizeof(usb2);
    for(int b = 0; b < n; b++){
        if((change & (1u << b)) && map[b]){
            hub_port_feature(h, HUB_REQ_CLEAR_FEATURE, map[b], port);
        }
    }
}

static void hub_write_port(struct usb_hub* h, uint8_t port){
    serial_write("usb-hub: slot ");
    serial_write(itoa(h->dev->slot));
    serial_write(" port ");
    serial_write(itoa(port));
}

// Reset the port; the speed of the device on it, or < 0.
static int hub_port_reset(struct usb_hub* h, uint8_t port){
    if(hub_port_feature(h, HUB_REQ_SET_FEATURE, HUB_PORT_RESET, port) < 0){
        return -EIO;
    }
    int64_t st = -EIO;
    for(int t = 0; t < HUB_RESET_MS / 10; t++){
        hub_delay_ms(10);
        st = hub_port_status(h, port);
        if(st < 0 || ((st >> 16) & HUB_PC_RESET) || !(st & HUB_PS_RESET)){
            break;
        }
    }
    if(st < 0){
        return -EIO;
    }
    hub_port_clear_changes(h, port, (uint16_t)(st >> 16) & ~HUB_PC_CONNECTION);
    hub_delay_ms(HUB_RESET_RECOVERY_MS);
    st = hub_port_status(h, port);
    if(st < 0 || !(st & HUB_PS_CONNECTION) || !(st & HUB_PS_ENABLE)){
        return -EIO;
    }
    if(h->ss){
        return USB_SPEED_SUPER;
    }
    if(st & HUB_PS_LOW_SPEED){
        return USB_SPEED_LOW;
    }
    return st & HUB_PS_HIGH_SPEED ? USB_SPEED_HIGH : USB_SPEED_FULL;
}

// A port whose change bit was set (or every port at probe): detach
// what left, attach what came.
static void hub_port_check(struct usb_hub* h, uint8_t port){
    int64_t st = hub_port_status(h, port);
    if(st < 0){
        return;
    }
    uint16_t status = (uint16_t)st, change = (uint16_t)(st >> 16);
    hub_port_clear_changes(h, port, change);
    struct usb_device* child = h->child[port];
    if(child && (!(status & HUB_PS_CONNECTION) || (change & HUB_PC_CONNECTION))){
        h->child[port] = NULL;
        usb_device_detach(child);
        child = NULL;
    }
    if(child || !(status & HUB_PS_CONNECTION) || h->dev->gone){
        return;
    }
    if(h->dev->depth + 1 > HUB_MAX_DEPTH){
        hub_write_port(h, port);
        serial_write(": too deep, not used\n");
        return;
    }
    hub_delay_ms(HUB_DEBOUNCE_MS);
    int speed = hub_port_reset(h, port);
    if(speed <= 0){
        hub_write_port(h, port);
        serial_write(" did not enable\n");
        return;
    }
    h->child[port] = usb_device_attach(h->dev, port, (uint8_t)speed);
    if(!h->child[port] && !h->ss){
        hub_port_feature(h, HUB_REQ_CLEAR_FEATURE, HUB_PORT_ENABLE, port);
    }
}

int usb_hub_probe(struct usb_device* dev, const struct usb_interface_descriptor* intf){
    if(intf->bInterfaceClass != 9){
        return -ENOENT;
    }
    if(hub_count == HUB_MAX_DEVICES){
        serial_write("usb-hub: too many hubs\n");
        return -ENOSPC;
    }
    const struct usb_endpoint_descriptor* ed = NULL;
    for(const uint8_t* p = usb_config_next(dev, (const uint8_t*)intf, USB_DT_ENDPOINT, true); p;
        p = usb_config_next(dev, p, USB_DT_ENDPOINT, true)){
        const struct usb_endpoint_descriptor* e = (const void*)p;
        if((e->bEndpointAddress & USB_EP_IN) && (e->bmAttributes & 3) == USB_EP_INTERRUPT){
            ed = e;
            break;
        }
    }
    if(!ed){
        return -ENOENT;
    }
    struct usb_hub* h = kzalloc(sizeof(*h));
    if(!h || !(h->page = usb_dma_page(dev))){
        kfree(h);
        return -ENOMEM;
    }
    h->dev = dev;
    h->ss  = dev->speed >= USB_SPEED_SUPER;
    // The hub descriptor: bNbrPorts, wHubCharacteristics, bPwrOn2PwrGood.
    uint8_t hd[16] = {0};
    int n = usb_control(dev, USB_RT_IN | USB_RT_CLASS, USB_REQ_GET_DESCRIPTOR,
                        (h->ss ? HUB_DT_SS_HUB : HUB_DT_HUB) << 8, 0, hd, h->ss ? 12 : 9);
    if(n < 7){
        serial_write("usb-hub: slot ");
        serial_write(itoa(dev->slot));
        serial_write(", no hub descriptor\n");
        kfree(h->page);
        kfree(h);
        return -EIO;
    }
    uint8_t ports = hd[2];
    uint16_t chars = hd[3] | hd[4] << 8;
    uint32_t power_ms = hd[5] * 2u;
    h->ports = ports > HUB_MAX_PORTS ? HUB_MAX_PORTS : ports;
    if(h->ss){
        usb_control(dev, USB_RT_CLASS, HUB_REQ_SET_HUB_DEPTH, dev->depth, 0, NULL, 0);
    }
    dev->is_hub    = true;
    dev->hub_ports = ports;
    // TT think time, wHubCharacteristics bits 6:5 (high-speed hubs).
    dev->hub_ttt   = dev->speed == USB_SPEED_HIGH ? (chars >> 5) & 3 : 0;
    h->ep = usb_endpoint_open(dev, ed);
    if(!h->ep){
        dev->is_hub = false;
        kfree(h->page);
        kfree(h);
        return -EIO;
    }
    h->ep->input = true;
    uint16_t mps = ed->wMaxPacketSize & 0x7FF;
    uint8_t want = (uint8_t)((ports + 8) / 8);
    h->len = mps && mps < want ? (uint8_t)mps : want;
    dev->driver = "usb-hub";
    dev->driver_data = h;
    hubs[hub_count++] = h;
    serial_write("usb-hub: slot ");
    serial_write(itoa(dev->slot));
    serial_write(", ");
    serial_write(itoa(ports));
    serial_write(h->ss ? " ports, USB 3" : " ports, USB 2");
    if(dev->speed == USB_SPEED_HIGH){
        serial_write(dev->desc.bDeviceProtocol == 2 ? ", multi-TT used as single" : ", single TT");
        serial_write(", think time ");
        serial_write(itoa(8 * (dev->hub_ttt + 1)));
        serial_write(" FS bit times");
    }
    serial_write(", depth ");
    serial_write(itoa(dev->depth));
    serial_write("\n");

    for(uint8_t p = 1; p <= h->ports; p++){
        hub_port_feature(h, HUB_REQ_SET_FEATURE, HUB_PORT_POWER, p);
    }
    hub_delay_ms(power_ms > 20 ? power_ms : 20);
    for(uint8_t p = 1; p <= h->ports && !dev->gone; p++){
        hub_port_check(h, p);
    }
    if(!dev->gone){
        usb_transfer_start(h->ep, &h->page, h->len);
    }
    return 0;
}

void usb_hub_service(void){
    for(int i = 0; i < hub_count; i++){
        struct usb_hub* h = hubs[i];
        if(h->dev->gone || !h->ep->busy || !usb_transfer_done(h->ep)){
            continue;
        }
        bool stalled = false;
        int n = usb_transfer_wait(h->ep, false, 0, &stalled);
        uint8_t bits[2] = {0};
        if(n > 0){
            memcpy(bits, h->page, n < 2 ? n : 2);
        }
        if(stalled){
            usb_endpoint_clear_halt(h->ep);
        }
        if(bits[0] & 1){
            // The hub itself: local power or over-current changed.
            uint8_t s[4];
            if(usb_control(h->dev, USB_RT_IN | USB_RT_CLASS, HUB_REQ_GET_STATUS, 0, 0, s, 4) == 4){
                if(s[2] & 1){
                    usb_control(h->dev, USB_RT_CLASS, HUB_REQ_CLEAR_FEATURE, HUB_C_HUB_LOCAL_POWER, 0, NULL, 0);
                }
                if(s[2] & 2){
                    usb_control(h->dev, USB_RT_CLASS, HUB_REQ_CLEAR_FEATURE, HUB_C_HUB_OVER_CURRENT, 0, NULL, 0);
                }
            }
        }
        int count = hub_count;
        for(uint8_t p = 1; p <= h->ports && !h->dev->gone; p++){
            if(bits[p / 8] & (1u << (p % 8))){
                hub_port_check(h, p);
            }
        }
        if(!h->dev->gone){
            usb_transfer_start(h->ep, &h->page, h->len);
        }
        // A hub came or went behind this one: the list was reordered,
        // so walk it again (a hub already served is busy again).
        if(hub_count != count){
            i = -1;
        }
    }
}

void usb_hub_disconnect(struct usb_device* dev){
    for(int i = 0; i < hub_count; i++){
        struct usb_hub* h = hubs[i];
        if(h->dev != dev){
            continue;
        }
        hubs[i] = hubs[--hub_count];               // struct and page leak: small
        for(uint8_t p = 1; p <= h->ports; p++){
            struct usb_device* child = h->child[p];
            h->child[p] = NULL;
            if(child){
                usb_device_detach(child);
            }
        }
        return;
    }
}
