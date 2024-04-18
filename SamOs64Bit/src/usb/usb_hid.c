// USB HID boot protocol keyboard and mouse.
//
// SET_PROTOCOL(boot) makes every keyboard send the same 8-byte report
// (modifier bits, a reserved byte, up to six usages pressed) and every
// mouse buttons, dx, dy, so no report descriptor is parsed. Reports
// come from one interrupt IN transfer per device, queued again as
// soon as it completes.
//
// Keys become scancode set 1 bytes for classic_keyboard_translate and
// keyboard_push, as virtio-input does: one set of modifiers and one
// Caps Lock with PS/2, and the focused process gets the same bytes.
// A key is a press when its usage appears in a report and a release
// when it is gone from the next one. No typematic repeat (the host
// makes it for USB keyboards; SamOS has none yet). Caps Lock sets the
// keyboard's LED with SET_REPORT.
//
// A mouse moves the PS/2 cursor when there is one, else a cursor of
// its own registered with the mouse layer (mouse.c calls
// usb_hid_mouse_attach once it is up; reports before that are dropped).
//
// Everything here runs on the BSP from usb_service (ARCH_IPI_INPUT or
// the tick, xhci.c), outside any disk wait, so keyboard_push and the
// graphics lock may be taken.

#include "usb.h"
#include "xhci.h"
#include "keyboard/keyboard.h"
#include "keyboard/classic.h"
#include "mouse/mouse.h"
#include "graphics/graphics.h"
#include "io/serial.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"
#include "bootmarker.h"
#include "status.h"

#define HID_MAX_DEVICES         8
#define HID_REQ_SET_REPORT      0x09
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B
#define HID_PROTOCOL_KEYBOARD   1
#define HID_PROTOCOL_MOUSE      2
#define HID_REPORT_MAX          8

struct usb_hid {
    struct usb_device*   dev;
    struct usb_endpoint* ep;
    void*                page;          // the report buffer
    uint8_t              iface;
    uint8_t              len;
    bool                 mouse;
    uint8_t              leds;
    uint8_t              last[HID_REPORT_MAX];
};

static struct usb_hid* hid_devices[HID_MAX_DEVICES];
static int hid_count;
static uint32_t hid_key_events;
static uint32_t hid_mouse_reports;
static struct mouse* hid_mouse_target;

// HID usage (keyboard page) to set 1; 0x100 marks the E0 keys.
static const uint16_t hid_usage_set1[] = {
    [0x04] = 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, // a-l
    [0x10] = 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, // m-x
    [0x1C] = 0x15, 0x2C,                                                             // y z
    [0x1E] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,             // 1-0
    [0x28] = 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B,       // Enter .. #
    [0x33] = 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A,                               // ; ' ` , . / Caps
    [0x3A] = 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58, // F1-F12
    [0x46] = 0x137, 0x46, 0,                                                         // PrtSc ScrLk Pause
    [0x49] = 0x152, 0x147, 0x149, 0x153, 0x14F, 0x151,                               // Ins Home PgUp Del End PgDn
    [0x4F] = 0x14D, 0x14B, 0x150, 0x148,                                             // Right Left Down Up
    [0x53] = 0x45, 0x135, 0x37, 0x4A, 0x4E, 0x11C,                                   // NumLk KP/ * - + Enter
    [0x59] = 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53,       // KP1-9 0 .
    [0x64] = 0x56,                                                                   // non-US backslash
};

// Modifier bits 0-7: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI.
static const uint16_t hid_modifier_set1[8] = { 0x1D, 0x2A, 0x38, 0x15B, 0x11D, 0x36, 0x138, 0x15C };

static void hid_feed(uint8_t scancode){
    char seq[CLASSIC_KEYBOARD_MAX_SEQUENCE];
    int n = classic_keyboard_translate(scancode, seq);
    for(int i = 0; i < n; i++){
        keyboard_push(seq[i]);
    }
}

