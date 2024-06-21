// Host test of disk/drivers/dw_mmc.c (QEMU has no DesignWare
// MMC) against a register model: the controller's registers, its FIFO
// and internal DMA (chained 32- or 64-bit descriptors walked in DMA
// memory), and an SD or eMMC card answering the commands of the SD
// Physical Layer Simplified Specification and JEDEC eMMC, over a disk
// image. Checks the command sequence and its flags, the card clock at
// each step, bus width, addressing, capacity, multi-block reads and
// writes by IDMAC and by PIO (32- and 64-bit FIFO), the PIO fallback
// for a 32-bit IDMAC above 4 GiB, the cache maintenance around DMA,
// no card, and a FAT32 image read back sector for sector.
//
//   dw_mmc_test [fat32.img]
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk/drivers/dw_mmc.h"
#include "status.h"

static int checks, failures;
#define CHECK(c, ...) do { checks++; if(!(c)){ failures++; printf("FAIL line %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while(0)

enum { CARD_NONE, CARD_SDHC, CARD_SDSC, CARD_EMMC };

#define CIU_HZ      100000000u
#define FIFO_DEPTH  32
#define ARENA_PAGES 64
#define LOG_MAX     4096

// ---- the model ----------------------------------------------------------

static uint8_t window[0x1000];             // the registers' address
static uint8_t arena[ARENA_PAGES][4096] __attribute__((aligned(4096)));

static struct {
    uint32_t reg[0x100 / 4];
    uint32_t verid, hcon, data_off, fifo_w;
    uint64_t dma_base;
    int      arena_used;
    int      kind;
    uint8_t* image;
    uint64_t image_bytes;
    // the card
    bool     app, ready, selected;
    uint32_t rca, width, blocklen, busy_polls;
    uint8_t  ext_csd[512];
    // the data phase
    uint8_t  buf[65536];
    uint32_t len, pos;
    uint64_t off;
    bool     active, writing, stop;
    // what happened
    int      n;
    uint32_t op[LOG_MAX], arg[LOG_MAX], flags[LOG_MAX], hz[LOG_MAX], ctype[LOG_MAX];
    int      dma_xfers, pio_words, bad_access;
    uint64_t cleaned, flushed, invalidated;
} m;

#define R(off) m.reg[(off) / 4]

static uint8_t* arena_at(uint64_t phys){
    uint64_t o = phys - m.dma_base;
    if(phys < m.dma_base || o >= sizeof(arena)){
        m.bad_access++;
        return NULL;
    }
    return (uint8_t*)arena + o;
}

static uint32_t card_hz(void){
    uint32_t div = R(0x08) & 0xFF;
    return div ? CIU_HZ / (2 * div) : CIU_HZ;
}

static void set_long(const uint32_t* v){
    for(int i = 0; i < 4; i++){
        R(0x30 + 4 * i) = v[i];
    }
}

// Put `value` at bits [start, start + len) of a 128-bit register.
static void put_bits(uint32_t* r, int start, int len, uint32_t value){
    for(int i = 0; i < len; i++){
        int b = start + i;
        r[b / 32] = (r[b / 32] & ~(1u << (b % 32))) | ((value >> i) & 1u) << (b % 32);
    }
}

static void data_done(void){
    m.active = false;
    R(0x44) |= 1u << 3;                         // DATA_OVER
    if(m.stop){
        R(0x44) |= 1u << 14;                    // ACD: the auto CMD12
    }
}

// IDMAC: walk the chain from DBADDR, moving each buffer.
static void idmac_run(void){
    bool d64 = (m.hcon >> 27) & 1;
    uint64_t at = R(0x88) | (d64 ? (uint64_t)R(0x8C) << 32 : 0);
    uint32_t moved = 0;
    for(int guard = 0; guard < 64; guard++){
        uint32_t* d = (uint32_t*)arena_at(at);
        if(!d || !(d[0] & (1u << 31))){
            m.bad_access++;
            return;
        }
        uint32_t size = d64 ? d[2] & 0x1FFF : d[1] & 0x1FFF;
        uint64_t buf = d64 ? (d[4] | (uint64_t)d[5] << 32) : d[2];
        uint8_t* p = arena_at(buf);
        if(!p || moved + size > m.len){
            m.bad_access++;
            return;
        }
        if(m.writing){
            memcpy(m.image + m.off + moved, p, size);
        } else {
            memcpy(p, m.image + m.off + moved, size);
        }
        moved += size;
        d[0] &= ~(1u << 31);
        if(d[0] & (1u << 2)){                   // LD
            break;
        }
        at = d64 ? (d[6] | (uint64_t)d[7] << 32) : d[3];
    }
    if(moved != m.len){
        m.bad_access++;
    }
    m.dma_xfers++;
    R(d64 ? 0x90 : 0x8C) |= m.writing ? 1u : 2u;   // TI / RI
    data_done();
}

