// Synopsys DesignWare Mobile Storage Host (dw_mmc), the JH7110's
// SD and eMMC controller, from the public descriptions of the block
// (Linux's drivers/mmc/host/dw_mmc.[ch], the SD Physical Layer
// Simplified Specification, JEDEC eMMC): one card per controller.
//
// Controller: reset (controller, FIFO, DMA), interrupts masked (the
// driver polls RINTSTS), FIFO thresholds at half the FIFO, card power,
// the card clock through CLKDIV (an update-clock command after each
// change). The clock feeding the block (ciu) is set up by U-Boot and
// not known without a clock driver: the device tree's clock-frequency
// if it has one, else assumed to be DW_MMC_CIU_GUESS_HZ, high enough
// that the dividers give at most the asked-for rate (a slower clock is
// still in spec).
//
// Card (SD 2.0 / eMMC): CMD0, CMD8 (2.7-3.6 V, check pattern), ACMD41
// until ready (HCS: SDHC/SDXC say block addressing in CCS); no answer
// to CMD55 means MMC: CMD1 until ready (sector mode in the OCR). CMD2,
// CMD3 (the SD card picks its RCA, an MMC is given 1), CMD9 (the CSD:
// capacity; eMMC above 2 GiB from EXT_CSD SEC_COUNT), CMD7, the bus
// width (ACMD6, or SWITCH to EXT_CSD BUS_WIDTH), CMD16 for byte-
// addressed cards, then 25 MHz (default speed; no high speed, no UHS).
//
// Data: CMD17/18 and CMD24/25, the controller sending CMD12 itself
// after a multiple-block transfer (SEND_STOP). By the internal DMA
// (IDMAC, when HCON says it is there: chained descriptors, one per
// 4 KiB bounce page, 32- or 64-bit descriptors as HCON's ADDR_CONFIG
// says) with arch_dma_* around the descriptors and the pages; else by
// the FIFO (PIO), which also covers a 32-bit IDMAC on memory above
// 4 GiB. A failed transfer resets the FIFO and DMA and sends CMD12.
//
// DW_MMC_HOST_TEST (tests64/host/dw_mmc_test.c) builds this file
// against a register model: the accessors, DMA pages and time below
// become the test's.

#include "dw_mmc.h"
#include "status.h"

#ifdef DW_MMC_HOST_TEST
#include <stddef.h>
#include <string.h>
uint32_t mmio_read32(volatile void* a);
void     mmio_write32(volatile void* a, uint32_t v);
uint64_t mmio_read64(volatile void* a);
void     mmio_write64(volatile void* a, uint64_t v);
void     arch_dma_clean(const volatile void* va, size_t size);
void     arch_dma_inval(const volatile void* va, size_t size);
void     arch_dma_flush(const volatile void* va, size_t size);
void*    dw_dma_page(void);
uint64_t dw_dma_phys(void* p);
uint64_t dw_time_us(void);
void     dw_log(const char* s);
void     dw_log_num(uint64_t v);
#define  dw_delay_us(us) ((void)(us))
#else
#include "arch/arch.h"
#include "disk/disk.h"
#include "disk/driver.h"
#include "disk/storage_policy.h"
#include "io/serial.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"

static void* dw_dma_page(void){
    void* p = kpage_zalloc(4096);
    if(p){
        arch_dma_clean(p, 4096);
    }
    return p;
}
static uint64_t dw_dma_phys(void* p){ return paging_kernel_virt_to_phys(p); }
static uint64_t dw_time_us(void){ return arch_time_us(); }
static void dw_log(const char* s){ serial_write(s); }
static void dw_log_num(uint64_t v){ serial_write(itoa((int)v)); }
#define dw_delay_us(us) udelay(us)
#endif

#define DW_CIU_GUESS_HZ     200000000u
#define DW_INIT_HZ          400000u
#define DW_DATA_HZ          25000000u

