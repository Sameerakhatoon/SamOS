// USB mass storage: bulk-only transport (BBB, USB MSC BOT 1.0)
// carrying SCSI commands, as a struct disk driver.
//
// Every command is three transfers: a 31-byte CBW on the bulk OUT
// endpoint, the data (up to MSC_DATA_PAGES pages, IN or OUT), a
// 13-byte CSW on the bulk IN endpoint. A STALL on the data stage is
// cleared and the CSW still read; a STALL on the CSW is cleared and
// the CSW read once more (BOT 6.7). A bad CSW, a phase error or a
// transfer that times out gets the reset recovery of BOT 5.3.4 (Bulk-
// Only Mass Storage Reset, then both endpoints reset); if that fails
// too the disk is marked broken and every later request fails.
//
// SCSI: TEST UNIT READY (with REQUEST SENSE, until the unit attention
// of a fresh device is gone), INQUIRY, READ CAPACITY(10) or (16),
// MODE SENSE(6) for write protection, READ/WRITE(10), or (16) above
// 2^32 blocks, SYNCHRONIZE CACHE after writes while the device takes
// it. LUN 0 only; 512-byte blocks only (others are logged and left).
//
// The disk layer calls under disk_lock, so a device has one
// command in flight and no lock of its own. Transfers wait on the
// xHCI completion line through disk_irq_wait (usb_transfer_wait with
// irq set), else poll. Devices found at boot become disks in the
// driver's mount, after the native and virtio disks so their ids do
// not move; one plugged in later is only logged (no hot-plug mount).
// An unplugged disk stays in the table and fails every request.
//
// Writes: the stick is the point of USB storage (a file written on the
// machine reads back elsewhere), so a USB disk is writable on bare
// metal too, unlike internal disks (storage_policy.h), unless the
// device reports write protection or SAMOS_STORAGE_FORCE_READ_ONLY.

#include "usb_msc.h"
#include "usb/usb.h"
#include "arch/arch.h"
#include "disk/disk.h"
#include "disk/storage_policy.h"
#include "io/serial.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "status.h"

#define MSC_MAX_DEVICES      8
#define MSC_DATA_PAGES       16                 // 64 KiB per command
#define MSC_SECTOR_SIZE      512
#define MSC_SECTORS_PER_CMD  (MSC_DATA_PAGES * 4096 / MSC_SECTOR_SIZE)
#define MSC_TIMEOUT_MS       5000
#define MSC_READY_TRIES      20

#define MSC_REQ_RESET        0xFF
#define MSC_CBW_SIG          0x43425355
#define MSC_CSW_SIG          0x53425355
#define MSC_CSW_PASSED       0
#define MSC_CSW_FAILED       1

struct msc_cbw {
    uint32_t sig, tag, len;
    uint8_t  flags, lun, cb_len, cb[16];
} __attribute__((packed));

struct msc_csw {
    uint32_t sig, tag, residue;
    uint8_t  status;
} __attribute__((packed));

struct usb_msc {
    struct usb_device*   dev;
    struct usb_endpoint* in;
    struct usb_endpoint* out;
    uint8_t              iface;
    uint32_t             tag;
    void*                cbw;                   // one page each
    void*                csw;
    void*                data[MSC_DATA_PAGES];
    uint64_t             blocks;
    struct disk*         disk;
    bool                 broken;
    bool                 no_sync;               // SYNCHRONIZE CACHE refused once
};

static struct usb_msc* msc_devices[MSC_MAX_DEVICES];
static int msc_count;
static bool msc_mounted;

