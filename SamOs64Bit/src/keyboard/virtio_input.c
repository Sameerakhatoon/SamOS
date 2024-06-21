// virtio-input keyboard (virtio 1.x, spec 5.8).
//
// The device sends Linux input events (type, code, value) on queue 0,
// into 8-byte buffers the driver keeps posted. EV_KEY events become
// scancode set 1 bytes and go through classic_keyboard_translate, the
// same state machine as PS/2: one set of modifiers and one Caps Lock,
// whichever keyboard pressed them, and the same keyboard_push to the
// focused window's process. Other event types (EV_SYN, EV_MSC, LEDs)
// are dropped; the status queue (LEDs) is not set up.
//
// Interrupts: the device's line (virtio_bus.h: PCI INTx, or on the ports its
// MMIO slot's SPI) is a disk completion line and may be shared with a
// virtio-blk, so its handler can run inside a disk wait, under
// disk_lock, where keyboard_push (gfx, process locks) must not be
// taken (docs/locking.md). The line handler therefore only sends
// ARCH_IPI_INPUT to the BSP; a disk wait holds that IPI off (x86:
// vector 0x48, priority class 4, under TPR 0x40; aarch64: an SGI under
// the wait's ICC_PMR_EL1), and it runs once the CPU leaves the wait and opens interrupts.
// Running every key on the BSP also keeps the PS/2 handler (IRQ1, BSP)
// and this one from racing on the translator state.
//
// Without a routable line (samos.legacy_pic, another chipset) the
// device is set up but no key arrives; PS/2 still works.

#include "virtio_input.h"
#include "keyboard.h"
#include "classic.h"
#include "virtio/virtio.h"
#include "virtio/virtio_bus.h"
#include "virtio/virtio_io.h"
#ifndef VIRTIO_HOST_TEST
#include "cpu/smp.h"
#endif
#include "io/serial.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "bootmarker.h"
#include "status.h"

#define VIRTIO_INPUT_CFG_SELECT   0
#define VIRTIO_INPUT_CFG_SUBSEL   1
#define VIRTIO_INPUT_CFG_SIZE     2
#define VIRTIO_INPUT_CFG_DATA     8
#define VIRTIO_INPUT_CFG_ID_NAME  0x01
#define VIRTIO_INPUT_CFG_EV_BITS  0x11

#define EV_KEY                    0x01
#define KEY_A                     30

#define VINPUT_QUEUE_SIZE         64
#define VINPUT_MAX_DEVICES        4

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;                        // 0 release, 1 press, 2 repeat
} __attribute__((packed));

// Linux key codes 1-83 and 86-88 are the set 1 scancodes themselves
// (KEY_ESC = 1 ... KEY_KPDOT = 83, KEY_102ND, KEY_F11, KEY_F12). The
// keys after them are the E0-prefixed ones classic.c knows.
static const struct { uint16_t code; uint8_t scancode; } vinput_extended[] = {
    {96,  0x1C},    // KEY_KPENTER
    {97,  0x1D},    // KEY_RIGHTCTRL
    {98,  0x35},    // KEY_KPSLASH
    {100, 0x38},    // KEY_RIGHTALT
    {102, 0x47},    // KEY_HOME
    {103, 0x48},    // KEY_UP
    {104, 0x49},    // KEY_PAGEUP
    {105, 0x4B},    // KEY_LEFT
    {106, 0x4D},    // KEY_RIGHT
    {107, 0x4F},    // KEY_END
    {108, 0x50},    // KEY_DOWN
    {109, 0x51},    // KEY_PAGEDOWN
    {110, 0x52},    // KEY_INSERT
    {111, 0x53},    // KEY_DELETE
};

int virtio_input_key_to_set1(unsigned int code){
    if((code >= 1 && code <= 83) || (code >= 86 && code <= 88)){
        return (int)code;
    }
    for(unsigned i = 0; i < sizeof(vinput_extended) / sizeof(vinput_extended[0]); i++){
        if(vinput_extended[i].code == code){
            return 0x100 | vinput_extended[i].scancode;
        }
    }
    return 0;
}

#ifndef VIRTIO_HOST_TEST

struct vinput {
    struct virtio_dev*         vdev;
    struct virtq               q;
    struct virtio_input_event* events;     // one page, one event per descriptor
    uint64_t                   events_phys;
};

static struct vinput* vinput_devices[VINPUT_MAX_DEVICES];
static int vinput_device_count;
static struct cpu* vinput_bsp;
static uint32_t vinput_key_events;

static void vinput_feed(uint8_t scancode){
    char seq[CLASSIC_KEYBOARD_MAX_SEQUENCE];
    int n = classic_keyboard_translate(scancode, seq);
    for(int i = 0; i < n; i++){
        keyboard_push(seq[i]);
    }
}