#define DW_CTRL         0x000
#define DW_PWREN        0x004
#define DW_CLKDIV       0x008
#define DW_CLKSRC       0x00C
#define DW_CLKENA       0x010
#define DW_TMOUT        0x014
#define DW_CTYPE        0x018
#define DW_BLKSIZ       0x01C
#define DW_BYTCNT       0x020
#define DW_INTMASK      0x024
#define DW_CMDARG       0x028
#define DW_CMD          0x02C
#define DW_RESP(n)      (0x030 + 4 * (n))
#define DW_RINTSTS      0x044
#define DW_STATUS       0x048
#define DW_FIFOTH       0x04C
#define DW_VERID        0x06C
#define DW_HCON         0x070
#define DW_BMOD         0x080
#define DW_PLDMND       0x084
#define DW_DBADDR       0x088           // DBADDRL with 64-bit descriptors
#define DW_DBADDRU      0x08C
#define DW_IDSTS32      0x08C
#define DW_IDSTS64      0x090

#define CTRL_RESET      (1u << 0)
#define CTRL_FIFO_RESET (1u << 1)
#define CTRL_DMA_RESET  (1u << 2)
#define CTRL_INT_ENABLE (1u << 4)
#define CTRL_DMA_ENABLE (1u << 5)
#define CTRL_USE_IDMAC  (1u << 25)

#define CMD_START       (1u << 31)
#define CMD_USE_HOLD    (1u << 29)
#define CMD_UPD_CLK     (1u << 21)
#define CMD_INIT        (1u << 15)
#define CMD_STOP        (1u << 14)
#define CMD_PRV_WAIT    (1u << 13)
#define CMD_SEND_STOP   (1u << 12)
#define CMD_DAT_WR      (1u << 10)
#define CMD_DAT_EXP     (1u << 9)
#define CMD_RESP_CRC    (1u << 8)
#define CMD_RESP_LONG   (1u << 7)
#define CMD_RESP_EXP    (1u << 6)

#define R_NONE          0u
#define R1              (CMD_RESP_EXP | CMD_RESP_CRC)  // also R6, R7
#define R2              (CMD_RESP_EXP | CMD_RESP_CRC | CMD_RESP_LONG)
#define R3              CMD_RESP_EXP                   // OCR: no CRC

#define INT_RESP_ERR    (1u << 1)
#define INT_CMD_DONE    (1u << 2)
#define INT_DATA_OVER   (1u << 3)
#define INT_TXDR        (1u << 4)
#define INT_RXDR        (1u << 5)
#define INT_RCRC        (1u << 6)
#define INT_DCRC        (1u << 7)
#define INT_RTO         (1u << 8)
#define INT_DRTO        (1u << 9)
#define INT_HTO         (1u << 10)
#define INT_FRUN        (1u << 11)
#define INT_HLE         (1u << 12)
#define INT_SBE         (1u << 13)
#define INT_ACD         (1u << 14)
#define INT_EBE         (1u << 15)
#define INT_DATA_ERR    (INT_DCRC | INT_DRTO | INT_HTO | INT_FRUN | INT_SBE | INT_EBE)

#define STATUS_BUSY     (1u << 9)
#define STATUS_FCNT(s)  (((s) >> 17) & 0x1FFF)

#define BMOD_SWR        (1u << 0)
#define BMOD_FB         (1u << 1)
#define BMOD_DE         (1u << 7)

#define DES0_OWN        (1u << 31)
#define DES0_CH         (1u << 4)
#define DES0_FS         (1u << 3)
#define DES0_LD         (1u << 2)
#define IDSTS_ERR       ((1u << 2) | (1u << 4) | (1u << 5))   // FBE, DU, CES

#define CMD_TIMEOUT_US  1000000
#define BUSY_TIMEOUT_US 3000000
#define DATA_TIMEOUT_US 5000000

// ---- registers and waits ------------------------------------------------

static uint32_t rd(struct dw_mmc_host* h, uint32_t off){ return mmio_read32(h->regs + off); }
static void wr(struct dw_mmc_host* h, uint32_t off, uint32_t v){ mmio_write32(h->regs + off, v); }

static bool wait_clear(struct dw_mmc_host* h, uint32_t off, uint32_t bits, uint64_t us){
    uint64_t end = dw_time_us() + us;
    while(rd(h, off) & bits){
        if(dw_time_us() > end){
            return false;
        }
    }
    return true;
}

