// QEMU's ramfb display through fw_cfg. See ramfb.h.
//
// fw_cfg (QEMU docs/specs/fw_cfg.rst), MMIO flavour: data at +0, the
// 16-bit selector at +8, the 64-bit DMA address at +16, the last two
// big-endian. The file directory (selector 0x19) is read byte by byte
// through the data register; "etc/ramfb" can only be written by DMA:
// the device reads a FWCfgDmaAccess (control, length, address, all
// big-endian) whose physical address is written to the DMA register,
// low half last, and clears `control` when done.

#include "ramfb.h"
#include "arch/arch.h"

#define FW_CFG_DATA         0x00
#define FW_CFG_SELECTOR     0x08
#define FW_CFG_DMA          0x10
#define FW_CFG_SIGNATURE    0x0000
#define FW_CFG_ID           0x0001
#define FW_CFG_FILE_DIR     0x0019
#define FW_CFG_ID_DMA       (1u << 1)
#define FW_CFG_DMA_ERROR    0x01
#define FW_CFG_DMA_SELECT   0x08
#define FW_CFG_DMA_WRITE    0x10
#define FW_CFG_NAME_MAX     56

#define DRM_FORMAT_XRGB8888 0x34325258u     // 'XR24': bytes B, G, R, X in memory

static void select_item(volatile uint8_t* fw, uint16_t item)
{
    mmio_write16(fw + FW_CFG_SELECTOR, __builtin_bswap16(item));
}

static uint32_t read_be32(volatile uint8_t* fw)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v = v << 8 | mmio_read8(fw + FW_CFG_DATA);
    return v;
}

static int name_is(const char* a, const char* b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

// Selector of fw_cfg file `name`, or -1.
static int find_file(volatile uint8_t* fw, const char* name)
{
    select_item(fw, FW_CFG_FILE_DIR);
    uint32_t count = read_be32(fw);
    for (uint32_t i = 0; i < count; i++) {
        char entry_name[FW_CFG_NAME_MAX];
        read_be32(fw);                                  // size
        uint16_t sel = (uint16_t)(read_be32(fw) >> 16); // select, reserved
        for (int k = 0; k < FW_CFG_NAME_MAX; k++)
            entry_name[k] = (char)mmio_read8(fw + FW_CFG_DATA);
        entry_name[FW_CFG_NAME_MAX - 1] = 0;
        if (name_is(entry_name, name))
            return sel;
    }
    return -1;
}

int ramfb_setup(volatile void* fw_cfg, struct ramfb_dma* dma, uint64_t dma_pa, uint64_t fb_pa, uint32_t width,
                uint32_t height)
{
    volatile uint8_t* fw = fw_cfg;
    select_item(fw, FW_CFG_SIGNATURE);
    if (read_be32(fw) != 0x51454d55u)                   // "QEMU"
        return -1;
    select_item(fw, FW_CFG_ID);
    if (!(mmio_read8(fw + FW_CFG_DATA) & FW_CFG_ID_DMA))
        return -2;
    int sel = find_file(fw, "etc/ramfb");
    if (sel < 0)
        return -3;                                      // no -device ramfb

    dma->cfg.addr = __builtin_bswap64(fb_pa);
    dma->cfg.fourcc = __builtin_bswap32(DRM_FORMAT_XRGB8888);
    dma->cfg.flags = 0;
    dma->cfg.width = __builtin_bswap32(width);
    dma->cfg.height = __builtin_bswap32(height);
    dma->cfg.stride = __builtin_bswap32(width * 4);
    dma->access.control = __builtin_bswap32(((uint32_t)sel << 16) | FW_CFG_DMA_SELECT | FW_CFG_DMA_WRITE);
    dma->access.length = __builtin_bswap32(sizeof(dma->cfg));
    dma->access.address = __builtin_bswap64(dma_pa + __builtin_offsetof(struct ramfb_dma, cfg));
    smp_mb();
    mmio_write32(fw + FW_CFG_DMA, __builtin_bswap32((uint32_t)(dma_pa >> 32)));
    mmio_write32(fw + FW_CFG_DMA + 4, __builtin_bswap32((uint32_t)dma_pa));
    uint32_t control;
    for (int spin = 0; spin < 1000000; spin++) {
        control = __builtin_bswap32(*(volatile uint32_t*)&dma->access.control);
        if (!(control & ~FW_CFG_DMA_ERROR))
            return control & FW_CFG_DMA_ERROR ? -4 : 0;
    }
    return -5;
}