static void hid_key(uint16_t sc, bool press){
    if(!sc){
        return;
    }
    struct keyboard* kbd = classic_init();
    KEYBOARD_CAPS_LOCK_STATE caps_before = keyboard_get_capslock(kbd);
    if(sc & 0x100){
        hid_feed(CLASSIC_KEYBOARD_E0_PREFIX);
    }
    hid_feed((uint8_t)sc | (press ? 0 : CLASSIC_KEYBOARD_KEY_RELEASED));
    if(keyboard_get_capslock(kbd) != caps_before){
        struct keyboard_event event = {0};
        event.type = KEYBOARD_EVENT_CAPS_LOCK_CHANGE;
        event.data.caps_lock.state = keyboard_get_capslock(kbd);
        keyboard_push_event_to_listeners(kbd, &event);
    }
    if(++hid_key_events == 1){
        serial_write("usb-hid: first key event\n");
    }
    boot_marker_set(BM_FEATURE_USB_KEYS, hid_key_events & 0xFFFFFF);
}

static uint16_t hid_usage(uint8_t u){
    return u < sizeof(hid_usage_set1) / sizeof(hid_usage_set1[0]) ? hid_usage_set1[u] : 0;
}

static bool hid_has(const uint8_t* r, uint8_t u){
    for(int i = 2; i < 8; i++){
        if(r[i] == u){
            return true;
        }
    }
    return false;
}

static void hid_keyboard_report(struct usb_hid* h, const uint8_t* r){
    if(r[2] == 1){
        return;                     // phantom state: too many keys down
    }
    for(int b = 0; b < 8; b++){
        if((r[0] ^ h->last[0]) & (1 << b)){
            hid_key(hid_modifier_set1[b], r[0] & (1 << b));
        }
    }
    for(int i = 2; i < 8; i++){
        if(h->last[i] > 3 && !hid_has(r, h->last[i])){
            hid_key(hid_usage(h->last[i]), false);
        }
    }
    for(int i = 2; i < 8; i++){
        if(r[i] > 3 && !hid_has(h->last, r[i])){
            hid_key(hid_usage(r[i]), true);
        }
    }
    memcpy(h->last, (void*)r, 8);
    uint8_t leds = keyboard_get_capslock(classic_init()) == KEYBOARD_CAPS_LOCK_ON ? 2 : 0;
    if(leds != h->leds){
        h->leds = leds;
        usb_control(h->dev, USB_RT_CLASS | USB_RT_INTERFACE, HID_REQ_SET_REPORT, 2 << 8, h->iface, &leds, 1);
    }
}

static void hid_mouse_report(struct usb_hid* h, const uint8_t* r){
    struct mouse* m = hid_mouse_target;
    if(!m){
        return;
    }
    gfx_lock();
    struct graphics_info* screen = graphics_screen_info();
    int x = m->coords.x + (int8_t)r[1];
    int y = m->coords.y + (int8_t)r[2];          // USB: positive is down
    int max_x = (int)screen->width - m->graphic.width;
    int max_y = (int)screen->height - m->graphic.height;
    x = x < 0 ? 0 : x > max_x ? max_x : x;
    y = y < 0 ? 0 : y > max_y ? max_y : y;
    mouse_position_set(m, x, y);
    if(r[0] & 1){
        mouse_click(m, MOUSE_LEFT_BUTTON_CLICKED);
    } else if(r[0] & 2){
        mouse_click(m, MOUSE_RIGHT_BUTTON_CLICKED);
    }
    mouse_moved(m);
    gfx_unlock();
    if(++hid_mouse_reports == 1){
        serial_write("usb-hid: first mouse report, cursor at ");
        serial_write(itoa(x));
        serial_write(",");
        serial_write(itoa(y));
        serial_write("\n");
    }
}