static int ctrl_reset(struct dw_mmc_host* h, uint32_t bits){
    wr(h, DW_CTRL, rd(h, DW_CTRL) | bits);
    return wait_clear(h, DW_CTRL, bits, CMD_TIMEOUT_US) ? 0 : -ETIMEOUT;
}

// A word to CMD; the controller takes it when START drops. HLE: CMD
// was written while the last one was still being taken.
static int send_raw(struct dw_mmc_host* h, uint32_t cmd, uint32_t arg){
    wr(h, DW_CMDARG, arg);
    wr(h, DW_CMD, cmd | CMD_START | CMD_USE_HOLD);
    if(!wait_clear(h, DW_CMD, CMD_START, CMD_TIMEOUT_US)){
        return -ETIMEOUT;
    }
    if(rd(h, DW_RINTSTS) & INT_HLE){
        wr(h, DW_RINTSTS, INT_HLE);
        return -EIO;
    }
    return 0;
}

static int set_clock(struct dw_mmc_host* h, uint32_t hz){
    uint32_t ciu = h->p.ciu_hz ? h->p.ciu_hz : DW_CIU_GUESS_HZ;
    uint32_t div = hz >= ciu ? 0 : (ciu + 2 * hz - 1) / (2 * hz);
    if(div > 255){
        div = 255;
    }
    wr(h, DW_CLKENA, 0);
    wr(h, DW_CLKSRC, 0);
    int res = send_raw(h, CMD_UPD_CLK | CMD_PRV_WAIT, 0);
    wr(h, DW_CLKDIV, div);
    res = res ? res : send_raw(h, CMD_UPD_CLK | CMD_PRV_WAIT, 0);
    wr(h, DW_CLKENA, 1);
    res = res ? res : send_raw(h, CMD_UPD_CLK | CMD_PRV_WAIT, 0);
    h->clock_hz = div ? ciu / (2 * div) : ciu;
    return res;
}

// One command and its response (resp[0] is bits 31:0; a long one
// fills all four). -ETIMEOUT when the card did not answer.
static int command(struct dw_mmc_host* h, uint32_t idx, uint32_t arg, uint32_t flags, uint32_t* resp){
    wr(h, DW_RINTSTS, 0xFFFFFFFFu);
    int res = send_raw(h, idx | flags, arg);
    if(res < 0){
        return res;
    }
    uint64_t end = dw_time_us() + CMD_TIMEOUT_US;
    uint32_t s;
    while(!((s = rd(h, DW_RINTSTS)) & INT_CMD_DONE)){
        if(dw_time_us() > end){
            return -ETIMEOUT;
        }
    }
    wr(h, DW_RINTSTS, INT_CMD_DONE | INT_RTO | INT_RCRC | INT_RESP_ERR);
    if(s & INT_RTO){
        return -ETIMEOUT;
    }
    if(s & (INT_RESP_ERR | ((flags & CMD_RESP_CRC) ? INT_RCRC : 0))){
        return -EIO;
    }
    if(resp){
        for(int i = 0; i < ((flags & CMD_RESP_LONG) ? 4 : 1); i++){
            resp[i] = rd(h, DW_RESP(i));
        }
    }
    return 0;
}

static int app_command(struct dw_mmc_host* h, uint32_t idx, uint32_t arg, uint32_t flags, uint32_t* resp){
    uint32_t r[4];
    int res = command(h, 55, h->rca << 16, R1, r);
    return res < 0 ? res : command(h, idx, arg, flags, resp);
}

// The card holds DAT0 low while busy (R1b, writes).
static int wait_busy(struct dw_mmc_host* h){
    return wait_clear(h, DW_STATUS, STATUS_BUSY, BUSY_TIMEOUT_US) ? 0 : -ETIMEOUT;
}

// Bits [start, start + len) of a 128-bit register (CSD, CID).
static uint32_t bits(const uint32_t* r, int start, int len){
    uint64_t v = r[start / 32];
    if(start / 32 < 3){
        v |= (uint64_t)r[start / 32 + 1] << 32;
    }
    return (uint32_t)(v >> (start % 32)) & (len == 32 ? 0xFFFFFFFFu : (1u << len) - 1);
}

