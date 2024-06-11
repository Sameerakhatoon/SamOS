// SD Host Controller Interface (SD Host Controller Simplified
// Specification 3.00) with one SD card per controller, by PIO: the SD
// slot of the Raspberry Pi 4 (EMMC2) and Pi 3 (Arasan), QEMU's raspi3b
// and sdhci-pci. The platform finds the controllers (aarch64: rpi.c from
// the device tree) and hands each to sdhci_add, the mount adds those on
// PCI (class 08/05) and sets up the card
// and makes it a REAL disk, so the MBR and FAT32 code mount its
// partitions.
//
// Every register access is 32 bits wide: the BCM2835's Arasan
// controller loses narrower writes, so the 16-bit transfer mode and
// command registers are written together (which also starts the
// command), and the 8- and 16-bit fields of 0x28 and 0x2c by a
// read-modify-write of their word.
//
// The card (SD Physical Layer Simplified Specification, 4.2): CMD0,
// CMD8 (2.7-3.6 V and the check pattern: a version 2 card), ACMD41 until
// it is ready (HCS asked after CMD8; CCS answers whether it takes block
// numbers or byte addresses), CMD2, CMD3 (its RCA), CMD9 (the CSD: the
// size), CMD7 (select), CMD16 512 on a byte-addressed card, ACMD6 for
// the 4-bit bus; 400 kHz until then, 25 MHz (default speed) after. A
// 1.8 V signalling level the firmware left in host control 2 is kept
// over the reset (the card cannot go back to 3.3 V without a power
// cycle).
//
// Transfers: CMD17/CMD24 for one block, CMD18/CMD25 with auto CMD12 for
// more, the data port read or written a block at a time as the
// buffer-ready bits say. Polled, under the disk lock; no
// interrupt is signalled. No DMA yet: SDMA/ADMA2 on the Pi need the bus
// addresses of the device tree's dma-ranges (0xc0000000 + RAM on the
// Pi 4's EMMC2 bus).
//
// The card is the Pi's boot medium, like the USB stick on a PC,
// so it is writable unless SAMOS_STORAGE_FORCE_READ_ONLY.
#include "sdhci.h"
#include "disk/disk.h"
#include "disk/driver.h"
#include "disk/storage_policy.h"
#include "arch/arch.h"
#include "io/pci.h"
#include "io/serial.h"
#include "memory/memory.h"
#include "string/string.h"
#include "status.h"

#define SDHCI_MAX           2
#define SECTOR              512
#define MAX_BLOCKS          128         // per command

#define REG_BLOCK           0x04        // block size [11:0], count [31:16]
#define REG_ARG             0x08
#define REG_CMD             0x0c        // transfer mode [15:0], command [31:16]
#define REG_RESP            0x10        // four words
#define REG_DATA            0x20
#define REG_PRESENT         0x24
#define REG_HOST            0x28        // host control 1 [7:0], power [15:8]
#define REG_CLOCK           0x2c        // clock [15:0], timeout [19:16], reset [26:24]
#define REG_INT             0x30        // normal [15:0], error [31:16]
#define REG_INT_ENABLE      0x34
#define REG_INT_SIGNAL      0x38
#define REG_HOST2           0x3c        // host control 2 [31:16]
#define REG_CAPS            0x40
#define REG_VERSION         0xfc        // host version [31:16]

#define PRESENT_CMD_INHIBIT (1u << 0)
#define PRESENT_DAT_INHIBIT (1u << 1)
#define PRESENT_CARD        (1u << 16)
#define HOST_4BIT           (1u << 1)
#define POWER_ON_33V        (0x0fu << 8)
#define HOST2_18V           (1u << 19)  // host control 2 bit 3
#define CLOCK_INT_EN        (1u << 0)
#define CLOCK_STABLE        (1u << 1)
#define CLOCK_CARD_EN       (1u << 2)
#define TIMEOUT_MAX         (0xeu << 16)
#define RESET_ALL           (1u << 24)
#define RESET_CMD           (1u << 25)
#define RESET_DAT           (1u << 26)

