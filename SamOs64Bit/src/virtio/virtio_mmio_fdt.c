// The virtio-mmio slots of a device tree (compatible
// "virtio,mmio", `reg`, `interrupts`), for the ports that boot with
// one. Linked by them only (x86 has no device tree). See
// virtio_mmio_fdt.h.

#include "virtio_mmio_fdt.h"
#include "virtio_bus.h"

int virtio_mmio_scan_fdt(const struct fdt* f, int (*irq_of)(const struct fdt* f, int node, uint16_t* inti),
                         bool highest_first)
{
    struct { uint64_t base, size; uint32_t irq; uint16_t inti; } s[VIRTIO_MMIO_MAX_SLOTS];
    int n = 0;
    for (int node = fdt_find_compatible(f, -1, "virtio,mmio"); node >= 0 && n < VIRTIO_MMIO_MAX_SLOTS;
         node = fdt_find_compatible(f, node, "virtio,mmio")) {
        struct fdt_region reg;
        uint16_t inti = 0;
        int irq = irq_of(f, node, &inti);
        if (fdt_reg_cpu(f, node, 0, &reg) < 0 || irq < 0)
            continue;
        // Insertion sort by address, in the order asked for.
        int i = n++;
        while (i > 0 && (highest_first ? s[i - 1].base < reg.base : s[i - 1].base > reg.base)) {
            s[i] = s[i - 1];
            i--;
        }
        s[i].base = reg.base;
        s[i].size = reg.size;
        s[i].irq = (uint32_t)irq;
        s[i].inti = inti;
    }
    for (int i = 0; i < n; i++)
        if (virtio_mmio_add_slot(s[i].base, s[i].size, s[i].irq, s[i].inti) < 0)
            return i;
    return n;
}
