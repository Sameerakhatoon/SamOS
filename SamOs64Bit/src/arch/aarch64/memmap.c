// Physical memory map from the device tree. See memmap.h.
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/mmu.h"
#include "boot/boot_info.h"
#include "config.h"
#include "memory/memory.h"

#define MAX_FDT_ENTRIES 16

static struct mem_region regions[MEMMAP_MAX];
static uint8_t sources[MEMMAP_MAX];
static int nodes[MEMMAP_MAX];
static int count;
// Dynamic /reserved-memory nodes: size and node only.
static uint64_t dyn_size[MAX_FDT_ENTRIES];
static int dyn_node[MAX_FDT_ENTRIES];
static int dyn_count;
static uint64_t initrd_base, initrd_size;

// A /chosen address property: one cell or two.
static uint64_t chosen_addr(const struct fdt* f, int node, const char* name)
{
    uint32_t len = 0;
    const uint8_t* p = fdt_getprop(f, node, name, &len);
    uint64_t v = 0;
    for (uint32_t i = 0; p && (len == 4 || len == 8) && i < len; i++)
        v = v << 8 | p[i];
    return v;
}

static int add(uint64_t base, uint64_t length, uint32_t type, uint8_t src, int node)
{
    if (count == MEMMAP_MAX)
        return -100;
    regions[count].base = base;
    regions[count].length = length;
    regions[count].type = type;
    sources[count] = src;
    nodes[count] = node;
    count++;
    return 0;
}

int memmap_build(const struct fdt* f, uint64_t dtb_pa, uint64_t kernel_start, uint64_t kernel_end)
{
    struct fdt_region mem[MAX_FDT_ENTRIES];
    struct fdt_reserved res[MAX_FDT_ENTRIES];
    int r = fdt_memory_regions(f, mem, MAX_FDT_ENTRIES);
    if (r < 0)
        return r;
    count = dyn_count = 0;
    for (int i = 0; i < r && i < MAX_FDT_ENTRIES; i++)
        if (mem[i].size && (r = add(mem[i].base, mem[i].size, MEM_REGION_USABLE, SRC_MEMORY, -1)) < 0)
            return r;
    if ((r = add(kernel_start, kernel_end - kernel_start, MEM_REGION_KERNEL, SRC_KERNEL, -1)) < 0 ||
        (r = add(dtb_pa, f->size, MEM_REGION_KERNEL, SRC_DTB, -1)) < 0)
        return r;
    int chosen = fdt_path_offset(f, "/chosen");
    if (chosen >= 0) {
        uint64_t s = chosen_addr(f, chosen, "linux,initrd-start"), e = chosen_addr(f, chosen, "linux,initrd-end");
        if (s && e > s) {
            initrd_base = s;
            initrd_size = e - s;
            if ((r = add(s, e - s, MEM_REGION_KERNEL, SRC_INITRD, -1)) < 0)
                return r;
        }
    }
    struct fdt_region m;
    for (int i = 0; (r = fdt_memreserve(f, i, &m)) == 0; i++)
        if ((r = add(m.base, m.size, MEM_REGION_RESERVED, SRC_MEMRESERVE, -1)) < 0)
            return r;
    if (r != FDT_ERR_NOTFOUND)
        return r;
    int n = fdt_reserved_memory(f, res, MAX_FDT_ENTRIES);
    if (n < 0)
        return n;
    for (int i = 0; i < n && i < MAX_FDT_ENTRIES; i++) {
        if (res[i].flags & FDT_RESERVED_DYNAMIC) {
            dyn_size[dyn_count] = res[i].size;
            dyn_node[dyn_count++] = res[i].node;
            continue;
        }
        uint32_t type = MEM_REGION_RESERVED | (res[i].flags & FDT_RESERVED_NOMAP ? MEMMAP_NOMAP : 0);
        if ((r = add(res[i].base, res[i].size, type, SRC_RESMEM, res[i].node)) < 0)
            return r;
    }
    return 0;
}

const struct samos_boot_info* memmap_boot_info(uint64_t pa)
{
    const struct samos_boot_info* bi = phys_to_virt(pa);
    if (!pa || (pa & 7) || bi->magic != SAMOS_BOOT_INFO_MAGIC || bi->version != SAMOS_BOOT_INFO_VERSION)
        return 0;
    return bi;
}