void usb_hid_service(void){
    for(int i = 0; i < hid_count; i++){
        struct usb_hid* h = hid_devices[i];
        if(h->dev->gone || !h->ep->busy || !usb_transfer_done(h->ep)){
            continue;
        }
        bool stalled = false;
        int n = usb_transfer_wait(h->ep, false, 0, &stalled);
        uint8_t r[HID_REPORT_MAX] = {0};
        if(n > 0){
            memcpy(r, h->page, n < HID_REPORT_MAX ? n : HID_REPORT_MAX);
        }
        if(stalled){
            usb_endpoint_clear_halt(h->ep);
        }
        if(h->dev->gone || usb_transfer_start(h->ep, &h->page, h->len) < 0){
            continue;
        }
        if(n >= 3 && h->mouse){
            hid_mouse_report(h, r);
        } else if(n >= 3){
            hid_keyboard_report(h, r);
        }
    }
}

int usb_hid_probe(struct usb_device* dev, const struct usb_interface_descriptor* intf){
    if(intf->bInterfaceClass != 3 || intf->bInterfaceSubClass != 1 ||
       (intf->bInterfaceProtocol != HID_PROTOCOL_KEYBOARD && intf->bInterfaceProtocol != HID_PROTOCOL_MOUSE) ||
       hid_count == HID_MAX_DEVICES){
        return -ENOENT;
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
    struct usb_hid* h = kzalloc(sizeof(*h));
    if(!h || !(h->page = kpage_zalloc(4096))){
        kfree(h);
        return -ENOMEM;
    }
    h->dev   = dev;
    h->iface = intf->bInterfaceNumber;
    h->mouse = intf->bInterfaceProtocol == HID_PROTOCOL_MOUSE;
    usb_control(dev, USB_RT_CLASS | USB_RT_INTERFACE, HID_REQ_SET_PROTOCOL, 0, h->iface, NULL, 0);
    // Reports only on change; a device that stalls this is fine.
    usb_control(dev, USB_RT_CLASS | USB_RT_INTERFACE, HID_REQ_SET_IDLE, 0, h->iface, NULL, 0);
    h->ep = usb_endpoint_open(dev, ed);
    if(!h->ep){
        kfree(h->page);
        kfree(h);
        return -EIO;
    }
    h->ep->input = true;
    uint16_t mps = ed->wMaxPacketSize & 0x7FF;
    h->len = mps < HID_REPORT_MAX ? mps : HID_REPORT_MAX;
    hid_devices[hid_count++] = h;
    dev->driver = "usb-hid";
    dev->driver_data = h;
    usb_transfer_start(h->ep, &h->page, h->len);
    serial_write(h->mouse ? "usb-hid: mouse" : "usb-hid: keyboard");
    serial_write(", slot ");
    serial_write(itoa(dev->slot));
    serial_write(", boot protocol, endpoint ");
    serial_write(itoa(ed->bEndpointAddress & 0xF));
    serial_write(" IN, interval ");
    serial_write(itoa(ed->bInterval));
    serial_write("\n");
    return 0;
}

void usb_hid_disconnect(struct usb_device* dev){
    for(int i = 0; i < hid_count; i++){
        if(hid_devices[i]->dev == dev){
            // Keys still down would stay down: release them.
            uint8_t none[HID_REPORT_MAX] = {0};
            if(!hid_devices[i]->mouse){
                hid_keyboard_report(hid_devices[i], none);
            }
            hid_devices[i] = hid_devices[--hid_count];   // struct and pages leak: small
            return;
        }
    }
}

static int usb_mouse_init(struct mouse* m){
    return 0;
}

static struct mouse usb_mouse = {
    .name = {"usb_mouse"},
    .init = usb_mouse_init,
};

// Without a PS/2 mouse the cursor of our own is registered only when a
// USB mouse is there at boot; one plugged in later then has none.
void usb_hid_mouse_attach(struct mouse* ps2){
    hid_mouse_target = ps2;
    for(int i = 0; i < hid_count; i++){
        if(!hid_devices[i]->mouse){
            continue;
        }
        if(ps2){
            serial_write("usb-hid: mice move the PS/2 cursor\n");
        } else if(mouse_register(&usb_mouse) == 0){
            hid_mouse_target = &usb_mouse;
            serial_write("usb-hid: cursor registered with the mouse layer\n");
        }
        return;
    }
}