// ---- data -----------------------------------------------------------------

// IDMAC descriptors for `bytes` over the bounce pages, chained.
static void idmac_setup(struct dw_mmc_host* h, uint32_t bytes){
    uint32_t* d = h->desc;
    uint64_t dphys = dw_dma_phys(h->desc);
    int n = (int)((bytes + 4095) / 4096);
    for(int i = 0; i < n; i++){
        uint32_t chunk = bytes - (uint32_t)i * 4096 > 4096 ? 4096 : bytes - (uint32_t)i * 4096;
        uint32_t flags = DES0_OWN | DES0_CH | (i == 0 ? DES0_FS : 0) | (i == n - 1 ? DES0_LD : 0);
        uint64_t buf = dw_dma_phys(h->bounce[i]);
        if(h->idmac64){
            uint64_t next = dphys + (uint64_t)(i + 1) * 32;
            uint32_t* e = d + 8 * i;
            e[0] = flags;
            e[1] = 0;
            e[2] = chunk;
            e[3] = 0;
            e[4] = (uint32_t)buf;
            e[5] = (uint32_t)(buf >> 32);
            e[6] = (uint32_t)next;
            e[7] = (uint32_t)(next >> 32);
        } else {
            uint32_t* e = d + 4 * i;
            e[0] = flags;
            e[1] = chunk;
            e[2] = (uint32_t)buf;
            e[3] = (uint32_t)(dphys + (uint64_t)(i + 1) * 16);
        }
    }
    arch_dma_clean(h->desc, (size_t)n * (h->idmac64 ? 32 : 16));
    wr(h, DW_CTRL, rd(h, DW_CTRL) | CTRL_DMA_ENABLE | CTRL_USE_IDMAC);
    wr(h, DW_DBADDR, (uint32_t)dphys);
    if(h->idmac64){
        wr(h, DW_DBADDRU, (uint32_t)(dphys >> 32));
    }
    wr(h, h->idmac64 ? DW_IDSTS64 : DW_IDSTS32, 0xFFFFFFFFu);
    wr(h, DW_BMOD, rd(h, DW_BMOD) | BMOD_DE | BMOD_FB);
    wr(h, DW_PLDMND, 1);
}

static void idmac_stop(struct dw_mmc_host* h){
    wr(h, DW_CTRL, rd(h, DW_CTRL) & ~(CTRL_DMA_ENABLE | CTRL_USE_IDMAC));
    wr(h, DW_BMOD, rd(h, DW_BMOD) & ~(BMOD_DE | BMOD_FB));
}

// FIFO words while the transfer runs: in when RXDR (or at the end),
// out when TXDR. `*done` counts bytes moved.
static void pio_step(struct dw_mmc_host* h, uint32_t s, uint8_t* buf, uint32_t bytes, uint32_t* done, bool write){
    volatile uint8_t* fifo = h->regs + h->data_off;
    uint32_t w = h->data_width;
    if(!write && (s & (INT_RXDR | INT_DATA_OVER))){
        for(uint32_t n = STATUS_FCNT(rd(h, DW_STATUS)); n && *done < bytes; n--, *done += w){
            uint64_t v = w == 8 ? mmio_read64(fifo) : mmio_read32(fifo);
            memcpy(buf + *done, &v, w);
        }
        wr(h, DW_RINTSTS, INT_RXDR);
    }
    if(write && (s & INT_TXDR)){
        for(uint32_t n = h->p.fifo_depth - STATUS_FCNT(rd(h, DW_STATUS)); n && *done < bytes; n--, *done += w){
            uint64_t v = 0;
            memcpy(&v, buf + *done, w);
            if(w == 8){
                mmio_write64(fifo, v);
            } else {
                mmio_write32(fifo, (uint32_t)v);
            }
        }
        wr(h, DW_RINTSTS, INT_TXDR);
    }
}

// The controller sent CMD12 itself after a multiple-block transfer
// (SEND_STOP): auto command done.
static bool wait_auto_stop(struct dw_mmc_host* h){
    uint64_t end = dw_time_us() + CMD_TIMEOUT_US;
    while(!(rd(h, DW_RINTSTS) & INT_ACD)){
        if(dw_time_us() > end){
            return false;
        }
    }
    return true;
}