static void start_data(uint64_t addr, bool write, bool stop){
    m.len = R(0x20);
    m.off = addr;
    m.pos = 0;
    m.writing = write;
    m.stop = stop;
    m.active = true;
    if(addr + m.len > m.image_bytes || m.len > sizeof(m.buf) || (R(0x1C) != 512)){
        m.active = false;
        R(0x44) |= 1u << 9;                     // DRTO: nothing comes
        return;
    }
    if((R(0x00) & (1u << 25)) && (R(0x80) & (1u << 7))){
        idmac_run();
        return;
    }
    if(!write){
        memcpy(m.buf, m.image + addr, m.len);
    }
    R(0x44) |= write ? 1u << 4 : 1u << 5;       // TXDR / RXDR
    if(!write && m.len <= FIFO_DEPTH * m.fifo_w){
        R(0x44) |= 1u << 3;                     // all of it is in the FIFO
        if(stop){
            R(0x44) |= 1u << 14;
        }
    }
}

static void exec(uint32_t cmd){
    uint32_t op = cmd & 0x3F, arg = R(0x28);
    if(m.n < LOG_MAX){
        m.op[m.n] = op;
        m.arg[m.n] = arg;
        m.flags[m.n] = cmd;
        m.hz[m.n] = card_hz();
        m.ctype[m.n] = R(0x18);
        m.n++;
    }
    bool app = m.app;
    m.app = false;
    uint32_t resp = 0x900;                      // R1: READY_FOR_DATA, TRAN
    bool sd = m.kind == CARD_SDHC || m.kind == CARD_SDSC;
    bool answer = m.kind != CARD_NONE;
    switch(op){
        case 0:
            m.ready = m.selected = false;
            m.rca = 0;
            m.busy_polls = 3;
            break;
        case 8:
            if(m.kind == CARD_SDHC && !app){
                resp = arg & 0xFFF;
            } else if(m.kind == CARD_EMMC && m.selected){
                memcpy(m.buf, m.ext_csd, 512);
                m.len = 512;
                m.pos = 0;
                m.writing = false;
                m.stop = false;
                m.active = true;
                if((R(0x00) & (1u << 25)) && (R(0x80) & (1u << 7))){
                    // the model's IDMAC reads from the image; EXT_CSD
                    // goes through the same path from a copy
                    uint8_t* save = m.image;
                    uint64_t bytes = m.image_bytes;
                    m.image = m.ext_csd;
                    m.image_bytes = 512;
                    m.off = 0;
                    idmac_run();
                    m.image = save;
                    m.image_bytes = bytes;
                } else {
                    R(0x44) |= (1u << 5) | (1u << 3);
                }
            } else {
                answer = false;
            }
            break;
        case 55:
            answer = sd;
            m.app = sd;
            break;
        case 41:
            answer = sd && app;
            if(m.busy_polls){
                m.busy_polls--;
                resp = 0x00FF8000;
            } else {
                m.ready = true;
                resp = 0x80FF8000 | (m.kind == CARD_SDHC && (arg & (1u << 30)) ? 1u << 30 : 0);
            }
            break;
        case 1:
            answer = m.kind == CARD_EMMC;
            if(m.busy_polls){
                m.busy_polls--;
                resp = 0x00FF8080;
            } else {
                m.ready = true;
                resp = 0xC0FF8080;
            }
            break;
        case 2: {
            uint32_t cid[4] = { 0 };
            const char* name = sd ? "SAMSD" : "SAMEMC";
            for(int i = 0, top = 103; name[i]; i++, top -= 8){
                put_bits(cid, top - 7, 8, (uint8_t)name[i]);
            }
            answer = answer && m.ready;
            set_long(cid);
            break;
        }
        case 3:
            if(sd){
                m.rca = 0x1234;
                resp = m.rca << 16;
            } else {
                m.rca = arg >> 16;
            }
            break;
        case 9: {
            uint32_t csd[4] = { 0 };
            uint64_t sectors = m.image_bytes / 512;
            if(m.kind == CARD_SDHC){
                put_bits(csd, 126, 2, 1);
                put_bits(csd, 48, 22, (uint32_t)(sectors / 1024 - 1));
            } else if(m.kind == CARD_SDSC){
                // 512-byte blocks, C_SIZE_MULT 7: (C_SIZE + 1) * 512 blocks
                put_bits(csd, 80, 4, 9);
                put_bits(csd, 47, 3, 7);
                put_bits(csd, 62, 12, (uint32_t)(sectors / 512 - 1));
            } else {
                put_bits(csd, 62, 12, 0xFFF);
            }
            answer = answer && (arg >> 16) == m.rca;
            set_long(csd);
            break;
        }
        case 7:
            m.selected = (arg >> 16) == m.rca;
            answer = answer && m.selected;
            break;
        case 6:
            if(app){
                m.width = arg == 2 ? 4 : 1;
            } else if(((arg >> 16) & 0xFF) == 183){
                m.width = ((arg >> 8) & 0xFF) == 2 ? 8 : ((arg >> 8) & 0xFF) == 1 ? 4 : 1;
            }
            break;
        case 16:
            m.blocklen = arg;
            break;
        case 17: case 18: case 24: case 25: {
            bool block = m.kind != CARD_SDSC;
            uint64_t addr = block ? (uint64_t)arg * 512 : arg;
            answer = answer && m.selected;
            if(answer){
                start_data(addr, op >= 24, op == 18 || op == 25);
            }
            break;
        }
        case 12:
        case 13:
            break;
        default:
            answer = false;
    }
    if(!answer){
        R(0x44) |= (1u << 2) | (1u << 8);       // CMD_DONE + RTO
        return;
    }
    if(!(cmd & (1u << 7))){
        R(0x30) = resp;
    }
    R(0x44) |= 1u << 2;
}