static void put_be16(uint8_t* p, uint16_t v){ p[0] = v >> 8; p[1] = v; }
static void put_be32(uint8_t* p, uint32_t v){ put_be16(p, v >> 16); put_be16(p + 2, v); }
static void put_be64(uint8_t* p, uint64_t v){ put_be32(p, v >> 32); put_be32(p + 4, v); }
static uint32_t get_be32(const uint8_t* p){ return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t get_be64(const uint8_t* p){ return (uint64_t)get_be32(p) << 32 | get_be32(p + 4); }

// One bulk transfer, waited for at the interrupt when there is one.
static int msc_xfer(struct usb_msc* m, struct usb_endpoint* ep, void** pages, uint32_t len, bool* stalled){
    usb_irq_arm(m->dev);
    int res = usb_transfer_start(ep, pages, len);
    if(res < 0){
        return res;
    }
    return usb_transfer_wait(ep, true, MSC_TIMEOUT_MS, stalled);
}

// BOT 5.3.4: Bulk-Only Mass Storage Reset, then both endpoints.
static void msc_reset_recovery(struct usb_msc* m){
    if(m->dev->gone ||
       usb_control(m->dev, USB_RT_CLASS | USB_RT_INTERFACE, MSC_REQ_RESET, 0, m->iface, NULL, 0) < 0 ||
       usb_endpoint_clear_halt(m->in) < 0 || usb_endpoint_clear_halt(m->out) < 0){
        if(!m->broken){
            serial_write("usb-msc: reset recovery failed, disk disabled\n");
        }
        m->broken = true;
    }
}

// One SCSI command: `len` bytes of m->data in or out. 0; -EIO when the
// device failed it (CSW status 1) or the transport did.
static int msc_command(struct usb_msc* m, const uint8_t* cdb, uint8_t cdb_len, uint32_t len, bool in){
    if(m->broken || m->dev->gone){
        return -EIO;
    }
    struct msc_cbw* w = m->cbw;
    memset(w, 0, sizeof(*w));
    w->sig    = MSC_CBW_SIG;
    w->tag    = ++m->tag;
    w->len    = len;
    w->flags  = in ? 0x80 : 0;
    w->cb_len = cdb_len;
    memcpy(w->cb, (void*)cdb, cdb_len);
    bool stalled = false;
    if(msc_xfer(m, m->out, &m->cbw, sizeof(*w), &stalled) != (int)sizeof(*w)){
        msc_reset_recovery(m);
        return -EIO;
    }
    if(len){
        struct usb_endpoint* ep = in ? m->in : m->out;
        int res = msc_xfer(m, ep, m->data, len, &stalled);
        if(stalled){
            usb_endpoint_clear_halt(ep);
        } else if(res < 0){
            msc_reset_recovery(m);
            return -EIO;
        }
    }
    struct msc_csw* s = m->csw;
    int res = -EIO;
    for(int tries = 0; tries < 2; tries++){
        res = msc_xfer(m, m->in, &m->csw, sizeof(*s), &stalled);
        if(!stalled){
            break;
        }
        usb_endpoint_clear_halt(m->in);
    }
    if(res != (int)sizeof(*s) || s->sig != MSC_CSW_SIG || s->tag != w->tag || s->status > MSC_CSW_FAILED){
        msc_reset_recovery(m);
        return -EIO;
    }
    return s->status == MSC_CSW_PASSED ? 0 : -EIO;
}

// REQUEST SENSE: the sense key, or < 0.
static int msc_sense(struct usb_msc* m){
    uint8_t cdb[6] = { 0x03, 0, 0, 0, 18, 0 };
    if(msc_command(m, cdb, 6, 18, true) < 0){
        return -EIO;
    }
    return ((uint8_t*)m->data[0])[2] & 0xF;
}

static int msc_ready(struct usb_msc* m){
    uint8_t cdb[6] = {0};
    for(int i = 0; i < MSC_READY_TRIES; i++){
        if(msc_command(m, cdb, 6, 0, false) == 0){
            return 0;
        }
        if(m->broken || msc_sense(m) < 0){
            return -EIO;
        }
        udelay(100000);
    }
    return -ETIMEOUT;
}

static int msc_rw(struct usb_msc* m, uint64_t lba, uint32_t count, bool write){
    uint8_t cdb[16] = {0};
    uint8_t len = 10;
    if(lba + count > 0xFFFFFFFFull){
        cdb[0] = write ? 0x8A : 0x88;               // WRITE(16), READ(16)
        put_be64(cdb + 2, lba);
        put_be32(cdb + 10, count);
        len = 16;
    } else {
        cdb[0] = write ? 0x2A : 0x28;               // WRITE(10), READ(10)
        put_be32(cdb + 2, (uint32_t)lba);
        put_be16(cdb + 7, (uint16_t)count);
    }
    int res = msc_command(m, cdb, len, count * MSC_SECTOR_SIZE, !write);
    if(res < 0 && !m->broken){
        msc_sense(m);                               // clear the condition
    }
    return res;
}

static void msc_copy(struct usb_msc* m, uint8_t* ptr, uint32_t bytes, bool to_device){
    for(uint32_t done = 0; done < bytes; done += 4096){
        uint32_t chunk = bytes - done > 4096 ? 4096 : bytes - done;
        if(to_device){
            memcpy(m->data[done / 4096], ptr + done, chunk);
        } else {
            memcpy(ptr + done, m->data[done / 4096], chunk);
        }
    }
}

static int msc_io(struct disk* disk, uint64_t lba, int total, void* buf, bool write){
    struct usb_msc* m = disk_private_data_driver(disk);
    if(!m || m->broken || m->dev->gone || total <= 0 ||
       lba >= m->blocks || (uint64_t)total > m->blocks - lba){
        return -EIO;
    }
    uint8_t* ptr = buf;
    while(total > 0){
        int count = total > MSC_SECTORS_PER_CMD ? MSC_SECTORS_PER_CMD : total;
        uint32_t bytes = (uint32_t)count * MSC_SECTOR_SIZE;
        if(write){
            msc_copy(m, ptr, bytes, true);
        }
        int res = msc_rw(m, lba, count, write);
        if(res < 0){
            return res;
        }
        if(!write){
            msc_copy(m, ptr, bytes, false);
        }
        lba   += count;
        ptr   += bytes;
        total -= count;
    }
    if(write && !m->no_sync){
        uint8_t cdb[10] = { 0x35 };                 // SYNCHRONIZE CACHE(10), everything
        if(msc_command(m, cdb, 10, 0, false) < 0 && !m->broken){
            msc_sense(m);
            m->no_sync = true;
        }
    }
    return m->broken ? -EIO : 0;
}

static int msc_read(struct disk* disk, uint64_t lba, int total, void* buf){
    return msc_io(disk, lba, total, buf, false);
}

static int msc_write(struct disk* disk, uint64_t lba, int total, void* buf){
    return msc_io(disk, lba, total, buf, true);
}

static void msc_write_text(const uint8_t* p, int n){
    char s[17];
    int len = 0;
    for(int i = 0; i < n && i < 16; i++){
        s[len++] = p[i] >= 0x20 && p[i] < 0x7F ? (char)p[i] : '?';
    }
    while(len && s[len - 1] == ' '){
        len--;
    }
    s[len] = 0;
    serial_write(s);
}

// Bring the device up and make it a disk.
static int msc_attach(struct usb_msc* m, struct disk_driver* driver){
    uint8_t* d = m->data[0];
    uint8_t inquiry[6] = { 0x12, 0, 0, 0, 36, 0 };
    if(msc_command(m, inquiry, 6, 36, true) < 0){
        return -EIO;
    }
    serial_write("usb-msc: slot ");
    serial_write(itoa(m->dev->slot));
    serial_write(", ");
    msc_write_text(d + 8, 8);
    serial_write(" ");
    msc_write_text(d + 16, 16);
    if((d[0] & 0x1F) != 0){
        serial_write(", not a direct-access device\n");
        return -ENOENT;
    }
    if(msc_ready(m) < 0){
        serial_write(", not ready\n");
        return -EIO;
    }
    uint8_t cap10[10] = { 0x25 };
    if(msc_command(m, cap10, 10, 8, true) < 0){
        serial_write(", READ CAPACITY failed\n");
        return -EIO;
    }
    uint64_t last = get_be32(d);
    uint32_t block = get_be32(d + 4);
    if(last == 0xFFFFFFFF){
        uint8_t cap16[16] = { 0x9E, 0x10 };
        put_be32(cap16 + 10, 32);
        if(msc_command(m, cap16, 16, 32, true) < 0){
            serial_write(", READ CAPACITY(16) failed\n");
            return -EIO;
        }
        last = get_be64(d);
        block = get_be32(d + 8);
    }
    if(block != MSC_SECTOR_SIZE){
        serial_write(", ");
        serial_write(itoa((int)block));
        serial_write("-byte blocks, not supported\n");
        return -EIO;
    }
    m->blocks = last + 1;
    bool wp = false;
    uint8_t sense6[6] = { 0x1A, 0, 0x3F, 0, 4, 0 };
    if(msc_command(m, sense6, 6, 4, true) == 0){
        wp = d[2] & 0x80;
    } else if(!m->broken){
        msc_sense(m);
    }
    int res = disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, m->blocks, MSC_SECTOR_SIZE, m, &m->disk);
    if(res < 0){
        serial_write(", no disk\n");
        return res;
    }
    m->disk->read_only = wp || SAMOS_STORAGE_FORCE_READ_ONLY;
    serial_write(", disk ");
    serial_write(itoa(m->disk->id));
    serial_write(", ");
    serial_write(itoa((int)(m->blocks / 2048)));
    serial_write(" MiB");
    serial_write(wp ? ", write-protected\n" : "\n");
    return 0;
}

