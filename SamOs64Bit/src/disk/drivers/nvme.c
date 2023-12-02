// Lecture 192 - NVME driver source, part 1. MMIO accessors,
// admin command raw-issue with a busy-wait phase poll, IO-queue
// create helpers, and the per-disk private allocator.

#include "nvme.h"
#include "status.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
#include "memory/memory.h"
#include "io/pci.h"
#include "kernel.h"

static uint32_t nvme_disk_driver_read_reg(struct disk* d, uint32_t off);
static void     nvme_disk_driver_write_reg(struct disk* d, uint32_t off, uint32_t val);
static void     nvme_disk_driver_unmount(struct disk* disk);

static inline uint32_t nvme_read32(struct nvme_disk_driver_private* p, uint32_t off){
    return *(volatile uint32_t*)((uintptr_t)p->base_address_nvme + off);
}

static inline void nvme_write32(struct nvme_disk_driver_private* p, uint32_t off, uint32_t v){
    *(volatile uint32_t*)((uintptr_t)p->base_address_nvme + off) = v;
}

static inline uint64_t nvme_read64(struct nvme_disk_driver_private* p, uint32_t off){
    uint32_t lo = nvme_read32(p, off);
    uint32_t hi = nvme_read32(p, off + 4);
    return ((uint64_t)hi << 32) | lo;
}

static inline void nvme_write64(struct nvme_disk_driver_private* p, uint32_t off, uint64_t v){
    nvme_write32(p, off,     (uint32_t)(v & 0xFFFFFFFFu));
    nvme_write32(p, off + 4, (uint32_t)(v >> 32));
}

// Lecture 192 - submit one admin command and busy-wait for its
// completion phase bit to flip. Returns 0 on status==0, -EIO on
// nonzero status, -ETIMEOUT on poll exhaustion.
static int nvme_admin_cmd_raw(struct disk* disk, uint8_t opcode, uint32_t nsid,
                              uint64_t prp1, uint32_t cdw10, uint32_t cdw11){
    struct nvme_disk_driver_private* p = disk_private_data_driver(disk);

    struct nvme_submission_queue_entry cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command   = NVME_COMMAND_BITS_BUILD(opcode, 0, 0, 0);
    cmd.nsid      = nsid;
    cmd.data_ptr1 = (uint32_t)(prp1 & 0xFFFFFFFFu);
    cmd.data_ptr2 = (uint32_t)(prp1 >> 32);
    cmd.command_cdw[0] = cdw10;
    cmd.command_cdw[1] = cdw11;

    struct nvme_submission_queue_entry* sqe = p->submission_queue.ptr + p->submission_queue.tail;
    memcpy(sqe, &cmd, sizeof(cmd));
    uint16_t new_tail = (uint16_t)(p->submission_queue.tail + 1u);
    if(new_tail >= p->submission_queue.size){
        new_tail = 0;
    }
    p->submission_queue.tail = new_tail;
    nvme_disk_driver_write_reg(disk, NVME_SQTDBL_OFFSET(0, p->doorbell_stride),
                               p->submission_queue.tail);

    struct nvme_completion_queue_entry* cqe = p->completion_queue.ptr + p->completion_queue.head;
    for(int i = 0; i < 1000000; ++i){
        uint32_t st = cqe->status_phase_and_command_identifier;
        if(((st >> 16) & 1u) == p->admin_cq_phase){
            break;
        }
        __asm__ __volatile__("pause");
    }
    if(((cqe->status_phase_and_command_identifier >> 16) & 1u) != p->admin_cq_phase){
        return -ETIMEOUT;
    }

    uint32_t st = cqe->status_phase_and_command_identifier;
    uint16_t status = (uint16_t)((st >> 17) & 0x7FFFu);

    uint16_t new_head = (uint16_t)(p->completion_queue.head + 1u);
    if(new_head >= p->completion_queue.size){
        new_head = 0;
        p->admin_cq_phase ^= 1u;
    }
    p->completion_queue.head = new_head;
    nvme_disk_driver_write_reg(disk, NVME_CQTDBL_OFFSET(0, p->doorbell_stride),
                               p->completion_queue.head);

    return (status == 0u) ? 0 : -EIO;
}

