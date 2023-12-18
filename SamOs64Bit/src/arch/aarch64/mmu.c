// Page tables, MMU enable, user address spaces. See mmu.h.
//
// mmu_early runs with the MMU off at the load address: everything it
// touches is reached through PC-relative adrp (the code model), and it
// stores physical addresses, never pointers, so the same globals are
// right once the kernel runs high. With the MMU off every access is
// Device memory: no unaligned access, nothing cached.
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/console.h"
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/pfa.h"
#include "arch/aarch64/pl011.h"
#include "arch/aarch64/sysreg.h"
#include "arch/aarch64/trap.h"

#define BLOCK_2M        (1ul << 21)
#define MAIR_VALUE      (0xfful << (8 * MT_NORMAL) | 0x04ul << (8 * MT_DEVICE))
#define SCTLR_M         (1ul << 0)
#define SCTLR_C         (1ul << 2)
#define SCTLR_I         (1ul << 12)
#define SCTLR_WXN       (1ul << 19)

// TCR_EL1: T0SZ = T1SZ = 16 (48-bit), 4 KiB granules (TG0 0, TG1 2),
// walks inner shareable and write-back cacheable, 8-bit ASIDs from
// TTBR0 (A1 = 0, AS = 0). IPS is added from ID_AA64MMFR0_EL1.
#define TCR_WALK(n)     (1ul << (8 + n) | 1ul << (10 + n) | 3ul << (12 + n))
#define TCR_VALUE       (16ul | TCR_WALK(0) | 16ul << 16 | TCR_WALK(16) | 2ul << 30)

extern char _head[], _text_end[], _rodata_end[], _stack_guard[], _stack_bottom[], _end[];

uint64_t mmu_dm_offset;
uint64_t kernel_phys;
uint64_t mmu_boot_ttbr0, mmu_boot_ttbr1, mmu_boot_sctlr;     // read by boot.S

static uint64_t kernel_root;        // TTBR1 L0 table
static uint64_t tables;
static uint64_t early_lo = ~0ul, early_hi;   // frames written with the MMU off
static uint64_t idmap_freed;
static uint32_t pa_bits;
static uint8_t asid_used[256 / 8];
// TTBR0 with no user space: every walk faults at level 0.
static uint64_t empty_root[512] __attribute__((aligned(4096)));

static inline void dsb_ishst(void) { __asm__ volatile("dsb ishst" ::: "memory"); }

static uint64_t table_alloc(void)
{
    uint64_t pa = pfa_alloc();
    if (!pa)
        return 0;
    uint64_t* t = phys_to_virt(pa);
    for (int i = 0; i < 512; i++)
        t[i] = 0;
    tables++;
    if (!mmu_dm_offset) {
        if (pa < early_lo)
            early_lo = pa;
        if (pa + PAGE_SIZE > early_hi)
            early_hi = pa + PAGE_SIZE;
    }
    return pa;
}

// Map [va, va + size) to pa with `prot` (leaf bits without type, AF).
// 2 MiB blocks where `blocks` and both sides are aligned, else pages.
// An existing identical leaf is fine; a different one, or a block in
// the way of a table, is -1.
static int map_range(uint64_t root, uint64_t va, uint64_t pa, uint64_t size, uint64_t prot, int blocks)
{
    uint64_t end = va + size;
    while (va < end) {
        uint64_t* t = phys_to_virt(root);
        for (int level = 0;; level++) {
            int shift = 39 - 9 * level;
            uint64_t* e = &t[(va >> shift) & 511];
            int block = level == 2 && blocks && !((va | pa) & (BLOCK_2M - 1)) && end - va >= BLOCK_2M &&
                        (*e & (PTE_VALID | PTE_TABLE)) != (PTE_VALID | PTE_TABLE);
            if (level == 3 || block) {
                uint64_t d = (pa & PTE_ADDR) | prot | PTE_AF | PTE_VALID | (block ? 0 : PTE_TABLE);
                if ((*e & PTE_VALID) && *e != d)
                    return -1;
                *e = d;
                va += 1ul << shift;
                pa += 1ul << shift;
                break;
            }
            if (!(*e & PTE_VALID)) {
                uint64_t n = table_alloc();
                if (!n)
                    return -1;
                *e = n | PTE_TABLE | PTE_VALID;
            } else if (!(*e & PTE_TABLE)) {
                return -1;
            }
            t = phys_to_virt(*e & PTE_ADDR);
        }
    }
    dsb_ishst();
    return 0;
}