#define INT_CMD_DONE        (1u << 0)
#define INT_XFER_DONE       (1u << 1)
#define INT_WRITE_READY     (1u << 4)
#define INT_READ_READY      (1u << 5)
#define INT_ERRORS          0xffff8000u
#define INT_USED            0xffff003fu

// The command register's low byte: response type, checks, data.
#define RESP_NONE           0x00
#define RESP_136            0x09        // R2: CRC checked
#define RESP_48             0x1a        // R1, R6, R7: CRC and index checked
#define RESP_48_BUSY        0x1b        // R1b
#define RESP_R3             0x02        // the OCR: neither
#define CMD_DATA            0x20

#define MODE_BLKCNT         (1u << 1)
#define MODE_AUTO12         (1u << 2)
#define MODE_READ           (1u << 4)
#define MODE_MULTI          (1u << 5)

#define APP                 0x100       // an application command: CMD55 first

#define OCR_READY           (1u << 31)
#define OCR_CCS             (1u << 30)
#define OCR_VOLTAGES        0x00ff8000u // 2.7-3.6 V

struct sdhci {
    volatile uint32_t* regs;
    uint64_t           phys;
    uint32_t           base_hz;
    const char*        name;
    uint32_t           version;         // 0: 1.00, 1: 2.00, 2: 3.00
    uint32_t           rca;
    bool               block_addr;      // SDHC/SDXC: block numbers, else bytes
    uint64_t           sectors;
    uint32_t           resp[4];
    uint32_t           status;          // the interrupt status of the last failure
    struct disk*       disk;
};

static struct sdhci hosts[SDHCI_MAX];
static int host_count;

static uint32_t rd(struct sdhci* h, uint32_t off){
    return h->regs[off / 4];
}

static void wr(struct sdhci* h, uint32_t off, uint32_t v){
    h->regs[off / 4] = v;
}

int sdhci_add(void* regs, uint64_t phys, uint32_t base_clock_hz, const char* name){
    if(host_count == SDHCI_MAX){
        return -1;
    }
    struct sdhci* h = &hosts[host_count++];
    h->regs = regs;
    h->phys = phys;
    h->base_hz = base_clock_hz;
    h->name = name;
    return 0;
}

// Until (reg & mask) == 0; false after `us`.
static bool wait_clear(struct sdhci* h, uint32_t reg, uint32_t mask, uint32_t us){
    TIME_MICROSECONDS end = arch_time_us() + us;
    while(rd(h, reg) & mask){
        if(arch_time_us() > end){
            return false;
        }
    }
    return true;
}

// Until one of `bits` is set in the interrupt status (then cleared), or
// an error bit, or `us`: false then, with the status kept.
static bool wait_int(struct sdhci* h, uint32_t bits, uint32_t us){
    TIME_MICROSECONDS end = arch_time_us() + us;
    for(;;){
        uint32_t s = rd(h, REG_INT);
        if(s & INT_ERRORS){
            h->status = s;
            wr(h, REG_INT, s);
            return false;
        }
        if(s & bits){
            wr(h, REG_INT, s & bits);
            return true;
        }
        if(arch_time_us() > end){
            h->status = s;
            return false;
        }
    }
}

static bool reset(struct sdhci* h, uint32_t bits){
    wr(h, REG_CLOCK, (rd(h, REG_CLOCK) & 0x000fffffu) | bits);
    return wait_clear(h, REG_CLOCK, bits, 100000);
}