// One data command over `blocks` sectors: the bounce pages with IDMAC
// (the caller fills or empties them), `buf` with PIO.
static int transfer(struct dw_mmc_host* h, uint32_t op, uint32_t arg, uint32_t blocks, uint8_t* buf, bool write){
    uint32_t bytes = blocks * 512;
    if(wait_busy(h) < 0 || ctrl_reset(h, CTRL_FIFO_RESET) < 0){
        return -ETIMEOUT;
    }
    wr(h, DW_RINTSTS, 0xFFFFFFFFu);
    wr(h, DW_BLKSIZ, 512);
    wr(h, DW_BYTCNT, bytes);
    if(h->idmac){
        idmac_setup(h, bytes);
    }
    bool multi = op == 18 || op == 25;
    uint32_t r[4];
    int res = command(h, op, arg, R1 | CMD_DAT_EXP | CMD_PRV_WAIT | (write ? CMD_DAT_WR : 0) |
                                  (multi ? CMD_SEND_STOP : 0), r);
    uint32_t done = 0, s = 0;
    uint64_t end = dw_time_us() + DATA_TIMEOUT_US;
    while(res == 0){
        s = rd(h, DW_RINTSTS);
        if(s & INT_DATA_ERR){
            res = -EIO;
        } else if(!h->idmac){
            pio_step(h, s, buf, bytes, &done, write);
        }
        if(res == 0 && (s & INT_DATA_OVER)){
            break;
        }
        if(res == 0 && dw_time_us() > end){
            res = -ETIMEOUT;
        }
    }
    if(res == 0 && !h->idmac && done != bytes){
        res = -EIO;
    }
    if(res == 0 && multi && !wait_auto_stop(h)){
        res = -ETIMEOUT;
    }
    if(h->idmac){
        uint32_t ids = rd(h, h->idmac64 ? DW_IDSTS64 : DW_IDSTS32);
        if(res == 0 && (ids & IDSTS_ERR)){
            res = -EIO;
        }
        wr(h, h->idmac64 ? DW_IDSTS64 : DW_IDSTS32, ids);
        idmac_stop(h);
    }
    if(res == 0 && write){
        res = wait_busy(h);
    }
    if(res < 0){
        dw_log("dw-mmc: CMD");
        dw_log_num(op);
        dw_log(" failed, RINTSTS 0x");
        char hex[9];
        for(int i = 0; i < 8; i++){
            hex[i] = "0123456789abcdef"[(s >> (28 - 4 * i)) & 0xF];
        }
        hex[8] = 0;
        dw_log(hex);
        dw_log("\n");
        ctrl_reset(h, CTRL_FIFO_RESET | CTRL_DMA_RESET);
        if(h->idmac){
            wr(h, DW_BMOD, BMOD_SWR);
        }
        command(h, 12, 0, R1 | CMD_STOP, r);
        wait_busy(h);
    }
    return res;
}

int dw_mmc_io(struct dw_mmc_host* h, uint64_t lba, uint32_t count, void* buf, bool write){
    uint8_t* p = buf;
    if(h->broken || lba >= h->sectors || count > h->sectors - lba){
        return -EIO;
    }
    while(count){
        uint32_t n = count > DW_MMC_BOUNCE_PAGES * 8 ? DW_MMC_BOUNCE_PAGES * 8 : count;
        uint32_t bytes = n * 512;
        uint32_t arg = (uint32_t)(h->block_addr ? lba : lba * 512);
        uint32_t op = write ? (n > 1 ? 25 : 24) : (n > 1 ? 18 : 17);
        if(h->idmac){
            for(uint32_t o = 0; o < bytes; o += 4096){
                uint32_t chunk = bytes - o > 4096 ? 4096 : bytes - o;
                if(write){
                    memcpy(h->bounce[o / 4096], p + o, chunk);
                    arch_dma_clean(h->bounce[o / 4096], chunk);
                } else {
                    arch_dma_flush(h->bounce[o / 4096], chunk);
                }
            }
        }
        int res = transfer(h, op, arg, n, p, write);
        if(res < 0){
            return res;
        }
        if(h->idmac && !write){
            for(uint32_t o = 0; o < bytes; o += 4096){
                uint32_t chunk = bytes - o > 4096 ? 4096 : bytes - o;
                arch_dma_inval(h->bounce[o / 4096], chunk);
                memcpy(p + o, h->bounce[o / 4096], chunk);
            }
        }
        lba += n;
        p += bytes;
        count -= n;
    }
    return 0;
}