static int nvme_create_io_cq(struct disk* disk, uint16_t qid, uint16_t qsize, void* cq_virt){
    uint32_t cdw10 = ((uint32_t)(qsize - 1) << 16) | qid;
    uint32_t cdw11 = 0x01;
    return nvme_admin_cmd_raw(disk, 0x05, 0, (uint64_t)(uintptr_t)cq_virt, cdw10, cdw11);
}

static int nvme_create_io_sq(struct disk* disk, uint16_t qid, uint16_t qsize,
                             void* sq_virt, uint16_t cqid){
    uint32_t cdw10 = ((uint32_t)(qsize - 1) << 16) | qid;
    uint32_t cdw11 = ((uint32_t)cqid << 16) | 0x01;
    return nvme_admin_cmd_raw(disk, 0x01, 0, (uint64_t)(uintptr_t)sq_virt, cdw10, cdw11);
}

static struct nvme_disk_driver_private* nvme_pci_private_new(struct pci_device* dev){
    struct nvme_disk_driver_private* p =
        kzalloc(sizeof(struct nvme_disk_driver_private));
    if(!p){
        return NULL;
    }
    p->device = dev;
    return p;
}

static void nvme_pci_device_private_free(struct nvme_disk_driver_private* p){
    if(p){
        kfree(p);
    }
}

static bool nvme_pci_device(struct pci_device* dev){
    return dev->class.base    == NVME_PCI_BASE_CLASS &&
           dev->class.subclass == NVME_PCI_SUBCLASS;
}

static uint32_t nvme_disk_driver_read_reg(struct disk* d, uint32_t off){
    struct nvme_disk_driver_private* p = disk_private_data_driver(d);
    if(!p){
        panic("NVME private data is NULL\n");
    }
    return nvme_read32(p, off);
}

static void nvme_disk_driver_write_reg(struct disk* d, uint32_t off, uint32_t val){
    struct nvme_disk_driver_private* p = disk_private_data_driver(d);
    if(!p){
        panic("NVME private data is NULL\n");
    }
    nvme_write32(p, off, val);
}

// Lecture 192 - identity-map BAR0 (or 2 pages as fallback) into
// the kernel address space with PCD set so MMIO accesses hit
// the device.
static void nvme_map_mmio_once(struct nvme_disk_driver_private* p){
    uintptr_t base = (uintptr_t)p->base_address_nvme;
    uint32_t sz = p->device->bars[0].size ? p->device->bars[0].size : 0x2000;
    const uint32_t flags = PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_CACHE_DISABLED;
    for(uintptr_t off = 0; off < sz; off += 0x1000){
        paging_map(kernel_desc(), (void*)(base + off), (void*)(base + off), flags);
    }
}

static inline struct nvme_submission_queue_entry* nvme_submission_queue_tail(struct nvme_disk_driver_private* p){
    return p->submission_queue.ptr + p->submission_queue.tail;
}

static inline struct nvme_completion_queue_entry* nvme_completion_queue_head(struct nvme_disk_driver_private* p){
    return p->completion_queue.ptr + p->completion_queue.head;
}

// Lecture 193 - L193 adds the device-init + admin command path.

static int nvme_admin_submission_queue_init(struct disk* disk){
    struct nvme_disk_driver_private* p = disk_private_data_driver(disk);
    return p->submission_queue.ptr ? 0 : -ENOMEM;
}

// Lecture 193 - upstream bug preserved: this checks the
// submission queue pointer rather than the completion queue
// pointer it ostensibly initialises.
static int nvme_admin_completion_queue_init(struct disk* disk){
    struct nvme_disk_driver_private* p = disk_private_data_driver(disk);
    return p->submission_queue.ptr ? 0 : -ENOMEM;
}

static uint8_t nvme_cap_doorbell_stride(struct disk* disk){
    uint32_t cap_hi = nvme_disk_driver_read_reg(disk, NVME_BASE_REGISTER_CAP + 4);
    return (uint8_t)(cap_hi & 0xF);
}