static int sd_cmd(struct sdhci* h, uint32_t cmd, uint32_t arg, uint32_t flags, uint32_t mode){
    if(cmd & APP){
        int res = sd_cmd(h, 55, h->rca << 16, RESP_48, 0);
        if(res < 0){
            return res;
        }
        cmd &= ~APP;
    }
    bool busy = (flags & 3) == 3;
    uint32_t inhibit = PRESENT_CMD_INHIBIT | (busy || (flags & CMD_DATA) ? PRESENT_DAT_INHIBIT : 0);
    if(!wait_clear(h, REG_PRESENT, inhibit, 100000)){
        h->status = rd(h, REG_PRESENT);
        reset(h, RESET_CMD | RESET_DAT);
        return -EIO;
    }
    wr(h, REG_INT, ~0u);
    wr(h, REG_ARG, arg);
    wr(h, REG_CMD, cmd << 24 | flags << 16 | mode);
    if(!wait_int(h, INT_CMD_DONE, 100000) || (busy && !wait_int(h, INT_XFER_DONE, 1000000))){
        reset(h, RESET_CMD | RESET_DAT);
        return -EIO;
    }
    for(int i = 0; i < 4; i++){
        h->resp[i] = rd(h, REG_RESP + 4 * i);
    }
    return 0;
}

// The SD clock at the highest rate up to `hz`; the rate, or 0.
static uint32_t set_clock(struct sdhci* h, uint32_t hz){
    uint32_t c = rd(h, REG_CLOCK) & 0x000f0000u;     // the timeout; reset bits stay 0
    wr(h, REG_CLOCK, c);
    uint32_t div = 0;       // SDCLK = base / (2 * div), base for 0
    if(h->base_hz > hz){
        if(h->version >= 2){
            div = (h->base_hz + 2 * hz - 1) / (2 * hz);
            div = div > 0x3ff ? 0x3ff : div;
        } else {
            for(div = 1; div < 0x80 && h->base_hz / (2 * div) > hz; div <<= 1){
            }
        }
    }
    c |= (div & 0xff) << 8 | (div >> 8 & 3) << 6 | CLOCK_INT_EN;
    wr(h, REG_CLOCK, c);
    TIME_MICROSECONDS end = arch_time_us() + 20000;
    while(!(rd(h, REG_CLOCK) & CLOCK_STABLE)){
        if(arch_time_us() > end){
            return 0;
        }
    }
    wr(h, REG_CLOCK, c | CLOCK_CARD_EN);
    return div ? h->base_hz / (2 * div) : h->base_hz;
}

// Bits [hi:lo] of a CSD or CID, whose bits [127:8] the four response
// words hold from bit 0 (the CRC byte is not kept).
static uint32_t r2_bits(const uint32_t r[4], int hi, int lo){
    uint32_t v = 0;
    for(int b = hi; b >= lo; b--){
        int i = b - 8;
        v = v << 1 | (r[i / 32] >> (i % 32) & 1);
    }
    return v;
}

static void say(struct sdhci* h, const char* what){
    serial_write("sdhci: ");
    serial_write(h->name);
    serial_write(": ");
    serial_write(what);
}

static int fail(struct sdhci* h, const char* what){
    say(h, what);
    serial_write(", status ");
    serial_write_hex(h->status);
    serial_write("\n");
    return -EIO;
}

static int host_init(struct sdhci* h){
    uint32_t keep18 = rd(h, REG_HOST2) & HOST2_18V;
    if(!reset(h, RESET_ALL)){
        h->status = rd(h, REG_CLOCK);
        return fail(h, "reset did not finish");
    }
    h->version = rd(h, REG_VERSION) >> 16 & 0xff;
    uint32_t caps = rd(h, REG_CAPS);
    uint32_t mhz = h->version >= 2 ? caps >> 8 & 0xff : caps >> 8 & 0x3f;
    if(mhz){
        h->base_hz = mhz * 1000000;
    }
    say(h, "SDHCI ");
    serial_write(itoa((int)h->version + 1));
    serial_write(".00 at ");
    serial_write_hex(h->phys);
    serial_write(", base clock ");
    serial_write(itoa((int)(h->base_hz / 1000)));
    serial_write(mhz ? " kHz (capabilities)" : " kHz (platform)");
    serial_write(rd(h, REG_PRESENT) & PRESENT_CARD ? ", card present\n" : ", no card detect\n");
    if(!h->base_hz){
        return fail(h, "no base clock");
    }
    wr(h, REG_HOST2, rd(h, REG_HOST2) | keep18);
    wr(h, REG_HOST, POWER_ON_33V);
    wr(h, REG_INT_ENABLE, INT_USED);
    wr(h, REG_INT_SIGNAL, 0);
    wr(h, REG_CLOCK, TIMEOUT_MAX);
    if(!set_clock(h, 400000)){
        return fail(h, "SD clock did not stabilise");
    }
    udelay(1000);           // at least 74 clocks before CMD0
    return 0;
}