// ---- the card -------------------------------------------------------------

// SD: ACMD41 until the card is ready. 1 SD, 0 no SD answer (MMC?), < 0.
static int sd_ready(struct dw_mmc_host* h){
    uint32_t r[4];
    bool v2 = command(h, 8, 0x1AA, R1, r) == 0 && (r[0] & 0xFFF) == 0x1AA;
    uint64_t end = dw_time_us() + CMD_TIMEOUT_US;
    for(;;){
        int res = app_command(h, 41, (v2 ? 1u << 30 : 0) | 0x00FF8000u, R3, r);
        if(res < 0){
            return 0;
        }
        if(r[0] & (1u << 31)){
            h->block_addr = v2 && (r[0] & (1u << 30));
            return 1;
        }
        if(dw_time_us() > end){
            return -ETIMEOUT;
        }
        dw_delay_us(10000);
    }
}

static int mmc_ready(struct dw_mmc_host* h){
    uint32_t r[4];
    command(h, 0, 0, R_NONE | CMD_INIT, r);
    uint64_t end = dw_time_us() + CMD_TIMEOUT_US;
    for(;;){
        int res = command(h, 1, 0x40FF8000u, R3, r);
        if(res < 0){
            return res;
        }
        if(r[0] & (1u << 31)){
            h->block_addr = ((r[0] >> 29) & 3) == 2;
            return 1;
        }
        if(dw_time_us() > end){
            return -ETIMEOUT;
        }
        dw_delay_us(10000);
    }
}

// Sectors from the CSD (SD 1.0 / 2.0, MMC); 0 for an eMMC above 2 GiB,
// whose size is in EXT_CSD.
static uint64_t csd_sectors(struct dw_mmc_host* h){
    if(h->sd && bits(h->csd, 126, 2) == 1){
        return ((uint64_t)bits(h->csd, 48, 22) + 1) * 1024;
    }
    uint32_t c_size = bits(h->csd, 62, 12);
    if(!h->sd && c_size == 0xFFF){
        return 0;
    }
    uint64_t blocks = (uint64_t)(c_size + 1) << (bits(h->csd, 47, 3) + 2);
    return (blocks << bits(h->csd, 80, 4)) / 512;
}

static int set_width(struct dw_mmc_host* h){
    uint32_t r[4];
    uint32_t want = h->p.bus_width >= 8 && !h->sd ? 8 : h->p.bus_width >= 4 ? 4 : 1;
    int res = 0;
    if(want == 1){
        return 0;
    }
    if(h->sd){
        res = app_command(h, 6, 2, R1, r);
    } else {
        res = command(h, 6, 3u << 24 | 183u << 16 | (want == 8 ? 2u : 1u) << 8, R1, r);
        res = res ? res : wait_busy(h);
    }
    if(res == 0){
        wr(h, DW_CTYPE, want == 8 ? 1u << 16 : 1u);
        h->width = want;
    }
    return res;
}