static int is_ram(uint32_t t)
{
    return t == SAMOS_E820_TYPE_USABLE || t == SAMOS_E820_TYPE_RECLAIMABLE || t == SAMOS_E820_TYPE_KERNEL ||
           t == SAMOS_E820_TYPE_ACPI_RECLAIMABLE || t == SAMOS_E820_TYPE_ACPI_NVS;
}

// Contiguous entries of one kind become one region.
static int add_merged(uint64_t base, uint64_t length, uint32_t type, uint8_t src)
{
    struct mem_region* last = count ? &regions[count - 1] : 0;
    if (last && sources[count - 1] == src && last->type == type && last->base + last->length == base) {
        last->length += length;
        return 0;
    }
    return add(base, length, type, src, -1);
}

int memmap_build_uefi(const struct samos_boot_info* bi, uint64_t kernel_start, uint64_t kernel_end)
{
    const uint64_t* map = phys_to_virt(bi->memory_map);
    const struct e820_entry* e = (const struct e820_entry*)(map + 1);
    uint64_t n = map[0];
    int r;
    count = dyn_count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (uint64_t i = 0; i < n; i++) {
            uint32_t t = e[i].type;
            if (pass == 0 && is_ram(t))
                r = add_merged(e[i].base_addr, e[i].length, MEM_REGION_USABLE, SRC_MEMORY);
            else if (pass == 1 && t == SAMOS_E820_TYPE_ACPI_RECLAIMABLE)
                r = add_merged(e[i].base_addr, e[i].length, MEM_REGION_ACPI, SRC_ACPI);
            else if (pass == 1 && t == SAMOS_E820_TYPE_ACPI_NVS)
                r = add_merged(e[i].base_addr, e[i].length, MEM_REGION_ACPI_NVS, SRC_ACPI);
            else
                r = 0;
            if (r < 0)
                return r;
        }
        if (pass == 0 && !count)
            return -1;
    }
    if ((r = add(kernel_start, kernel_end - kernel_start, MEM_REGION_KERNEL, SRC_KERNEL, -1)) < 0 ||
        (r = add(virt_to_phys((void*)bi),SAMOS_BOOT_DATA_PAGES * PAGE_SIZE, MEM_REGION_KERNEL, SRC_BOOT_DATA, -1)) < 0)
        return r;
    if (bi->dtb && (r = add(bi->dtb, fdt_header_totalsize(phys_to_virt(bi->dtb)), MEM_REGION_KERNEL, SRC_DTB, -1)) < 0)
        return r;
    if (bi->initrd_size) {
        initrd_base = bi->initrd_base;
        initrd_size = bi->initrd_size;
        if ((r = add(initrd_base, initrd_size, MEM_REGION_KERNEL, SRC_INITRD, -1)) < 0)
            return r;
    }
    return 0;
}

int memmap_ram(uint64_t base, uint64_t size)
{
    for (int i = 0; i < count; i++)
        if (regions[i].type == MEM_REGION_USABLE && base >= regions[i].base &&
            base + size <= regions[i].base + regions[i].length)
            return !memmap_nomap(base, size);
    return 0;
}

const struct mem_region* memmap_regions(int* n)
{
    *n = count;
    return regions;
}

int memmap_initrd(uint64_t* base, uint64_t* size)
{
    *base = initrd_base;
    *size = initrd_size;
    return initrd_size ? 0 : -1;
}

int memmap_nomap(uint64_t base, uint64_t size)
{
    for (int i = 0; i < count; i++)
        if ((regions[i].type & MEMMAP_NOMAP) && base >= regions[i].base &&
            base + size <= regions[i].base + regions[i].length)
            return 1;
    return 0;
}

void memmap_print(const struct fdt* f)
{
    static const char* const names[] = { "memory", "kernel image", "dtb", "memreserve", "reserved-memory", "initrd",
                                         "acpi", "boot data" };
    for (int i = 0; i < count; i++) {
        const char* node = nodes[i] >= 0 ? fdt_node_name(f, nodes[i]) : 0;
        console_printf("memmap: 0x%lx-0x%lx %s%s%s%s\n", regions[i].base, regions[i].base + regions[i].length,
                       names[sources[i]], node ? " " : "", node ? node : "",
                       regions[i].type & MEMMAP_NOMAP ? " (no-map)" : "");
    }
    for (int i = 0; i < dyn_count; i++)
        console_printf("memmap: reserved-memory %s: dynamic, 0x%lx bytes, not placed\n",
                       fdt_node_name(f, dyn_node[i]), dyn_size[i]);
}
