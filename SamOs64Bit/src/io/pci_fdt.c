// S-PAR - a device tree PCIe host bridge (pci_fdt.h).
#include "io/pci_fdt.h"
#include "arch/arch.h"
#include "memory/memory.h"
#include "string/string.h"
#include "status.h"

#define PCI_SPACE_IO     1          // phys.hi bits 25:24
#define PCI_SPACE_MEM32  2
#define PCI_SPACE_MEM64  3
#define PCI_MAX_PARENT_CELLS 4

static struct fdt g_dt;
static int g_node = -1;
static pci_fdt_irq_fn g_parent_irq;

static uint32_t be32(const uint8_t* p){
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t cells(const uint8_t* p, uint32_t n){
    uint64_t v = 0;
    for(uint32_t i = 0; i < n; i++){
        v = v << 32 | be32(p + 4 * i);
    }
    return v;
}

static int node_by_phandle(const struct fdt* f, uint32_t ph){
    for(int n = 0; n >= 0; n = fdt_next_node(f, n, NULL)){
        if(fdt_getprop_u32(f, n, "phandle", 0) == ph || fdt_getprop_u32(f, n, "linux,phandle", 0) == ph){
            return n;
        }
    }
    return -ENOENT;
}

static void ecam_init(const struct fdt* f, int node){
    struct fdt_region reg;
    if(fdt_reg_cpu(f, node, 0, &reg) < 0){
        return;
    }
    uint32_t len;
    const uint8_t* range = fdt_getprop(f, node, "bus-range", &len);
    uint32_t first = 0, last = (uint32_t)(reg.size >> 20) - 1;
    if(range && len == 8){
        first = be32(range);
        last = be32(range + 4);
    }
    if(last > 255 || first > last || ((uint64_t)(last - first + 1) << 20) > reg.size){
        return;
    }
    void* v = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED);
    if(v){
        pci_ecam_install_range(0, reg.base, (uint8_t)first, (uint8_t)last, v);
    }
}

// ranges: <phys.hi phys.mid phys.lo> <parent address> <size>. The
// parent is the root on QEMU virt, so its address is the CPU's.
static void windows_init(const struct fdt* f, int node){
    uint32_t len;
    const uint8_t* p = fdt_getprop(f, node, "ranges", &len);
    uint32_t pac = fdt_address_cells(f, node), sc = fdt_getprop_u32(f, node, "#size-cells", 2);
    uint32_t entry = 4 * (3 + pac + sc);
    struct pci_host_windows w;
    memset(&w, 0, sizeof(w));
    for(uint32_t off = 0; p && pac <= 2 && sc <= 2 && off + entry <= len; off += entry){
        uint32_t space = (be32(p + off) >> 24) & 3;
        struct pci_window* win = space == PCI_SPACE_IO ? &w.io : space == PCI_SPACE_MEM32 ? &w.mem32
                               : space == PCI_SPACE_MEM64 ? &w.mem64 : NULL;
        if(!win || win->size){
            continue;
        }
        win->pci_base = cells(p + off + 4, 2);
        win->cpu_base = cells(p + off + 12, pac);
        win->size = cells(p + off + 12 + 4 * pac, sc);
    }
    if(w.mem32.size || w.mem64.size){
        pci_set_host_windows(&w);
    }
}

// interrupt-map entries: child unit address (#address-cells, 3) and
// pin (#interrupt-cells, 1), the parent's phandle, its unit address
// (its #address-cells, 0 if absent) and interrupt specifier.
static int intx_route(struct pci_device* d, uint8_t pin, uint16_t* inti_flags){
    const struct fdt* f = &g_dt;
    struct pci_device* e = d;
    for(; e->parent_bridge; e = e->parent_bridge){
        pin = (uint8_t)(((pin - 1 + e->addr.slot) & 3) + 1);
    }
    uint32_t child[4] = { (uint32_t)e->addr.bus << 16 | (uint32_t)e->addr.slot << 11 | (uint32_t)e->addr.func << 8,
                          0, 0, pin };
    uint32_t mask[4] = { ~0u, ~0u, ~0u, ~0u };
    uint32_t len, mlen;
    const uint8_t* m = fdt_getprop(f, g_node, "interrupt-map-mask", &mlen);
    for(int i = 0; m && mlen == 16 && i < 4; i++){
        mask[i] = be32(m + 4 * i);
    }
    const uint8_t* p = fdt_getprop(f, g_node, "interrupt-map", &len);
    if(!p || fdt_getprop_u32(f, g_node, "#address-cells", 3) != 3 ||
       fdt_getprop_u32(f, g_node, "#interrupt-cells", 1) != 1){
        return -ENOENT;
    }
    for(uint32_t off = 0; off + 20 <= len;){
        int parent = node_by_phandle(f, be32(p + off + 16));
        if(parent < 0){
            return -ENOENT;
        }
        uint32_t pac = fdt_getprop_u32(f, parent, "#address-cells", 0);
        uint32_t pic = fdt_getprop_u32(f, parent, "#interrupt-cells", 1);
        uint32_t next = off + 20 + 4 * (pac + pic);
        if(pic > PCI_MAX_PARENT_CELLS || next > len){
            return -ENOENT;
        }
        bool match = true;
        for(int i = 0; i < 4; i++){
            match = match && (child[i] & mask[i]) == be32(p + off + 4 * i);
        }
        if(match){
            uint32_t spec[PCI_MAX_PARENT_CELLS];
            for(uint32_t i = 0; i < pic; i++){
                spec[i] = be32(p + off + 20 + 4 * (pac + i));
            }
            return g_parent_irq(f, parent, spec, pic, inti_flags);
        }
        off = next;
    }
    return -ENOENT;
}

int pci_fdt_host_init(const struct fdt* f, pci_fdt_irq_fn parent_irq){
    int node = fdt_find_compatible(f, -1, "pci-host-ecam-generic");
    const char* status = node >= 0 ? fdt_getprop_string(f, node, "status") : NULL;
    if(node < 0 || (status && strncmp(status, "okay", 5) != 0)){
        return -ENOENT;
    }
    g_dt = *f;
    g_node = node;
    ecam_init(f, node);
    windows_init(f, node);
    if(parent_irq && fdt_getprop(f, node, "interrupt-map", NULL)){
        g_parent_irq = parent_irq;
        pci_set_intx_router(intx_route);
    }
    return 0;
}

// msi-map: <rid-base msi-controller msi-base length> entries.
int pci_fdt_msi_id(const struct pci_device* d, uint32_t* id){
    uint32_t len;
    const uint8_t* p = g_node >= 0 ? fdt_getprop(&g_dt, g_node, "msi-map", &len) : NULL;
    uint32_t rid = (uint32_t)d->addr.bus << 8 | (uint32_t)d->addr.slot << 3 | d->addr.func;
    for(uint32_t off = 0; p && off + 16 <= len; off += 16){
        uint32_t base = be32(p + off), count = be32(p + off + 12);
        if(rid >= base && rid - base < count){
            *id = be32(p + off + 8) + (rid - base);
            return 0;
        }
    }
    return -ENOENT;
}