// Lecture 193 - admin command path with LBA + block count.
static int nvme_admin_send_command(struct disk* disk, uint8_t opcode,
                                   uint32_t nsid, void* data, uint64_t lba,
                                   uint16_t num_blocks,
                                   struct nvme_completion_queue_entry** completion_out){
    struct nvme_disk_driver_private* p = disk_private_data_driver(disk);
    struct nvme_submission_queue_entry cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = NVME_COMMAND_BITS_BUILD(opcode, 0, 0, 0);
    cmd.nsid    = nsid;

    uint64_t prp1 = (uint64_t)(uintptr_t)data;
    cmd.data_ptr1 = (uint32_t)(prp1 & 0xFFFFFFFFu);
    cmd.data_ptr2 = (uint32_t)(prp1 >> 32);

    cmd.command_cdw[0] = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.command_cdw[1] = (uint32_t)(lba >> 32);
    cmd.command_cdw[2] = (num_blocks == 0) ? 0 : (uint32_t)(num_blocks - 1u);

    struct nvme_submission_queue_entry* sqe = p->submission_queue.ptr + p->submission_queue.tail;
    memcpy(sqe, &cmd, sizeof(cmd));
    uint16_t new_tail = (uint16_t)(p->submission_queue.tail + 1u);
    if(new_tail >= p->submission_queue.size){
        new_tail = 0;
    }
    p->submission_queue.tail = new_tail;
    nvme_disk_driver_write_reg(disk, NVME_SQTDBL_OFFSET(0, p->doorbell_stride),
                               p->submission_queue.tail);

    struct nvme_completion_queue_entry* cqe = p->completion_queue.ptr + p->completion_queue.head;
    for(int i = 0; i < 1000000; ++i){
        uint32_t st = cqe->status_phase_and_command_identifier;
        if(((st >> 16) & 1u) == p->admin_cq_phase){
            break;
        }
        __asm__ __volatile__("pause");
    }
    if(((cqe->status_phase_and_command_identifier >> 16) & 1u) != p->admin_cq_phase){
        return -ETIMEOUT;
    }
    uint32_t st = cqe->status_phase_and_command_identifier;
    uint16_t status = (uint16_t)((st >> 17) & 0x7FFFu);

    uint16_t new_head = (uint16_t)(p->completion_queue.head + 1u);
    if(new_head >= p->completion_queue.size){
        new_head = 0;
        p->admin_cq_phase ^= 1u;
    }
    p->completion_queue.head = new_head;
    nvme_disk_driver_write_reg(disk, NVME_CQTDBL_OFFSET(0, p->doorbell_stride),
                               p->completion_queue.head);
    if(completion_out){
        *completion_out = cqe;
    }
    return (status == 0u) ? 0 : -EIO;
}

// Lecture 193 - assemble BAR0+BAR1 (64-bit BAR pair) into the
// MMIO base. Upstream's low-mask `0xFFFFFFFF0ULL` (an extra
// trailing 0) is preserved verbatim; the intent was almost
// certainly `0xFFFFFFFFFULL`.
static void* nvme_pci_mmio_base(struct pci_device* dev){
    uint64_t low  = ((uint64_t)dev->bars[0].addr) & 0xFFFFFFFF0ULL;
    uint64_t high = (uint64_t)dev->bars[1].addr;
    return (void*)(uintptr_t)((high << 32) | low);
}

// Lecture 195 - L193 shipped a `nvme_disk_driver_umount` (no
// 'n') call site. L195 fixes it to `nvme_disk_driver_unmount`.
// SamOs follows the fix and drops the typo'd shim.

