#ifndef USB_USB_H
#define USB_USB_H
// USB: the xHCI host controller (xhci.c) and what the class
// drivers (usb_hid.c, disk/drivers/usb_msc.c) see of a device.
//
// Shared code: registers through arch.h's mmio_*, configuration space
// through io/pci.h, DMA memory one page at a time (usb_dma_page:
// kpage_zalloc and paging_kernel_virt_to_phys), so the Pi 4's VL805 and the
// VisionFive 2's Cadence controller can reuse it.
//
// One device per root port: no hubs, so no route strings and no
// transaction translators. A device is bound to the first interface
// a class driver takes: HID boot keyboard or mouse, or mass storage
// bulk-only SCSI.

#include <stdint.h>
#include <stdbool.h>

#define USB_SPEED_FULL       1     // xHCI default protocol speed ids
#define USB_SPEED_LOW        2
#define USB_SPEED_HIGH       3
#define USB_SPEED_SUPER      4

#define USB_DT_DEVICE        1
#define USB_DT_CONFIG        2
#define USB_DT_STRING        3
#define USB_DT_INTERFACE     4
#define USB_DT_ENDPOINT      5
#define USB_DT_SS_COMPANION  0x30

#define USB_REQ_CLEAR_FEATURE      1
#define USB_REQ_GET_DESCRIPTOR     6
#define USB_REQ_SET_CONFIGURATION  9

// bmRequestType: direction, type, recipient.
#define USB_RT_IN            0x80
#define USB_RT_CLASS         0x20
#define USB_RT_INTERFACE     0x01
#define USB_RT_ENDPOINT      0x02

#define USB_EP_IN            0x80
#define USB_EP_BULK          2
#define USB_EP_INTERRUPT     3

struct usb_device_descriptor {
    uint8_t  bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t  iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} __attribute__((packed));

struct usb_interface_descriptor {
    uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting;
    uint8_t bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
    uint8_t  bLength, bDescriptorType, bEndpointAddress, bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

struct xhci;
struct usb_endpoint;

struct usb_device {
    struct xhci*         hc;
    uint8_t              slot;
    uint8_t              port;          // root hub port, 1-based
    uint8_t              speed;         // USB_SPEED_*
    uint8_t              max_dci;       // highest endpoint context in use
    uint16_t             mps0;
    struct usb_device_descriptor desc;
    uint8_t*             config;        // the whole configuration descriptor
    uint16_t             config_len;
    char                 product[40];
    volatile bool        gone;          // unplugged: every transfer fails
    const char*          driver;        // the class driver that took it
    void*                driver_data;
    struct usb_endpoint* ep[32];        // by device context index
    void*                ctx_page;      // output device context
    void*                in_page;       // input context
    void*                buf_page;      // control transfer data
};

// usb_init: find and start every xHCI controller and enumerate what is
// plugged in. Idempotent; the first of the disk mount and keyboard_init
// does the work.
void usb_init(void);
// An xHCI without PCI (device tree "generic-xhci", a Cadence
// USB3 core in host mode): its registers and its interrupt line (a
// GSI with ACPI MPS INTI flags; 0 polls). Before usb_init.
void usb_xhci_add_mmio(uint64_t phys, uint64_t size, uint32_t gsi, uint16_t inti);

// Control transfer on endpoint 0 (len <= 4096). Bytes moved, or < 0.
int usb_control(struct usb_device* dev, uint8_t type, uint8_t req, uint16_t value,
                uint16_t index, void* data, uint16_t len);

// Set up the endpoint `ed` (bulk or interrupt, from dev->config) with
// a transfer ring. NULL on failure.
struct usb_endpoint* usb_endpoint_open(struct usb_device* dev, const struct usb_endpoint_descriptor* ed);

// One zeroed page the device's controller can reach (below
// 4 GiB without AC64), else NULL. Every transfer page comes from here.
void* usb_dma_page(struct usb_device* dev);

// Queue one transfer on a bulk or interrupt endpoint: `len` bytes
// through `pages` (each a page from usb_dma_page). One transfer per
// endpoint at a time.
int  usb_transfer_start(struct usb_endpoint* ep, void** pages, uint32_t len);
// Before a transfer the caller will wait for with irq set (a disk
// driver under disk_lock): point the line at this CPU and drop an old
// signal (disk_irq_arm).
void usb_irq_arm(struct usb_device* dev);
// Collect events; true once the transfer ended.
bool usb_transfer_done(struct usb_endpoint* ep);
// Wait for it: through the controller's completion line when the
// caller may (a disk driver under disk_lock), else polling. Bytes
// moved, -EIO (with *stalled set on a STALL), -ETIMEOUT.
int  usb_transfer_wait(struct usb_endpoint* ep, bool irq, uint32_t ms, bool* stalled);
// After a STALL: reset the endpoint on both sides (Reset Endpoint,
// Set TR Dequeue Pointer, CLEAR_FEATURE(ENDPOINT_HALT)).
int  usb_endpoint_clear_halt(struct usb_endpoint* ep);
uint8_t usb_endpoint_address(struct usb_endpoint* ep);

// Walk dev->config: the next descriptor of `type` after `from` (NULL:
// from the start), stopping at the next interface descriptor when
// `in_iface` is set. NULL when there is none.
const uint8_t* usb_config_next(struct usb_device* dev, const uint8_t* from, uint8_t type, bool in_iface);

// Class drivers. probe returns 0 when it took the device.
int  usb_hid_probe(struct usb_device* dev, const struct usb_interface_descriptor* intf);
void usb_hid_disconnect(struct usb_device* dev);
void usb_hid_service(void);
// mouse.c, once the mouse layer is up: USB mice move `ps2`'s cursor,
// or one of their own registered then when `ps2` is NULL.
struct mouse;
void usb_hid_mouse_attach(struct mouse* ps2);
int  usb_msc_probe(struct usb_device* dev, const struct usb_interface_descriptor* intf);
void usb_msc_disconnect(struct usb_device* dev);

// Input work (HID reports, plug events) runs on the BSP: ARCH_IPI_INPUT
// from the controller's interrupt, or the BSP's tick when polled.
void usb_input_kick(struct xhci* hc);

#endif