static int card_init(struct dw_mmc_host* h){
    uint32_t r[4];
    h->rca = 0;
    command(h, 0, 0, R_NONE | CMD_INIT, r);
    int res = h->p.no_sd ? 0 : sd_ready(h);
    h->sd = res == 1;
    if(res == 0 && !h->p.no_mmc){
        res = mmc_ready(h);
    }
    if(res <= 0){
        dw_log(res == 0 || res == -ETIMEOUT ? "dw-mmc: no card answered\n" : "dw-mmc: card did not get ready\n");
        return res < 0 ? res : -EIO;
    }
    if(command(h, 2, 0, R2, h->cid) < 0){
        return -EIO;
    }
    if(h->sd){
        res = command(h, 3, 0, R1, r);
        h->rca = r[0] >> 16;
    } else {
        h->rca = 1;
        res = command(h, 3, h->rca << 16, R1, r);
    }
    if(res < 0 || command(h, 9, h->rca << 16, R2, h->csd) < 0 || command(h, 7, h->rca << 16, R1, r) < 0
       || wait_busy(h) < 0){
        return -EIO;
    }
    if(set_width(h) < 0){
        dw_log("dw-mmc: bus width not set, 1-bit\n");
    }
    h->sectors = csd_sectors(h);
    if(!h->block_addr && command(h, 16, 512, R1, r) < 0){
        return -EIO;
    }
    uint32_t hz = h->p.max_hz && h->p.max_hz < DW_DATA_HZ ? h->p.max_hz : DW_DATA_HZ;
    if(set_clock(h, hz) < 0){
        return -EIO;
    }
    if(!h->sectors){
        // EXT_CSD (512 bytes, CMD8 on an MMC): SEC_COUNT at 212.
        uint8_t ext[512];
        if(h->idmac){
            arch_dma_flush(h->bounce[0], 512);
        }
        if(transfer(h, 8, 0, 1, ext, false) < 0){
            return -EIO;
        }
        if(h->idmac){
            arch_dma_inval(h->bounce[0], 512);
            memcpy(ext, h->bounce[0], 512);
        }
        h->sectors = ext[212] | (uint32_t)ext[213] << 8 | (uint32_t)ext[214] << 16 | (uint32_t)ext[215] << 24;
    }
    return h->sectors ? 0 : -EIO;
}

// ---- the controller -------------------------------------------------------

int dw_mmc_init(struct dw_mmc_host* h){
    uint32_t ver = rd(h, DW_VERID) & 0xFFFF;
    uint32_t hcon = rd(h, DW_HCON);
    h->data_off = ver < 0x240A ? 0x100 : 0x200;
    h->data_width = ((hcon >> 7) & 7) == 2 ? 8 : 4;
    h->idmac64 = (hcon >> 27) & 1;
    h->width = 1;
    if(ctrl_reset(h, CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET) < 0){
        dw_log("dw-mmc: controller reset timed out\n");
        return -ETIMEOUT;
    }
    wr(h, DW_RINTSTS, 0xFFFFFFFFu);
    wr(h, DW_INTMASK, 0);
    wr(h, DW_CTRL, rd(h, DW_CTRL) & ~CTRL_INT_ENABLE);
    wr(h, DW_TMOUT, 0xFFFFFFFFu);
    if(!h->p.fifo_depth){
        h->p.fifo_depth = ((rd(h, DW_FIFOTH) >> 16) & 0xFFF) + 1;
    }
    wr(h, DW_FIFOTH, 2u << 28 | (h->p.fifo_depth / 2 - 1) << 16 | h->p.fifo_depth / 2);
    wr(h, DW_PWREN, 1);
    dw_delay_us(1000);
    wr(h, DW_CTYPE, 0);
    if(set_clock(h, DW_INIT_HZ) < 0){
        dw_log("dw-mmc: the card clock did not update\n");
        return -EIO;
    }

    // IDMAC when HCON's transfer mode says it is there, its
    // descriptors and pages reachable.
    h->idmac = false;
    h->pio_why = ((hcon >> 16) & 3) ? "no internal DMA" : 0;
    if(!h->pio_why){
        h->desc = dw_dma_page();
        bool low = h->desc && dw_dma_phys(h->desc) >> 32 == 0;
        for(int i = 0; i < DW_MMC_BOUNCE_PAGES && h->desc; i++){
            if(!h->bounce[i] && !(h->bounce[i] = dw_dma_page())){
                h->desc = 0;
                break;
            }
            low = low && dw_dma_phys(h->bounce[i]) >> 32 == 0;
        }
        h->pio_why = !h->desc ? "no memory for DMA" : !h->idmac64 && !low ? "32-bit IDMAC, memory above 4 GiB" : 0;
        h->idmac = !h->pio_why;
    }
    if(h->idmac){
        wr(h, DW_BMOD, BMOD_SWR);
        wait_clear(h, DW_BMOD, BMOD_SWR, CMD_TIMEOUT_US);
    }
    return card_init(h);
}