static uint32_t fifo_count(void){
    if(!m.active || m.writing){
        return 0;
    }
    uint32_t left = (m.len - m.pos) / m.fifo_w;
    return left > FIFO_DEPTH ? FIFO_DEPTH : left;
}

static void fifo_read(uint8_t* out){
    if(!m.active || m.writing || m.pos + m.fifo_w > m.len){
        m.bad_access++;
        memset(out, 0, m.fifo_w);
        return;
    }
    memcpy(out, m.buf + m.pos, m.fifo_w);
    m.pos += m.fifo_w;
    m.pio_words++;
    if(m.len - m.pos <= FIFO_DEPTH * m.fifo_w){
        R(0x44) |= 1u << 3;
        if(m.stop){
            R(0x44) |= 1u << 14;
        }
    }
    if(m.pos == m.len){
        m.active = false;
    }
}

static void fifo_write(const uint8_t* in){
    if(!m.active || !m.writing || m.pos + m.fifo_w > m.len){
        m.bad_access++;
        return;
    }
    memcpy(m.image + m.off + m.pos, in, m.fifo_w);
    m.pos += m.fifo_w;
    m.pio_words++;
    if(m.pos == m.len){
        data_done();
    }
}

static uint32_t reg_off(volatile void* a){
    return (uint32_t)((volatile uint8_t*)a - window);
}

uint32_t mmio_read32(volatile void* a){
    uint32_t off = reg_off(a);
    if(off == m.data_off && m.fifo_w == 4){
        uint32_t v;
        fifo_read((uint8_t*)&v);
        return v;
    }
    switch(off){
        case 0x44:                              // RXDR / TXDR follow the FIFO
            return R(off) | (m.active && m.pos < m.len && !((R(0x00) >> 25) & 1)
                             ? (m.writing ? 1u << 4 : 1u << 5) : 0);
        case 0x48: return fifo_count() << 17;   // STATUS, never busy
        case 0x6C: return m.verid;
        case 0x70: return m.hcon;
    }
    if(off >= 0x100){
        m.bad_access++;
        return 0;
    }
    return R(off);
}

