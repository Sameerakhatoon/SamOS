// Host-side test of the virtio core (src/virtio/virtio.c) and
// the virtio-mmio transport (virtio_mmio.c) against a fake virtio-mmio
// block device. x86 QEMU has no virtio-mmio, so this is where the MMIO
// register sequence is checked before the aarch64 device work/the riscv64 device work use it on real slots.
// Built with -DVIRTIO_HOST_TEST: every mmio_read/write lands in the
// fake device below, and "physical" addresses are host pointers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "virtio/virtio.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_io.h"
#include "keyboard/virtio_input.h"
#include "status.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if(!(cond)){ failures++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

// ---- DMA memory: host pages, identity "physical" addresses ------------
void* virtio_dma_page(void){
    void* p = NULL;
    if(posix_memalign(&p, 4096, 4096) != 0){
        return NULL;
    }
    memset(p, 0, 4096);
    return p;
}
void virtio_dma_free(void* p){ free(p); }
uint64_t virtio_dma_phys(void* v){ return (uint64_t)(uintptr_t)v; }

// ---- the fake device -----------------------------------------------------
#define FAKE_SECTORS 2048
static uint8_t fake_regs_window[VIRTIO_MMIO_SLOT_SIZE];   // only its address is used
static struct {
    uint32_t magic, version, device_id;
    uint64_t features;
    uint32_t dev_sel, drv_sel;
    uint64_t driver_features;
    uint32_t status;
    uint8_t  status_log[16];
    int      status_writes;
    uint32_t queue_sel, queue_num, queue_ready, queue_max;
    uint64_t desc, driver, device;
    uint32_t isr;
    uint32_t generation;
    int      bump_generation_once;
    int      notifies;
    uint16_t last_avail;
    uint8_t  disk[FAKE_SECTORS * 512];
} fake;

static void fake_reset(uint32_t version, uint32_t device_id, uint64_t features){
    memset(&fake, 0, sizeof(fake));
    fake.magic = VIRTIO_MMIO_MAGIC;
    fake.version = version;
    fake.device_id = device_id;
    fake.features = features;
    fake.queue_max = 256;
}

// Serve every request the driver made available on queue 0, as a
// virtio-blk would: header, data, status byte.
static void fake_process(void){
    struct virtq_desc* desc = (struct virtq_desc*)(uintptr_t)fake.desc;
    struct virtq_avail* avail = (struct virtq_avail*)(uintptr_t)fake.driver;
    struct virtq_used* used = (struct virtq_used*)(uintptr_t)fake.device;
    while(fake.last_avail != avail->idx){
        uint16_t head = avail->ring[fake.last_avail % fake.queue_num];
        fake.last_avail++;
        uint16_t i = head;
        uint32_t written = 0;
        uint8_t* hdr = (uint8_t*)(uintptr_t)desc[i].addr;
        uint32_t type; uint64_t sector;
        memcpy(&type, hdr, 4);
        memcpy(&sector, hdr + 8, 8);
        uint64_t pos = sector * 512;
        while(desc[i].flags & VIRTQ_DESC_F_NEXT){
            i = desc[i].next;
            uint8_t* buf = (uint8_t*)(uintptr_t)desc[i].addr;
            if(!(desc[i].flags & VIRTQ_DESC_F_NEXT)){
                *buf = 0;                          // status OK
                written++;
                break;
            }
            if(type == 0 && (desc[i].flags & VIRTQ_DESC_F_WRITE)){
                memcpy(buf, fake.disk + pos, desc[i].len);
                written += desc[i].len;
            } else if(type == 1){
                memcpy(fake.disk + pos, buf, desc[i].len);
            }
            pos += desc[i].len;
        }
        used->ring[used->idx % fake.queue_num].id = head;
        used->ring[used->idx % fake.queue_num].len = written;
        used->idx++;
        fake.isr |= VIRTIO_ISR_QUEUE;
    }
}

static uint32_t fake_off(volatile void* a){
    return (uint32_t)((volatile uint8_t*)a - fake_regs_window);
}