static int nvme_disk_driver_mount_for_device(struct disk_driver* driver,
                                             struct pci_device* dev){
    int res = 0;
    pci_enable_bus_master(dev);

    struct nvme_disk_driver_private* p = nvme_pci_private_new(dev);
    if(!p){
        return -ENOMEM;
    }
    p->base_address_nvme = nvme_pci_mmio_base(dev);
    nvme_map_mmio_once(p);

    struct disk* disk = NULL;
    res = disk_create_new(driver, NULL, SAMOS_DISK_TYPE_REAL, 0, 0,
                          NVME_SECTOR_SIZE, p, &disk);
    if(res < 0){
        nvme_pci_device_private_free(p);
        return res;
    }

    {
        uint32_t cc = nvme_disk_driver_read_reg(disk, NVME_BASE_REGISTER_CC);
        cc &= ~1u;
        nvme_disk_driver_write_reg(disk, NVME_BASE_REGISTER_CC, cc);
        for(int i = 0; i < 5000; ++i){
            if((nvme_disk_driver_read_reg(disk, NVME_BASE_REGISTER_CSTS) & 1u) == 0){
                break;
            }
            __asm__ __volatile__("pause");
        }
        if((nvme_disk_driver_read_reg(disk, NVME_BASE_REGISTER_CSTS) & 1u) != 0){
            nvme_disk_driver_unmount(disk);
            return -EIO;
        }
    }

    uint64_t cap  = nvme_read64(p, NVME_BASE_REGISTER_CAP);
    uint16_t mqes = (uint16_t)((cap & 0xFFFFu) + 1u);
    p->doorbell_stride = (uint8_t)((cap >> 32) & 0xFu);

    p->submission_queue.size = (NVME_ADMIN_SUBMISSION_QUEUE_TOTAL_ENTRIES <= mqes)
                                 ? NVME_ADMIN_SUBMISSION_QUEUE_TOTAL_ENTRIES : mqes;
    p->completion_queue.size = (NVME_ADMIN_COMPLETION_QUEUE_TOTAL_ENTRIES <= mqes)
                                 ? NVME_ADMIN_COMPLETION_QUEUE_TOTAL_ENTRIES : mqes;
    p->submission_queue.tail = 0;
    p->completion_queue.head = 0;
    p->submission_queue.ptr = kzalloc(sizeof(*p->submission_queue.ptr) * p->submission_queue.size);
    p->completion_queue.ptr = kzalloc(sizeof(*p->completion_queue.ptr) * p->completion_queue.size);
    if(!p->submission_queue.ptr || !p->completion_queue.ptr){
        nvme_disk_driver_unmount(disk);
        return -ENOMEM;
    }

    uint32_t aqa = ((p->completion_queue.size - 1u) << 16) |
                   ((p->submission_queue.size - 1u) << 0);
    nvme_disk_driver_write_reg(disk, NVME_BASE_REGISTER_AQA, aqa);
    nvme_write64(p, NVME_BASE_REGISTER_ASQ, (uint64_t)(uintptr_t)p->submission_queue.ptr);
    nvme_write64(p, NVME_BASE_REGISTER_ACQ, (uint64_t)(uintptr_t)p->completion_queue.ptr);

    {
        uint32_t cc = 0;
        cc |= (0u << 7);   // MPS=0 -> 4k
        cc |= (0u << 4);   // CSS=NVM
        cc |= (6u << 16);  // IOSQES
        cc |= (4u << 20);  // IOCQES
        cc |= 1u;          // CC.EN
        nvme_disk_driver_write_reg(disk, NVME_BASE_REGISTER_CC, cc);
    }

    for(int i = 0; i < 5000; ++i){
        if((nvme_disk_driver_read_reg(disk, NVME_BASE_REGISTER_CSTS) & 1u) == 1u){
            break;
        }
        __asm__ __volatile__("pause");
    }
    if((nvme_disk_driver_read_reg(disk, NVME_BASE_REGISTER_CSTS) & 1u) != 1u){
        nvme_disk_driver_unmount(disk);
        return -EIO;
    }

    p->admin_cq_phase = 1;
    p->nsid           = 1;

    const uint16_t io_entries = (mqes < 64 ? mqes : 64);
    p->io_submission_queue.size = io_entries;
    p->io_completion_queue.size = io_entries;
    p->io_submission_queue.tail = 0;
    p->io_completion_queue.head = 0;
    p->io_completion_queue.phase = 1;

    p->io_submission_queue.ptr = kzalloc(sizeof(struct nvme_submission_queue_entry) * io_entries);
    p->io_completion_queue.ptr = kzalloc(sizeof(struct nvme_completion_queue_entry) * io_entries);
    if(!p->io_submission_queue.ptr || !p->io_completion_queue.ptr){
        nvme_disk_driver_unmount(disk);
        return -ENOMEM;
    }

    res = nvme_create_io_cq(disk, 1, io_entries, p->io_completion_queue.ptr);
    if(res < 0){
        nvme_disk_driver_unmount(disk);
        return res;
    }
    res = nvme_create_io_sq(disk, 1, io_entries, p->io_submission_queue.ptr, 1);
    if(res < 0){
        nvme_disk_driver_unmount(disk);
        return res;
    }
    return 0;
}