void mmio_write32(volatile void* a, uint32_t v){
    uint32_t off = reg_off(a);
    if(off == m.data_off && m.fifo_w == 4){
        fifo_write((uint8_t*)&v);
        return;
    }
    if(off >= 0x100){
        m.bad_access++;
        return;
    }
    switch(off){
        case 0x00:                              // the resets end at once
            if(v & 2){
                m.active = false;
            }
            R(off) = v & ~7u;
            return;
        case 0x2C:
            R(off) = v & ~(1u << 31);
            if((v & (1u << 31)) && !(v & (1u << 21))){
                exec(v);
            }
            return;
        case 0x44:                              // write 1 to clear
        case 0x8C:
        case 0x90:
            if(off == 0x44 || (off == 0x8C) != ((m.hcon >> 27) & 1)){
                R(off) &= ~v;
                return;
            }
            break;
        case 0x80:
            R(off) = v & ~1u;
            return;
    }
    R(off) = v;
}

uint64_t mmio_read64(volatile void* a){
    uint64_t v = 0;
    if(reg_off(a) == m.data_off && m.fifo_w == 8){
        fifo_read((uint8_t*)&v);
    } else {
        m.bad_access++;
    }
    return v;
}

void mmio_write64(volatile void* a, uint64_t v){
    if(reg_off(a) == m.data_off && m.fifo_w == 8){
        fifo_write((uint8_t*)&v);
    } else {
        m.bad_access++;
    }
}

void arch_dma_clean(const volatile void* va, size_t size){ m.cleaned += size; }
void arch_dma_inval(const volatile void* va, size_t size){ m.invalidated += size; }
void arch_dma_flush(const volatile void* va, size_t size){ m.flushed += size; }

void* dw_dma_page(void){
    if(m.arena_used == ARENA_PAGES){
        return NULL;
    }
    memset(arena[m.arena_used], 0, 4096);
    return arena[m.arena_used++];
}

uint64_t dw_dma_phys(void* p){
    return m.dma_base + (uint64_t)((uint8_t*)p - (uint8_t*)arena);
}

static uint64_t now;
uint64_t dw_time_us(void){ return now += 7; }

static char logbuf[4096];
void dw_log(const char* s){ strncat(logbuf, s, sizeof(logbuf) - strlen(logbuf) - 1); }
void dw_log_num(uint64_t v){ char b[24]; snprintf(b, sizeof b, "%llu", (unsigned long long)v); dw_log(b); }

// ---- the tests ------------------------------------------------------------

#define HCON_IDMAC64   ((1u << 27) | (1u << 7))     // 64-bit addressing, 32-bit FIFO
#define HCON_IDMAC32   (1u << 7)
#define HCON_NODMA64W  ((3u << 16) | (2u << 7))     // no DMA, 64-bit FIFO
#define VERID_290A     0x5342290Au
#define VERID_230A     0x5342230Au

static void setup(int kind, uint32_t verid, uint32_t hcon, uint64_t dma_base, uint8_t* image, uint64_t bytes){
    memset(&m, 0, sizeof(m));
    m.kind = kind;
    m.verid = verid;
    m.hcon = hcon;
    m.data_off = (verid & 0xFFFF) < 0x240A ? 0x100 : 0x200;
    m.fifo_w = ((hcon >> 7) & 7) == 2 ? 8 : 4;
    m.dma_base = dma_base;
    m.image = image;
    m.image_bytes = bytes;
    R(0x4C) = (FIFO_DEPTH - 1) << 16;           // FIFOTH's reset value: depth - 1
    uint64_t sectors = bytes / 512;
    for(int i = 0; i < 4; i++){
        m.ext_csd[212 + i] = (uint8_t)(sectors >> (8 * i));
    }
    logbuf[0] = 0;
}

static struct dw_mmc_host host(uint32_t bus_width, bool emmc){
    struct dw_mmc_host h;
    memset(&h, 0, sizeof(h));
    h.regs = window;
    h.p.bus_width = bus_width;
    h.p.ciu_hz = CIU_HZ;
    h.p.max_hz = 100000000;
    h.p.non_removable = emmc;
    return h;
}

// The command opcodes in order, from `from`, as a string "0 8 55 41".
static const char* ops(int from, int to){
    static char s[1024];
    s[0] = 0;
    for(int i = from; i < to && i < m.n; i++){
        snprintf(s + strlen(s), sizeof s - strlen(s), i > from ? " %u" : "%u", m.op[i]);
    }
    return s;
}

static int find_op(uint32_t op, int from){
    for(int i = from; i < m.n; i++){
        if(m.op[i] == op){
            return i;
        }
    }
    return -1;
}

static uint8_t* pattern(uint64_t bytes, unsigned seed){
    uint8_t* p = malloc(bytes);
    for(uint64_t i = 0; i < bytes; i++){
        p[i] = (uint8_t)(i * 31 + seed + (i >> 9));
    }
    return p;
}