uint32_t mmio_read32(volatile void* a){
    uint32_t off = fake_off(a);
    switch(off){
        case VIRTIO_MMIO_MAGIC_VALUE:      return fake.magic;
        case VIRTIO_MMIO_VERSION:          return fake.version;
        case VIRTIO_MMIO_DEVICE_ID:        return fake.device_id;
        case VIRTIO_MMIO_VENDOR_ID:        return 0x554d4551;
        case VIRTIO_MMIO_DEVICE_FEATURES:  return (uint32_t)(fake.features >> (32 * fake.dev_sel));
        case VIRTIO_MMIO_QUEUE_NUM_MAX:    return fake.queue_sel == 0 ? fake.queue_max : 0;
        case VIRTIO_MMIO_QUEUE_READY:      return fake.queue_ready;
        case VIRTIO_MMIO_INTERRUPT_STATUS: return fake.isr;
        case VIRTIO_MMIO_STATUS:           return fake.status;
        case VIRTIO_MMIO_CONFIG_GENERATION:
            // bump_generation_once: the config changes between the
            // driver's first generation read and its second.
            if(fake.bump_generation_once == 2){
                fake.generation++;
                fake.bump_generation_once = 0;
            } else if(fake.bump_generation_once == 1){
                fake.bump_generation_once = 2;
            }
            return fake.generation;
        case VIRTIO_MMIO_CONFIG:           return FAKE_SECTORS + (fake.generation ? 0 : 1000);
        case VIRTIO_MMIO_CONFIG + 4:       return 0;
    }
    printf("fake: unexpected read32 at 0x%x\n", off);
    failures++;
    return 0;
}

void mmio_write32(volatile void* a, uint32_t v){
    uint32_t off = fake_off(a);
    switch(off){
        case VIRTIO_MMIO_DEVICE_FEATURES_SEL: fake.dev_sel = v; return;
        case VIRTIO_MMIO_DRIVER_FEATURES_SEL: fake.drv_sel = v; return;
        case VIRTIO_MMIO_DRIVER_FEATURES:
            fake.driver_features &= ~(0xFFFFFFFFULL << (32 * fake.drv_sel));
            fake.driver_features |= (uint64_t)v << (32 * fake.drv_sel);
            return;
        case VIRTIO_MMIO_QUEUE_SEL:   fake.queue_sel = v; return;
        case VIRTIO_MMIO_QUEUE_NUM:   fake.queue_num = v; return;
        case VIRTIO_MMIO_QUEUE_READY: fake.queue_ready = v; return;
        case VIRTIO_MMIO_QUEUE_DESC_LOW:    fake.desc   = (fake.desc   & ~0xFFFFFFFFULL) | v; return;
        case VIRTIO_MMIO_QUEUE_DESC_HIGH:   fake.desc   = (fake.desc   & 0xFFFFFFFFULL) | ((uint64_t)v << 32); return;
        case VIRTIO_MMIO_QUEUE_DRIVER_LOW:  fake.driver = (fake.driver & ~0xFFFFFFFFULL) | v; return;
        case VIRTIO_MMIO_QUEUE_DRIVER_HIGH: fake.driver = (fake.driver & 0xFFFFFFFFULL) | ((uint64_t)v << 32); return;
        case VIRTIO_MMIO_QUEUE_DEVICE_LOW:  fake.device = (fake.device & ~0xFFFFFFFFULL) | v; return;
        case VIRTIO_MMIO_QUEUE_DEVICE_HIGH: fake.device = (fake.device & 0xFFFFFFFFULL) | ((uint64_t)v << 32); return;
        case VIRTIO_MMIO_QUEUE_NOTIFY:
            fake.notifies++;
            if(v == 0 && fake.queue_ready){
                fake_process();
            }
            return;
        case VIRTIO_MMIO_INTERRUPT_ACK: fake.isr &= ~v; return;
        case VIRTIO_MMIO_STATUS:
            if(fake.status_writes < 16){
                fake.status_log[fake.status_writes++] = (uint8_t)v;
            }
            // FEATURES_OK sticks only when the driver took VERSION_1.
            if((v & VIRTIO_STATUS_FEATURES_OK) && !(fake.driver_features & (1ULL << VIRTIO_F_VERSION_1))){
                v &= ~VIRTIO_STATUS_FEATURES_OK;
            }
            fake.status = v;
            if(v == 0){
                fake.queue_ready = 0;
            }
            return;
    }
    printf("fake: unexpected write32 0x%x at 0x%x\n", v, off);
    failures++;
}