// Lecture 194 - IO submit-and-poll. Build a one-or-two-page
// PRP list, post into IO SQ 1, ring the doorbell, spin-poll
// the IO CQ for the phase flip.
static int nvme_io_submit_and_poll(struct disk* disk, uint8_t opcode,
                                   uint64_t lba, uint16_t nlb, void* buf){
    struct nvme_disk_driver_private* p = disk_private_data_driver(disk);
    const uint32_t page_size = 4096;
    uint32_t bytes = (uint32_t)nlb * NVME_SECTOR_SIZE;
    uintptr_t addr = (uintptr_t)buf;

    uint32_t first_span = page_size - (uint32_t)(addr & (page_size - 1));
    if(first_span > bytes){
        first_span = bytes;
    }
    uint64_t prp1 = (uint64_t)addr;
    uint64_t prp2 = 0;
    if(bytes > first_span){
        uint32_t remain = bytes - first_span;
        if(remain > page_size){
            remain = page_size;
        }
        prp2 = (uint64_t)(addr + first_span);
        uint32_t described = first_span + remain;
        nlb = (uint16_t)((described + NVME_SECTOR_SIZE - 1) / NVME_SECTOR_SIZE);
    }

    struct nvme_submission_queue_entry cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command   = NVME_COMMAND_BITS_BUILD(opcode, 0, 0, 0);
    cmd.nsid      = p->nsid;
    cmd.data_ptr1 = (uint32_t)(prp1 & 0xFFFFFFFFu);
    cmd.data_ptr2 = (uint32_t)(prp1 >> 32);
    cmd.data_ptr3 = (uint32_t)(prp2 & 0xFFFFFFFFu);
    cmd.data_ptr4 = (uint32_t)(prp2 >> 32);
    cmd.command_cdw[0] = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.command_cdw[1] = (uint32_t)(lba >> 32);
    cmd.command_cdw[2] = (uint32_t)(nlb - 1u);

    struct nvme_submission_queue_entry* sqe = p->io_submission_queue.ptr + p->io_submission_queue.tail;
    memcpy(sqe, &cmd, sizeof(cmd));
    uint16_t new_tail = (uint16_t)(p->io_submission_queue.tail + 1u);
    if(new_tail >= p->io_submission_queue.size){
        new_tail = 0;
    }
    p->io_submission_queue.tail = new_tail;
    nvme_disk_driver_write_reg(disk, NVME_SQTDBL_OFFSET(1, p->doorbell_stride),
                               p->io_submission_queue.tail);

    struct nvme_completion_queue_entry* cqe = p->io_completion_queue.ptr + p->io_completion_queue.head;
    for(int i = 0; i < 1000000; ++i){
        uint32_t st = cqe->status_phase_and_command_identifier;
        if(((st >> 16) & 1u) == p->io_completion_queue.phase){
            break;
        }
        __asm__ __volatile__("pause");
    }
    if(((cqe->status_phase_and_command_identifier >> 16) & 1u) != p->io_completion_queue.phase){
        return -ETIMEOUT;
    }

    uint32_t st2 = cqe->status_phase_and_command_identifier;
    uint16_t status = (uint16_t)((st2 >> 17) & 0x7FFFu);
    uint16_t new_head = (uint16_t)(p->io_completion_queue.head + 1u);
    if(new_head >= p->io_completion_queue.size){
        new_head = 0;
        p->io_completion_queue.phase ^= 1u;
    }
    p->io_completion_queue.head = new_head;
    nvme_disk_driver_write_reg(disk, NVME_CQTDBL_OFFSET(1, p->doorbell_stride),
                               p->io_completion_queue.head);
    return (status == 0u) ? 0 : -EIO;
}