// Reads and writes over a card set up in `h`: one sector, 200 (two
// commands: 128 + 72), a write of 129 (128 + 1) that the image then holds, a
// read past the end refused.
static void io_checks(struct dw_mmc_host* h, const char* what){
    uint64_t bytes = m.image_bytes;
    uint8_t* buf = malloc(200 * 512);
    int before = m.n;
    CHECK(dw_mmc_io(h, 5, 1, buf, false) == 0 && memcmp(buf, m.image + 5 * 512, 512) == 0,
          "%s: one sector read", what);
    CHECK(m.op[m.n - 1] == 17 && !(m.flags[m.n - 1] & (1u << 12)), "%s: CMD17 without auto stop", what);
    before = m.n;
    CHECK(dw_mmc_io(h, 1000, 200, buf, false) == 0 && memcmp(buf, m.image + 1000 * 512, 200 * 512) == 0,
          "%s: 200 sectors read", what);
    CHECK(!strcmp(ops(before, m.n), "18 18"), "%s: two CMD18s, got %s", what, ops(before, m.n));
    bool stop = true;
    for(int i = before; i < m.n; i++){
        stop = stop && (m.flags[i] & (1u << 12)) && (m.flags[i] & (1u << 9)) && !(m.flags[i] & (1u << 10));
    }
    CHECK(stop, "%s: CMD18 with data expected, read, auto stop", what);
    CHECK(m.arg[before] == (h->block_addr ? 1000u : 1000u * 512) &&
          m.arg[before + 1] == (h->block_addr ? 1128u : 1128u * 512),
          "%s: addresses %s (0x%x, 0x%x)", what, h->block_addr ? "in blocks" : "in bytes", m.arg[before],
          m.arg[before + 1]);
    uint8_t* w = pattern(129 * 512, 7);
    before = m.n;
    CHECK(dw_mmc_io(h, 3000, 129, w, true) == 0, "%s: 129 sectors written", what);
    CHECK(!strcmp(ops(before, m.n), "25 24"), "%s: CMD25 then CMD24, got %s", what, ops(before, m.n));
    CHECK(m.n > before && (m.flags[before] & (1u << 10)), "%s: write flag set", what);
    CHECK(memcmp(m.image + 3000 * 512, w, 129 * 512) == 0, "%s: the card holds what was written", what);
    memset(buf, 0, 129 * 512);
    CHECK(dw_mmc_io(h, 3000, 129, buf, false) == 0 && memcmp(buf, w, 129 * 512) == 0, "%s: read back", what);
    CHECK(dw_mmc_io(h, bytes / 512 - 1, 2, buf, false) == -EIO, "%s: a read past the end is refused", what);
    bool fast = true;
    for(int i = find_op(17, 0); i >= 0 && i < m.n; i++){
        fast = fast && m.hz[i] <= 25000000 && m.hz[i] > 400000;
    }
    CHECK(fast, "%s: data at the data clock (<= 25 MHz)", what);
    CHECK(m.bad_access == 0, "%s: no stray register or DMA access (%d)", what, m.bad_access);
    free(w);
    free(buf);
}