uint8_t mmio_read8(volatile void* a){ return (uint8_t)mmio_read32(a); }
uint16_t mmio_read16(volatile void* a){ return (uint16_t)mmio_read32(a); }
void mmio_write8(volatile void* a, uint8_t v){ mmio_write32(a, v); }
void mmio_write16(volatile void* a, uint16_t v){ mmio_write32(a, v); }

// ---- the test --------------------------------------------------------------
static int blk_request(struct virtq* q, uint32_t type, uint64_t sector, uint8_t* data, uint32_t len,
                       uint8_t* hdr_page){
    memcpy(hdr_page, &type, 4);
    memset(hdr_page + 4, 0, 4);
    memcpy(hdr_page + 8, &sector, 8);
    hdr_page[64] = 0xFF;
    struct virtq_buf bufs[3] = {
        { virtio_dma_phys(hdr_page), 16, false },
        { virtio_dma_phys(data), len, type == 0 },
        { virtio_dma_phys(hdr_page + 64), 1, true },
    };
    int head = virtq_add(q, bufs, 3);
    virtq_kick(q);
    uint32_t got_head = 0xFFFF, got_len = 0;
    CHECK(virtq_pop_used(q, &got_head, &got_len), "request completed");
    CHECK((int)got_head == head, "used id %u is the head %d", got_head, head);
    return hdr_page[64];
}