static int nvme_disk_driver_mount(struct disk_driver* driver){
    int res = 0;
    size_t total_pci = pci_device_count();
    for(size_t i = 0; i < total_pci; i++){
        struct pci_device* dev = NULL;
        res = pci_device_get(i, &dev);
        if(res < 0){
            break;
        }
        if(nvme_pci_device(dev)){
            res = nvme_disk_driver_mount_for_device(driver, dev);
            if(res < 0){
                break;
            }
        }
    }
    return res;
}

static void nvme_disk_driver_unmount(struct disk* disk){
    struct nvme_disk_driver_private* p = disk_private_data_driver(disk);
    if(p){
        nvme_pci_device_private_free(p);
    }
}

// Lecture 194 - chunked READ. Upstream's LBA advance line is
// `slba + nlb;` (no assignment, dead expression). Preserved
// verbatim under an explicit `(void)` cast so the project's
// `-Werror=unused-value` does not reject it.
static int nvme_disk_driver_read(struct disk* disk, unsigned int lba,
                                 int total_sectors, void* buf_out){
    struct disk* hw = disk_hardware_disk(disk);
    if(!hw){
        hw = disk;
    }
    int remaining = total_sectors;
    uint64_t slba = (uint64_t)lba;
    uint8_t* p = (uint8_t*)buf_out;
    while(remaining > 0){
        uint16_t nlb = (remaining > 16) ? 16 : (uint16_t)remaining;
        int rc = nvme_io_submit_and_poll(hw, NVME_OPCODE_READ, slba, nlb, p);
        if(rc < 0){
            return rc;
        }
        // Lecture 195 - upstream L194 wrote `slba + nlb;` (a
        // dead expression). L195 fixes it to `slba += nlb;`.
        slba += nlb;
        p += (size_t)nlb * NVME_SECTOR_SIZE;
        remaining -= nlb;
    }
    return 0;
}

static int nvme_disk_driver_write(struct disk* disk, unsigned int lba,
                                  int total_sectors, void* buf_in){
    struct disk* hw = disk_hardware_disk(disk);
    if(!hw){
        hw = disk;
    }
    int remaining = total_sectors;
    uint64_t slba = (uint64_t)lba;
    uint8_t* p = (uint8_t*)buf_in;
    while(remaining > 0){
        uint16_t nlb = (remaining > 16) ? 16 : (uint16_t)remaining;
        int rc = nvme_io_submit_and_poll(hw, NVME_OPCODE_WRITE, slba, nlb, p);
        if(rc < 0){
            return rc;
        }
        slba += nlb;
        p += (size_t)nlb * NVME_SECTOR_SIZE;
        remaining -= nlb;
    }
    return 0;
}

static int nvme_disk_driver_mount_partition(struct disk* disk, long start_lba,
                                            long end_lba, struct disk** out){
    return disk_create_new(disk->driver, disk->hardware_disk,
                           SAMOS_DISK_TYPE_PARTITION,
                           start_lba, end_lba, disk->sector_size, NULL, out);
}

static struct disk_driver nvme_driver = {
    .name = {"NVME"},
    .functions.loaded          = NULL,
    .functions.unloaded        = NULL,
    .functions.read            = nvme_disk_driver_read,
    .functions.write           = nvme_disk_driver_write,
    .functions.mount           = nvme_disk_driver_mount,
    .functions.unmount         = nvme_disk_driver_unmount,
    .functions.mount_partition = nvme_disk_driver_mount_partition,
};

struct disk_driver* nvme_driver_init(void){
    return &nvme_driver;
}