static void test_sdhc_idmac64(void){
    uint64_t bytes = 64ull << 20;
    uint8_t* img = pattern(bytes, 1);
    setup(CARD_SDHC, VERID_290A, HCON_IDMAC64, 0x200000000ull, img, bytes);
    struct dw_mmc_host h = host(4, false);
    CHECK(dw_mmc_init(&h) == 0, "SDHC: init (%s)", logbuf);
    int sel = find_op(7, 0);
    CHECK(!strncmp(ops(0, sel + 1), "0 8 55 41 55 41 55 41 55 41 2 3 9 7", 64),
          "SDHC: CMD0, CMD8, ACMD41 until ready, CMD2, CMD3, CMD9, CMD7; got %s", ops(0, m.n));
    CHECK(m.arg[1] == 0x1AA, "SDHC: CMD8 asks 2.7-3.6 V with the check pattern");
    CHECK(m.arg[3] == (0x00FF8000u | 1u << 30), "SDHC: ACMD41 says HCS (0x%x)", m.arg[3]);
    CHECK(m.flags[0] & (1u << 15), "SDHC: CMD0 sends the init clocks");
    CHECK(!(m.flags[3] & (1u << 8)) && (m.flags[3] & (1u << 6)), "SDHC: ACMD41 (R3) without a CRC check");
    CHECK((m.flags[sel - 3] & (1u << 7)) && (m.flags[sel - 1] & (1u << 7)), "SDHC: CMD2 and CMD9 long responses");
    bool slow = true;
    for(int i = 0; i <= sel; i++){
        slow = slow && m.hz[i] <= 400000 && m.hz[i] >= 100000;
    }
    CHECK(slow, "SDHC: identification at 100-400 kHz (%u Hz)", m.hz[0]);
    CHECK(m.arg[sel] == 0x1234u << 16, "SDHC: CMD7 selects the RCA the card gave");
    CHECK(!strcmp(ops(sel + 1, sel + 3), "55 6") && m.arg[sel + 2] == 2 && m.width == 4 && R(0x18) == 1,
          "SDHC: ACMD6 to 4 bits, CTYPE 1");
    CHECK(find_op(16, 0) < 0, "SDHC: no CMD16 (block addressing)");
    CHECK(h.sd && h.block_addr && h.sectors == bytes / 512, "SDHC: %llu sectors, block addressing",
          (unsigned long long)h.sectors);
    CHECK(h.idmac && h.idmac64 && h.clock_hz == 25000000, "SDHC: IDMAC 64-bit, 25 MHz (%u)", h.clock_hz);
    CHECK((R(0x4C) & 0xFFF) == FIFO_DEPTH / 2 && ((R(0x4C) >> 16) & 0xFFF) == FIFO_DEPTH / 2 - 1,
          "SDHC: FIFO thresholds at half the FIFO read from FIFOTH");
    uint64_t cleaned = m.cleaned, flushed = m.flushed, inval = m.invalidated;
    io_checks(&h, "SDHC IDMAC 64");
    CHECK(m.dma_xfers >= 5 && m.pio_words == 0, "SDHC: by DMA (%d transfers)", m.dma_xfers);
    CHECK(m.flushed - flushed >= 200 * 512 && m.invalidated - inval >= 200 * 512,
          "SDHC: read pages flushed before and dropped after (%llu, %llu)",
          (unsigned long long)(m.flushed - flushed), (unsigned long long)(m.invalidated - inval));
    CHECK(m.cleaned - cleaned >= 129 * 512, "SDHC: written pages and descriptors cleaned");
    free(img);
}

static void test_idmac32(void){
    uint64_t bytes = 16ull << 20;
    uint8_t* img = pattern(bytes, 2);
    setup(CARD_SDHC, VERID_290A, HCON_IDMAC32, 0x80000000ull, img, bytes);
    struct dw_mmc_host h = host(4, false);
    CHECK(dw_mmc_init(&h) == 0 && h.idmac && !h.idmac64, "IDMAC 32-bit below 4 GiB: used (%s)", logbuf);
    io_checks(&h, "SDHC IDMAC 32");
    CHECK(m.dma_xfers >= 5 && m.pio_words == 0, "IDMAC 32: by DMA");
    free(img);

    img = pattern(bytes, 3);
    setup(CARD_SDHC, VERID_290A, HCON_IDMAC32, 0x100000000ull, img, bytes);
    h = host(4, false);
    CHECK(dw_mmc_init(&h) == 0 && !h.idmac && h.pio_why && !strcmp(h.pio_why, "32-bit IDMAC, memory above 4 GiB"),
          "IDMAC 32-bit above 4 GiB: PIO instead (%s)", h.pio_why ? h.pio_why : "-");
    io_checks(&h, "SDHC PIO fallback");
    CHECK(m.dma_xfers == 0 && m.pio_words > 0, "PIO fallback: by the FIFO (%d words)", m.pio_words);
    free(img);
}

static void test_pio64_old(void){
    uint64_t bytes = 16ull << 20;
    uint8_t* img = pattern(bytes, 4);
    setup(CARD_SDHC, VERID_230A, HCON_NODMA64W, 0x80000000ull, img, bytes);
    struct dw_mmc_host h = host(4, false);
    CHECK(dw_mmc_init(&h) == 0 && !h.idmac && h.data_off == 0x100 && h.data_width == 8,
          "no DMA, version 2.30a: PIO, FIFO at 0x100, 64-bit words");
    io_checks(&h, "PIO 64-bit FIFO");
    CHECK(m.dma_xfers == 0 && m.pio_words > 0, "PIO 64: by the FIFO");
    free(img);
}