static int card_init(struct sdhci* h){
    h->rca = 0;
    sd_cmd(h, 0, 0, RESP_NONE, 0);
    bool v2 = sd_cmd(h, 8, 0x1aa, RESP_48, 0) == 0 && (h->resp[0] & 0xfff) == 0x1aa;
    uint32_t ocr = 0;
    TIME_MICROSECONDS end = arch_time_us() + 1000000;
    do{
        if(sd_cmd(h, APP | 41, (v2 ? OCR_CCS : 0) | OCR_VOLTAGES, RESP_R3, 0) < 0){
            return fail(h, "no answer to ACMD41 (no SD card?)");
        }
        ocr = h->resp[0];
        if(!(ocr & OCR_READY)){
            udelay(10000);
        }
    } while(!(ocr & OCR_READY) && arch_time_us() < end);
    if(!(ocr & OCR_READY)){
        return fail(h, "card did not become ready (ACMD41)");
    }
    h->block_addr = v2 && (ocr & OCR_CCS);
    if(sd_cmd(h, 2, 0, RESP_136, 0) < 0 || sd_cmd(h, 3, 0, RESP_48, 0) < 0){
        return fail(h, "CMD2/CMD3 failed");
    }
    h->rca = h->resp[0] >> 16;
    if(sd_cmd(h, 9, h->rca << 16, RESP_136, 0) < 0){
        return fail(h, "CMD9 (CSD) failed");
    }
    if(r2_bits(h->resp, 127, 126) == 1){
        h->sectors = ((uint64_t)r2_bits(h->resp, 69, 48) + 1) * 1024;
    } else {
        uint64_t blocks = (uint64_t)(r2_bits(h->resp, 73, 62) + 1) << (r2_bits(h->resp, 49, 47) + 2);
        h->sectors = (blocks << r2_bits(h->resp, 83, 80)) / SECTOR;
    }
    if(sd_cmd(h, 7, h->rca << 16, RESP_48_BUSY, 0) < 0){
        return fail(h, "CMD7 (select) failed");
    }
    if(!h->block_addr && sd_cmd(h, 16, SECTOR, RESP_48, 0) < 0){
        return fail(h, "CMD16 failed");
    }
    bool wide = sd_cmd(h, APP | 6, 2, RESP_48, 0) == 0;
    if(wide){
        wr(h, REG_HOST, rd(h, REG_HOST) | HOST_4BIT);
    }
    uint32_t hz = set_clock(h, 25000000);
    if(!hz){
        return fail(h, "SD clock did not stabilise at 25 MHz");
    }
    say(h, h->block_addr ? "SDHC/SDXC card" : "SDSC card");
    serial_write(", RCA ");
    serial_write_hex(h->rca);
    serial_write(wide ? ", 4-bit bus, " : ", 1-bit bus, ");
    serial_write(itoa((int)(hz / 1000)));
    serial_write(" kHz, PIO\n");
    return 0;
}