int main(void){
    struct virtio_dev d;
    volatile void* base = fake_regs_window;

    // Empty slot, legacy device, bad magic.
    fake_reset(2, 0, 0);
    CHECK(virtio_mmio_attach(base, &d) == -ENOENT, "empty slot is -ENOENT");
    fake_reset(1, VIRTIO_ID_BLOCK, 0);
    CHECK(virtio_mmio_attach(base, &d) == -EIO, "version 1 (legacy) is refused");
    fake_reset(2, VIRTIO_ID_BLOCK, 0);
    fake.magic = 0;
    CHECK(virtio_mmio_attach(base, &d) == -EIO, "bad magic is refused");

    // A device without VERSION_1 fails negotiation and is marked FAILED.
    fake_reset(2, VIRTIO_ID_BLOCK, 1ULL << 9);
    CHECK(virtio_mmio_attach(base, &d) == 0, "attach");
    CHECK(virtio_negotiate(&d, ~0ULL) == -EIO, "no VERSION_1, negotiation fails");
    CHECK(fake.status & VIRTIO_STATUS_FAILED, "FAILED set, status 0x%x", fake.status);

    // The real sequence.
    fake_reset(2, VIRTIO_ID_BLOCK, (1ULL << VIRTIO_F_VERSION_1) | (1ULL << 9) | (1ULL << 6) | (1ULL << 33));
    CHECK(virtio_mmio_attach(base, &d) == 0 && d.device_id == VIRTIO_ID_BLOCK, "attach a block device");
    CHECK(virtio_negotiate(&d, (1ULL << 9) | (1ULL << 5)) == 0, "negotiate");
    CHECK(d.features == ((1ULL << VIRTIO_F_VERSION_1) | (1ULL << 9)),
          "negotiated VERSION_1 and FLUSH only: 0x%llx", (unsigned long long)d.features);
    CHECK(fake.driver_features == d.features, "the device was told the same features");
    const uint8_t want[] = {0, 1, 3, 11};
    CHECK(fake.status_writes == 4 && memcmp(fake.status_log, want, 4) == 0,
          "status writes 0, 1, 3, 11 (got %d writes, last 0x%x)", fake.status_writes, fake.status);

    fake.bump_generation_once = 1;
    CHECK(virtio_cfg_read64(&d, 0) == FAKE_SECTORS, "capacity re-read after a generation change");

    struct virtq q;
    CHECK(virtq_setup(&d, &q, 0, 1000) == 0, "queue 0 set up");
    CHECK(q.size == VIRTQ_MAX_SIZE && fake.queue_num == VIRTQ_MAX_SIZE, "queue size capped at %d: %u", VIRTQ_MAX_SIZE, q.size);
    CHECK(fake.queue_ready == 1, "queue ready");
    CHECK(fake.desc == q.desc_phys && fake.driver == q.avail_phys && fake.device == q.used_phys,
          "ring addresses given to the device");
    CHECK((q.used_phys & 3) == 0 && q.used_phys + 6 + 8 * q.size <= q.desc_phys + 4096,
          "used ring aligned and inside the page");
    CHECK(virtq_setup(&d, &q, 1, 8) == -ENOENT, "queue 1 does not exist");
    struct virtq q0b;
    CHECK(virtq_setup(&d, &q0b, 0, 8) == -ENOENT, "queue 0 already in use");
    virtio_driver_ok(&d);
    CHECK(fake.status == 15, "DRIVER_OK: status 0x%x", fake.status);

    // Write then read back through the queue.
    uint8_t* hdr = virtio_dma_page();
    uint8_t* data = virtio_dma_page();
    for(int i = 0; i < 1024; i++){
        data[i] = (uint8_t)(i * 7 + 3);
    }
    CHECK(blk_request(&q, 1, 5, data, 1024, hdr) == 0, "write status OK");
    CHECK(memcmp(fake.disk + 5 * 512, data, 1024) == 0, "the device got the data");
    memset(data, 0, 1024);
    CHECK(blk_request(&q, 0, 5, data, 1024, hdr) == 0, "read status OK");
    CHECK(data[0] == 3 && data[1023] == (uint8_t)(1023 * 7 + 3), "read back what was written");
    CHECK(q.num_free == q.size, "every descriptor back on the free list");
    CHECK(d.ops->isr(&d) == VIRTIO_ISR_QUEUE && fake.isr == 0, "ISR read and acknowledged");

    // Fill the ring with one-descriptor chains, then drain it.
    int notifies = fake.notifies;
    struct virtq_buf one = { virtio_dma_phys(hdr), 16, false };
    int added = 0;
    while(virtq_add(&q, &one, 1) >= 0){
        added++;
    }
    CHECK(added == q.size, "%d chains fit, expected %u", added, q.size);
    CHECK(virtq_add(&q, &one, 1) == -ENOSPC, "a full ring says -ENOSPC");
    CHECK(fake.notifies == notifies, "no doorbell before the kick");
    // The fake has no header type 1/0 here: hdr still holds the read
    // request, so each chain is a "read" with no data. Only the ring matters.
    virtq_kick(&q);
    int popped = 0;
    while(virtq_pop_used(&q, NULL, NULL)){
        popped++;
    }
    CHECK(popped == q.size && q.num_free == q.size, "drained %d, %u free", popped, q.num_free);
    struct virtq_buf many[VIRTQ_MAX_SIZE];
    for(int i = 0; i < VIRTQ_MAX_SIZE; i++){
        many[i] = one;
    }
    CHECK(virtq_add(&q, many, q.size) >= 0, "one chain of every descriptor after the wrap");
    CHECK(virtq_add(&q, &one, 1) == -ENOSPC, "and then the ring is full");

    // NO_NOTIFY in the used ring suppresses the doorbell.
    q.used->flags = VIRTQ_USED_F_NO_NOTIFY;
    notifies = fake.notifies;
    virtq_kick(&q);
    CHECK(fake.notifies == notifies, "no doorbell while the device says NO_NOTIFY");

    virtio_reset(&d);
    CHECK(fake.status == 0 && fake.queue_ready == 0, "reset");

    // virtio-input key codes -> scancode set 1.
    CHECK(virtio_input_key_to_set1(30) == 0x1E, "KEY_A");
    CHECK(virtio_input_key_to_set1(1) == 0x01 && virtio_input_key_to_set1(83) == 0x53, "ESC and KPDOT");
    CHECK(virtio_input_key_to_set1(88) == 0x58, "F12");
    CHECK(virtio_input_key_to_set1(103) == 0x148, "KEY_UP is E0 48");
    CHECK(virtio_input_key_to_set1(97) == 0x11D, "right Ctrl is E0 1D");
    CHECK(virtio_input_key_to_set1(84) == 0 && virtio_input_key_to_set1(0x110) == 0, "no set 1 code");

    free(hdr);
    free(data);
    printf("virtio host test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