static void test_sdsc(void){
    uint64_t bytes = 16ull << 20;
    uint8_t* img = pattern(bytes, 5);
    setup(CARD_SDSC, VERID_290A, HCON_IDMAC64, 0x80000000ull, img, bytes);
    struct dw_mmc_host h = host(4, false);
    CHECK(dw_mmc_init(&h) == 0, "SDSC: init (%s)", logbuf);
    CHECK(m.arg[find_op(41, 0)] == 0x00FF8000u, "SDSC: no CMD8 answer, ACMD41 without HCS");
    int set = find_op(16, 0);
    CHECK(set > 0 && m.arg[set] == 512, "SDSC: CMD16 sets 512-byte blocks");
    CHECK(h.sd && !h.block_addr && h.sectors == bytes / 512, "SDSC: %llu sectors from a version 1 CSD",
          (unsigned long long)h.sectors);
    io_checks(&h, "SDSC");
    free(img);
}

static void test_emmc(void){
    uint64_t bytes = 64ull << 20;
    uint8_t* img = pattern(bytes, 6);
    setup(CARD_EMMC, VERID_290A, HCON_IDMAC64, 0x80000000ull, img, bytes);
    struct dw_mmc_host h = host(8, true);
    CHECK(dw_mmc_init(&h) == 0, "eMMC: init (%s)", logbuf);
    int one = find_op(1, 0), sw = find_op(6, 0);
    CHECK(one > 0 && m.op[one - 1] == 0 && m.arg[one] == 0x40FF8000u,
          "eMMC: no CMD55 answer, CMD0 again, CMD1 with sector mode; got %s", ops(0, m.n));
    CHECK(m.arg[find_op(3, 0)] == 1u << 16, "eMMC: CMD3 gives RCA 1");
    CHECK(sw > 0 && m.arg[sw] == 0x03B70200u && m.width == 8 && R(0x18) == 1u << 16,
          "eMMC: SWITCH EXT_CSD 183 to 8 bits, CTYPE bit 16");
    CHECK(find_op(8, sw) > sw, "eMMC: EXT_CSD read (CMD8 with data)");
    CHECK(!h.sd && h.block_addr && h.sectors == bytes / 512, "eMMC: %llu sectors from SEC_COUNT",
          (unsigned long long)h.sectors);
    io_checks(&h, "eMMC");
    free(img);
}

static void test_no_card(void){
    uint8_t img[4096];
    setup(CARD_NONE, VERID_290A, HCON_IDMAC64, 0x80000000ull, img, sizeof img);
    struct dw_mmc_host h = host(4, false);
    CHECK(dw_mmc_init(&h) < 0 && strstr(logbuf, "no card answered"), "no card: init fails (%s)", logbuf);
}

static void test_fat32(const char* path){
    FILE* fp = fopen(path, "rb");
    if(!fp){
        CHECK(0, "cannot open %s", path);
        return;
    }
    fseek(fp, 0, SEEK_END);
    uint64_t bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* img = malloc(bytes);
    CHECK(fread(img, 1, bytes, fp) == bytes, "FAT32 image read");
    fclose(fp);
    setup(CARD_SDHC, VERID_290A, HCON_IDMAC64, 0x200000000ull, img, bytes);
    struct dw_mmc_host h = host(4, false);
    CHECK(dw_mmc_init(&h) == 0 && h.sectors == bytes / 512, "FAT32 card: init");
    uint8_t* buf = malloc(1 << 20);
    CHECK(dw_mmc_io(&h, 0, 2048, buf, false) == 0 && memcmp(buf, img, 1 << 20) == 0,
          "FAT32 card: the first MiB read back sector for sector");
    CHECK(!memcmp(buf + 82, "FAT32   ", 8) && buf[510] == 0x55 && buf[511] == 0xAA,
          "FAT32 card: boot sector says FAT32, signature 55 AA");
    uint32_t fsinfo = buf[48] | buf[49] << 8;
    CHECK(!memcmp(buf + fsinfo * 512, "RRaA", 4), "FAT32 card: FSInfo at sector %u", fsinfo);
    free(buf);
    free(img);
}

int main(int argc, char** argv){
    test_sdhc_idmac64();
    test_idmac32();
    test_pio64_old();
    test_sdsc();
    test_emmc();
    test_no_card();
    if(argc > 1){
        test_fat32(argv[1]);
    }
    printf("dw_mmc host test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