static uint64_t lookup(uint64_t root, uint64_t va, int* level)
{
    uint64_t* t = phys_to_virt(root);
    for (int l = 0; l < 4; l++) {
        uint64_t e = t[(va >> (39 - 9 * l)) & 511];
        if (!(e & PTE_VALID))
            return 0;
        if (l == 3 || !(e & PTE_TABLE)) {
            if (level)
                *level = l;
            return e;
        }
        t = phys_to_virt(e & PTE_ADDR);
    }
    return 0;
}

// Free a table tree (tables only, not what the leaves map).
static uint64_t free_tables(uint64_t pa, int level)
{
    uint64_t freed = 1;
    if (level < 3) {
        uint64_t* t = phys_to_virt(pa);
        for (int i = 0; i < 512; i++)
            if ((t[i] & (PTE_VALID | PTE_TABLE)) == (PTE_VALID | PTE_TABLE))
                freed += free_tables(t[i] & PTE_ADDR, level + 1);
    }
    pfa_free(pa);
    tables--;
    return freed;
}

// ---- MMU off ----------------------------------------------------

static void __attribute__((noreturn)) early_fail(const struct fdt* f, const char* what, int err)
{
    pl011_init(f);
    console_printf("\nmmu: %s (error %d), halted\n", what, err);
    aarch64_halt();
}