#ifndef DW_MMC_HOST_TEST
// ---- the disk driver ------------------------------------------------------

#define DW_MMC_MAX 2

static struct dw_mmc_params dw_slots[DW_MMC_MAX];
static int dw_slot_count;

void dw_mmc_attach(const struct dw_mmc_params* p){
    if(dw_slot_count < DW_MMC_MAX){
        dw_slots[dw_slot_count++] = *p;
    }
}

static int dw_read(struct disk* disk, uint64_t lba, int total, void* buf){
    return total < 0 ? -EIO : dw_mmc_io(disk_private_data_driver(disk), lba, (uint32_t)total, buf, false);
}

static int dw_write(struct disk* disk, uint64_t lba, int total, void* buf){
    return total < 0 ? -EIO : dw_mmc_io(disk_private_data_driver(disk), lba, (uint32_t)total, buf, true);
}

static void dw_name(struct dw_mmc_host* h, char* out){
    // The CID's product name: 5 characters on SD (bits 103:64), 6 on MMC.
    int n = h->sd ? 5 : 6, top = 103;
    for(int i = 0; i < n; i++, top -= 8){
        char c = (char)bits(h->cid, top - 7, 8);
        out[i] = c >= 0x20 && c < 0x7F ? c : '?';
    }
    out[n] = 0;
}

static int dw_mount(struct disk_driver* driver){
    for(int i = 0; i < dw_slot_count; i++){
        struct dw_mmc_host* h = kzalloc(sizeof(*h));
        if(!h){
            return -ENOMEM;
        }
        h->p = dw_slots[i];
        h->regs = paging_map_mmio(h->p.phys, h->p.size ? h->p.size : 0x10000, PAGING_CACHE_DISABLED);
        dw_log("dw-mmc: controller at 0x");
        char hex[17];
        for(int k = 0; k < 16; k++){
            hex[k] = "0123456789abcdef"[(h->p.phys >> (60 - 4 * k)) & 0xF];
        }
        hex[16] = 0;
        dw_log(hex);
        dw_log(h->p.ciu_hz ? "\n" : ", ciu clock not in the device tree, assumed 200 MHz\n");
        if(!h->regs || dw_mmc_init(h) < 0){
            kfree(h);                   // DMA pages leak: rare, small
            continue;
        }
        struct disk* disk = 0;
        if(disk_create_new(driver, 0, SAMOS_DISK_TYPE_REAL, 0, h->sectors, 512, h, &disk) < 0){
            continue;
        }
        // A removable SD card is the board's own boot medium (as the
        // USB stick on a PC, usb_msc.c): writable. An eMMC keeps the
        // disk layer's read-only policy for internal disks. microSD has
        // no write-protect switch, so WRTPRT is not read.
        if(h->sd && !h->p.non_removable){
            disk->read_only = SAMOS_STORAGE_FORCE_READ_ONLY;
        }
        char name[8];
        dw_name(h, name);
        dw_log("dw-mmc: disk ");
        dw_log_num(disk->id);
        dw_log(", ");
        dw_log_num(h->sectors / 2048);
        dw_log(" MiB, ");
        dw_log(!h->sd ? (h->block_addr ? "eMMC" : "MMC") : h->block_addr ? "SDHC/SDXC" : "SDSC");
        dw_log(" '");
        dw_log(name);
        dw_log("', ");
        dw_log_num(h->width);
        dw_log("-bit, ");
        dw_log_num(h->clock_hz / 1000);
        dw_log(" kHz, ");
        dw_log(h->idmac ? (h->idmac64 ? "IDMAC 64-bit" : "IDMAC 32-bit") : "PIO: ");
        dw_log(h->idmac ? "" : h->pio_why);
        dw_log(disk->read_only ? ", read-only\n" : "\n");
    }
    return 0;
}

static struct disk_driver dw_mmc_driver = {
    .name = {"DW-MMC"},
    .functions.read  = dw_read,
    .functions.write = dw_write,
    .functions.mount = dw_mount,
};

struct disk_driver* dw_mmc_driver_init(void){
    return &dw_mmc_driver;
}
#endif