int usb_msc_probe(struct usb_device* dev, const struct usb_interface_descriptor* intf){
    // Class 8 mass storage, subclass 6 SCSI transparent, protocol 0x50 bulk-only.
    if(intf->bInterfaceClass != 8 || intf->bInterfaceSubClass != 6 || intf->bInterfaceProtocol != 0x50){
        return -ENOENT;
    }
    if(msc_count == MSC_MAX_DEVICES){
        return -ENOSPC;
    }
    const struct usb_endpoint_descriptor* ein = NULL;
    const struct usb_endpoint_descriptor* eout = NULL;
    for(const uint8_t* p = usb_config_next(dev, (const uint8_t*)intf, USB_DT_ENDPOINT, true); p;
        p = usb_config_next(dev, p, USB_DT_ENDPOINT, true)){
        const struct usb_endpoint_descriptor* e = (const void*)p;
        if((e->bmAttributes & 3) == USB_EP_BULK){
            if(e->bEndpointAddress & USB_EP_IN){
                ein = ein ? ein : e;
            } else {
                eout = eout ? eout : e;
            }
        }
    }
    if(!ein || !eout){
        return -ENOENT;
    }
    struct usb_msc* m = kzalloc(sizeof(*m));
    if(!m){
        return -ENOMEM;
    }
    m->dev   = dev;
    m->iface = intf->bInterfaceNumber;
    m->cbw   = kpage_zalloc(4096);
    m->csw   = kpage_zalloc(4096);
    bool ok = m->cbw && m->csw;
    for(int i = 0; i < MSC_DATA_PAGES; i++){
        m->data[i] = kpage_zalloc(4096);
        ok = ok && m->data[i];
    }
    if(ok){
        m->in  = usb_endpoint_open(dev, ein);
        m->out = usb_endpoint_open(dev, eout);
    }
    if(!ok || !m->in || !m->out){
        serial_write("usb-msc: endpoints not set up\n");
        return -EIO;                    // the pages leak: rare
    }
    msc_devices[msc_count++] = m;
    dev->driver = "usb-msc";
    dev->driver_data = m;
    serial_write("usb-msc: slot ");
    serial_write(itoa(dev->slot));
    serial_write(", bulk-only, endpoints ");
    serial_write(itoa(ein->bEndpointAddress & 0xF));
    serial_write(" IN ");
    serial_write(itoa(eout->bEndpointAddress & 0xF));
    serial_write(" OUT, ");
    serial_write(itoa(ein->wMaxPacketSize & 0x7FF));
    serial_write(msc_mounted ? "-byte packets, plugged in after boot: not mounted\n" : "-byte packets\n");
    return 0;
}

void usb_msc_disconnect(struct usb_device* dev){
    for(int i = 0; i < msc_count; i++){
        if(msc_devices[i]->dev == dev){
            msc_devices[i]->broken = true;
            if(msc_devices[i]->disk){
                serial_write("usb-msc: disk ");
                serial_write(itoa(msc_devices[i]->disk->id));
                serial_write(" gone, its requests fail from now on\n");
            }
        }
    }
}

static int msc_mount(struct disk_driver* driver){
    usb_init();
    int found = 0;
    for(int i = 0; i < msc_count; i++){
        if(!msc_devices[i]->disk && !msc_devices[i]->broken && msc_attach(msc_devices[i], driver) == 0){
            found++;
        }
    }
    msc_mounted = true;
    return found ? 0 : -ENOENT;
}

static void msc_unmount(struct disk* disk){
}

static struct disk_driver usb_msc_driver = {
    .name = {"USB-MSC"},
    .functions.read    = msc_read,
    .functions.write   = msc_write,
    .functions.mount   = msc_mount,
    .functions.unmount = msc_unmount,
};

struct disk_driver* usb_msc_driver_init(void){
    return &usb_msc_driver;
}