// Direct map of [start, end) minus the no-map regions.
static int map_ram(uint64_t start, uint64_t end)
{
    int n;
    const struct mem_region* m = memmap_regions(&n);
    while (start < end) {
        uint64_t cut = end, skip = 0;
        for (int i = 0; i < n; i++) {
            uint64_t s = m[i].base & ~(PAGE_SIZE - 1);
            uint64_t e = (m[i].base + m[i].length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (!(m[i].type & MEMMAP_NOMAP) || e <= start || s >= cut)
                continue;
            if (s <= start) {
                skip = e;           // start is inside: jump past it
                break;
            }
            cut = s;
        }
        if (skip) {
            start = skip;
            continue;
        }
        if (map_range(kernel_root, DIRECT_MAP_BASE + start, start, cut - start, PROT_KRW, 1) < 0)
            return -1;
        start = cut;
    }
    return 0;
}

static void dcache_inval(uint64_t start, uint64_t end)
{
    uint64_t line = 4ul << ((sysreg_read("ctr_el0") >> 16) & 0xf);
    for (uint64_t a = start & ~(line - 1); a < end; a += line)
        __asm__ volatile("dc ivac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

void mmu_early(uint64_t dtb)
{
    static const uint8_t pa_range_bits[] = { 32, 36, 40, 42, 44, 48 };
    struct fdt f;
    int r = fdt_init(&f, (const void*)dtb, 0);
    if (r < 0)
        early_fail(0, "no valid device tree", r);

    uint64_t mmfr0 = sysreg_read("id_aa64mmfr0_el1");
    if (((mmfr0 >> 28) & 0xf) == 0xf)
        early_fail(&f, "no 4 KiB granule (ID_AA64MMFR0_EL1.TGran4)", -1);
    uint64_t ips = mmfr0 & 0xf;
    if (ips > 5)
        ips = 5;    // 52-bit needs 64 KiB pages or LPA2
    pa_bits = pa_range_bits[ips];

    kernel_phys = (uint64_t)_head;      // PC-relative: the load address
    uint64_t kend = (uint64_t)_end;
    if ((r = memmap_build(&f, dtb, kernel_phys, kend)) < 0)
        early_fail(&f, "memory map from the device tree", r);
    int n;
    const struct mem_region* m = memmap_regions(&n);
    if (pfa_init(m, n) < 0)
        early_fail(&f, "page-frame allocator: no usable RAM", -1);

    // TTBR1: the direct map, then the kernel image with W^X.
    if (!(kernel_root = table_alloc()))
        early_fail(&f, "no frame for the kernel table", -1);
    for (int i = 0; i < n; i++)
        if (m[i].type == MEM_REGION_USABLE && map_ram(m[i].base, m[i].base + m[i].length) < 0)
            early_fail(&f, "direct map", i);
    uint64_t text = (uint64_t)_text_end, ro = (uint64_t)_rodata_end;
    uint64_t guard = (uint64_t)_stack_guard, sb = (uint64_t)_stack_bottom;
    // The page below the boot stack stays unmapped (a guard).
    if (map_range(kernel_root, KERNEL_VBASE, kernel_phys, text - kernel_phys, PROT_KTEXT, 0) < 0 ||
        map_range(kernel_root, KERNEL_VBASE + text - kernel_phys, text, ro - text, PROT_KRO, 0) < 0 ||
        map_range(kernel_root, KERNEL_VBASE + ro - kernel_phys, ro, guard - ro, PROT_KRW, 0) < 0 ||
        map_range(kernel_root, KERNEL_VBASE + sb - kernel_phys, sb, kend - sb, PROT_KRW, 0) < 0)
        early_fail(&f, "kernel image map", -1);

    // TTBR0: the kernel text at its own address, only for the few
    // instructions between SCTLR_EL1.M and the jump high (boot.S).
    uint64_t idmap = table_alloc();
    if (!idmap || map_range(idmap, kernel_phys, kernel_phys, text - kernel_phys, PROT_KTEXT, 0) < 0)
        early_fail(&f, "identity map", -1);

    // Written with the caches off: drop any stale lines before the
    // MMU turns them on (QEMU has no caches; hardware does).
    dcache_inval(kernel_phys, kend);
    dcache_inval(early_lo, early_hi);
    dcache_inval(pfa_table_pa(), pfa_table_pa() + pfa_table_bytes());

    sysreg_write("mair_el1", MAIR_VALUE);
    sysreg_write("tcr_el1", TCR_VALUE | ips << 32);
    isb();
    mmu_boot_ttbr0 = idmap;
    mmu_boot_ttbr1 = kernel_root;
    mmu_boot_sctlr = sysreg_read("sctlr_el1") | SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_WXN;
}

// ---- MMU on -----------------------------------------------------

void mmu_late(void)
{
    mmu_dm_offset = DIRECT_MAP_BASE;
    sysreg_write("ttbr0_el1", kimage_pa(empty_root));
    isb();
    __asm__ volatile("tlbi vmalle1\n\tdsb nsh\n\tisb" ::: "memory");
    idmap_freed = free_tables(mmu_boot_ttbr0, 0);
    mmu_boot_ttbr0 = 0;
}

void mmu_report(void)
{
    uint64_t sctlr = sysreg_read("sctlr_el1");
    console_printf("mmu: 4 KiB granule, 48-bit VA, 4 levels, %u-bit PA, 8-bit ASIDs\n", pa_bits);
    console_printf("mmu: MAIR 0x%lx (attr0 normal WB, attr1 device nGnRE), SCTLR%s%s%s%s\n",
                   sysreg_read("mair_el1"), sctlr & SCTLR_M ? " M" : "", sctlr & SCTLR_C ? " C" : "",
                   sctlr & SCTLR_I ? " I" : "", sctlr & SCTLR_WXN ? " WXN" : "");
    console_printf("mmu: kernel 0x%lx-0x%lx at pa 0x%lx (text RX, rodata R, data/bss/stack RW XN)\n",
                   (uint64_t)_head, (uint64_t)_end, kernel_phys);
    console_printf("mmu: direct map at 0x%lx, %lu table frames, identity map removed (%lu tables freed)\n",
                   DIRECT_MAP_BASE, tables, idmap_freed);
    console_printf("pfa: %lu frames, %lu free, %lu used, %lu reserved, table 0x%lx (%lu bytes)\n",
                   pfa_total_frames(), pfa_free_frames(), pfa_used_frames(), pfa_reserved_frames(),
                   pfa_table_pa(), pfa_table_bytes());
}

uint64_t mmu_map_device(uint64_t pa, uint64_t size)
{
    if (!mmu_dm_offset)
        return pa;
    uint64_t start = pa & ~(PAGE_SIZE - 1);
    uint64_t end = (pa + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (map_range(kernel_root, DIRECT_MAP_BASE + start, start, end - start, PROT_DEVICE, 1) < 0)
        return 0;
    isb();
    return DIRECT_MAP_BASE + pa;
}

int mmu_map_kernel(uint64_t va, uint64_t pa, uint64_t size, uint64_t prot)
{
    int r = map_range(kernel_root, va, pa, size, prot, 0);
    isb();
    return r;
}

uint64_t mmu_lookup(uint64_t va, int* level)
{
    uint64_t root = va >> 63 ? kernel_root : sysreg_read("ttbr0_el1") & PTE_ADDR;
    return lookup(root, va, level);
}

uint64_t mmu_at(uint64_t va, int kind)
{
    switch (kind) {
    case AT_S1E1R: __asm__ volatile("at s1e1r, %0" :: "r"(va)); break;
    case AT_S1E1W: __asm__ volatile("at s1e1w, %0" :: "r"(va)); break;
    case AT_S1E0R: __asm__ volatile("at s1e0r, %0" :: "r"(va)); break;
    default:       __asm__ volatile("at s1e0w, %0" :: "r"(va)); break;
    }
    isb();
    return sysreg_read("par_el1");
}

uint64_t mmu_table_frames(void)
{
    return tables;
}

// ---- user address spaces ----------------------------------------

int aspace_create(struct aspace* as)
{
    uint32_t asid;
    for (asid = 1; asid < 256 && (asid_used[asid / 8] >> (asid % 8) & 1); asid++)
        ;
    if (asid == 256)
        return -1;      // no rollover yet: 255 live spaces at most
    if (!(as->root = table_alloc()))
        return -1;
    asid_used[asid / 8] |= 1 << (asid % 8);
    as->asid = asid;
    return 0;
}

int aspace_map(struct aspace* as, uint64_t va, uint64_t pa, int prot)
{
    if (va >= USER_TOP || ((va | pa) & (PAGE_SIZE - 1)))
        return -1;
    // Never executable at EL1 (PXN); UXN unless asked; AP[2] unless writable.
    uint64_t d = PTE_ATTR(MT_NORMAL) | PTE_SH_INNER | PTE_AP_EL0 | PTE_NG | PTE_PXN;
    if (!(prot & USER_W))
        d |= PTE_AP_RO;
    if (!(prot & USER_X))
        d |= PTE_UXN;
    return map_range(as->root, va, pa, PAGE_SIZE, d, 0);
}

void aspace_switch(const struct aspace* as)
{
    sysreg_write("ttbr0_el1", as ? as->root | (uint64_t)as->asid << 48 : kimage_pa(empty_root));
    isb();
}

void aspace_destroy(struct aspace* as)
{
    if ((sysreg_read("ttbr0_el1") & PTE_ADDR) == as->root)
        aspace_switch(0);
    __asm__ volatile("dsb ishst\n\ttlbi aside1is, %0\n\tdsb ish\n\tisb" :: "r"((uint64_t)as->asid << 48) : "memory");
    free_tables(as->root, 0);
    asid_used[as->asid / 8] &= ~(1 << (as->asid % 8));
    as->root = 0;
    as->asid = 0;
}