static void vinput_key(uint16_t code, uint32_t value){
    int sc = virtio_input_key_to_set1(code);
    if(!sc){
        return;
    }
    struct keyboard* kbd = classic_init();
    KEYBOARD_CAPS_LOCK_STATE caps_before = keyboard_get_capslock(kbd);
    if(sc & 0x100){
        vinput_feed(CLASSIC_KEYBOARD_E0_PREFIX);
    }
    vinput_feed((uint8_t)sc | (value == 0 ? CLASSIC_KEYBOARD_KEY_RELEASED : 0));
    if(keyboard_get_capslock(kbd) != caps_before){
        struct keyboard_event event = {0};
        event.type = KEYBOARD_EVENT_CAPS_LOCK_CHANGE;
        event.data.caps_lock.state = keyboard_get_capslock(kbd);
        keyboard_push_event_to_listeners(kbd, &event);
    }
    if(++vinput_key_events == 1){
        serial_write("virtio-input: first key event\n");
    }
    boot_marker_set(BM_FEATURE_VIRTIO_INPUT_KEYS, vinput_key_events & 0xFFFFFF);
}

static void vinput_post(struct vinput* v, uint64_t phys){
    struct virtq_buf buf = { phys, sizeof(struct virtio_input_event), true };
    virtq_add(&v->q, &buf, 1);
}

// ARCH_IPI_INPUT, on the BSP: take every event, give its buffer back.
static void vinput_service(void){
    for(int i = 0; i < vinput_device_count; i++){
        struct vinput* v = vinput_devices[i];
        uint32_t head;
        bool any = false;
        while(virtq_pop_used(&v->q, &head, NULL)){
            uint64_t phys = v->q.desc[head].addr;
            // The event is the device's (the CPU never writes one).
            virtio_dma_inval(&v->events[(phys - v->events_phys) / sizeof(struct virtio_input_event)],
                             sizeof(struct virtio_input_event));
            struct virtio_input_event e = v->events[(phys - v->events_phys) / sizeof(e)];
            if(e.type == EV_KEY){
                vinput_key(e.code, e.value);
            }
            vinput_post(v, phys);
            any = true;
        }
        if(any){
            virtq_kick(&v->q);
        }
    }
}

// The line's handler, possibly inside a disk wait: hand over to the BSP.
static void vinput_irq(void* ctx){
    arch_send_ipi(vinput_bsp, ARCH_IPI_INPUT);
}

static uint8_t vinput_cfg_select(struct virtio_dev* d, uint8_t select, uint8_t subsel){
    d->ops->cfg_write8(d, VIRTIO_INPUT_CFG_SELECT, select);
    d->ops->cfg_write8(d, VIRTIO_INPUT_CFG_SUBSEL, subsel);
    return d->ops->cfg_read8(d, VIRTIO_INPUT_CFG_SIZE);
}

static int vinput_attach(struct virtio_dev* d, void* ctx){
    if(vinput_device_count == VINPUT_MAX_DEVICES){
        return -ENOSPC;
    }
    int res = virtio_negotiate(d, 0);
    if(res < 0){
        return res;
    }
    // A keyboard reports EV_KEY with the letter keys; a mouse or tablet
    // has EV_KEY too, for its buttons only.
    if(vinput_cfg_select(d, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY) <= KEY_A / 8 ||
       !(d->ops->cfg_read8(d, VIRTIO_INPUT_CFG_DATA + KEY_A / 8) & (1 << (KEY_A % 8)))){
        return -ENOENT;
    }
    char name[33];
    uint8_t len = vinput_cfg_select(d, VIRTIO_INPUT_CFG_ID_NAME, 0);
    if(len > 32){
        len = 32;
    }
    for(uint8_t i = 0; i < len; i++){
        name[i] = (char)d->ops->cfg_read8(d, VIRTIO_INPUT_CFG_DATA + i);
    }
    name[len] = 0;

    struct vinput* v = kzalloc(sizeof(*v));
    if(!v){
        return -ENOMEM;
    }
    v->vdev = d;
    v->events = virtio_dma_page();
    if(!v->events || virtq_setup(d, &v->q, 0, VINPUT_QUEUE_SIZE) < 0){
        virtio_reset(d);
        virtio_dma_free(v->events);
        kfree(v);
        return -EIO;
    }
    v->events_phys = virtio_dma_phys(v->events);
    for(uint16_t i = 0; i < v->q.size; i++){
        vinput_post(v, v->events_phys + i * sizeof(struct virtio_input_event));
    }
    vinput_devices[vinput_device_count++] = v;
    virtio_driver_ok(d);
    virtq_kick(&v->q);

    struct virtio_line* line = virtio_irq(d, false, vinput_irq, v);
    serial_write("virtio-input: ");
    serial_write(name);
    if(line){
        serial_write(", GSI ");
        serial_write(itoa((int)line->gsi));
        serial_write(", vector ");
        serial_write(itoa(line->vector));
        serial_write(", keys by IPI to the BSP\n");
    } else {
        serial_write(", no interrupt route, keys not delivered\n");
    }
    return 0;
}

void virtio_input_init(void){
    vinput_bsp = this_cpu();
    smp_input_register(vinput_service);
    virtio_probe(VIRTIO_ID_INPUT, vinput_attach, NULL);
}

#endif