static int transfer(struct sdhci* h, uint64_t lba, int n, uint8_t* buf, bool write){
    uint32_t arg = h->block_addr ? (uint32_t)lba : (uint32_t)(lba * SECTOR);
    uint32_t mode = MODE_BLKCNT | (write ? 0 : MODE_READ) | (n > 1 ? MODE_MULTI | MODE_AUTO12 : 0);
    uint32_t cmd = n > 1 ? (write ? 25 : 18) : (write ? 24 : 17);
    wr(h, REG_BLOCK, (uint32_t)n << 16 | SECTOR);
    if(sd_cmd(h, cmd, arg, RESP_48 | CMD_DATA, mode) < 0){
        return -EIO;
    }
    for(int b = 0; b < n; b++){
        if(!wait_int(h, write ? INT_WRITE_READY : INT_READ_READY, 1000000)){
            goto failed;
        }
        for(int i = 0; i < SECTOR; i += 4, buf += 4){
            uint32_t w;
            if(write){
                memcpy(&w, buf, 4);
                wr(h, REG_DATA, w);
            } else {
                w = rd(h, REG_DATA);
                memcpy(buf, &w, 4);
            }
        }
    }
    if(!wait_int(h, INT_XFER_DONE, 1000000)){
        goto failed;
    }
    return 0;
failed:
    reset(h, RESET_CMD | RESET_DAT);
    if(n > 1){
        sd_cmd(h, 12, 0, RESP_48_BUSY, 0);      // the card may still be in the transfer
    }
    return -EIO;
}

static int sdhci_io(struct disk* disk, uint64_t lba, int total, void* buf, bool write){
    struct sdhci* h = disk->driver_private;
    uint8_t* p = buf;
    if(lba >= h->sectors || (uint64_t)total > h->sectors - lba){
        return -EIO;
    }
    while(total > 0){
        int n = total > MAX_BLOCKS ? MAX_BLOCKS : total;
        if(transfer(h, lba, n, p, write) < 0){
            fail(h, write ? "write failed" : "read failed");
            return -EIO;
        }
        lba += n;
        p += n * SECTOR;
        total -= n;
    }
    return 0;
}

static int sdhci_read(struct disk* disk, uint64_t lba, int total, void* buf){
    return sdhci_io(disk, lba, total, buf, false);
}

static int sdhci_write(struct disk* disk, uint64_t lba, int total, void* buf){
    return sdhci_io(disk, lba, total, buf, true);
}

// SD host controllers on PCI (class 08/05: QEMU's sdhci-pci): BAR 0 is
// the slot's registers, as the firmware assigned it (x86) or the
// kernel did (aarch64 virt, once BARs are assigned there).
static void sdhci_pci_probe(void){
    size_t total = pci_device_count();
    for(size_t i = 0; i < total; i++){
        struct pci_device* dev = NULL;
        if(pci_device_get(i, &dev) < 0 || !dev || dev->class.base != 0x08 || dev->class.subclass != 0x05){
            continue;
        }
        uint64_t base = dev->bars[0].addr & ~0xFULL;
        void* regs = dev->bars[0].type == PCI_DEVICE_IO_MEMORY && base
                   ? paging_map_mmio(base, 0x100, PAGING_CACHE_DISABLED) : NULL;
        if(!regs){
            serial_write("sdhci: PCI controller without a mapped memory BAR 0\n");
            continue;
        }
        sdhci_add(regs, base, 0, "pci");
    }
}

static int sdhci_mount(struct disk_driver* driver){
    sdhci_pci_probe();
    for(int i = 0; i < host_count; i++){
        struct sdhci* h = &hosts[i];
        if(host_init(h) < 0 || card_init(h) < 0){
            continue;
        }
        if(disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, h->sectors, SECTOR, h, &h->disk) < 0){
            continue;
        }
        h->disk->read_only = SAMOS_STORAGE_FORCE_READ_ONLY;
        serial_write("sdhci: disk ");
        serial_write(itoa(h->disk->id));
        serial_write(", ");
        serial_write(itoa((int)(h->sectors / 2048)));
        serial_write(" MiB\n");
    }
    return 0;
}

static struct disk_driver sdhci_driver = {
    .name = {"SDHCI"},
    .functions.read  = sdhci_read,
    .functions.write = sdhci_write,
    .functions.mount = sdhci_mount,
};

struct disk_driver* sdhci_driver_init(void){
    return &sdhci_driver;
}
