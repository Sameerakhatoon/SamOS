// Sv48 / Sv39 page tables, the kernel's address space, user
// address spaces. See mmu.h for the layout.
//
// mmu_boot runs from boot.S at the physical load address with the MMU
// off; everything it reaches (this file's walker, pfa_total_frames, globals)
// is addressed PC-relative (medany), so it works there as well as at
// KERNEL_VMA. It must not use a pointer stored in initialised data,
// which holds the link address.

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/riscv64.h"
#include "cpu/percpu.h"
#include "firmware/fdt.h"
#include "firmware/fdt_dev.h"
#include "memory/memregion.h"
#include "memory/pfa.h"

#define PTE_PPN_MASK 0x003ffffffffffc00ul
#define MMIO_WINDOW  (1ul << 30)
#define MAX_REGIONS  32

// linker.ld; each boundary is page aligned.
extern char __kernel_start[], __text_end[], __rodata_end[], __image_end[];
extern char __pt_pool_start[], __pt_pool_end[], __stack_start[], __kernel_end[];

// The layout the shared kernel sees (layout.h).
uint64_t dmap_base, dmap_size, arena_base, user_end;
uint64_t kernel_va_off;                     // VA - PA of the image
int mmu_levels;

static uint64_t kernel_pa;                  // load address
static uint64_t kernel_root;                // physical
static uint64_t pt_virt_off;                // tables are at PA + this (0 with the MMU off)
static uint64_t mmio_base, mmio_next;
static unsigned pool_used;
static bool sealed;                         // kernel root-level upper half is final
static uint64_t sv48_readback;              // satp after the Sv48 probe write
static bool sv39_forced;                    // Sv48 not tried (sv39_only)
static uint64_t boot_dtb, boot_dtb_size;
static uint64_t initrd_base, initrd_size;
static unsigned asid_bits;
static uint32_t next_asid;

static struct mem_region regions[MAX_REGIONS];
static int nregions;

// The shared multiheap's arena (VA = arena_base + PA) sits
// between the direct map and the mmio window, so the window moved up
// from 0xffffc00000000000 in Sv48 (layout.h).
static void set_layout(int levels)
{
    mmu_levels = levels;
    if (levels == 4) {
        dmap_base = 0xffff800000000000ul;
        dmap_size = 1ul << 46;
        arena_base = 0xffffc00000000000ul;
        mmio_base = 0xffffe00000000000ul;
        user_end = 1ul << 47;
    } else {
        dmap_base = 0xffffffc000000000ul;
        dmap_size = 1ul << 36;
        arena_base = 0xffffffd000000000ul;
        mmio_base = 0xffffffe000000000ul;
        user_end = 1ul << 38;
    }
    mmio_next = mmio_base;
}

uint64_t kernel_image_pa(const void* sym)
{
    uint64_t a = (uint64_t)sym;
    return a >= KERNEL_VMA ? a - kernel_va_off : a;
}

uint64_t mmu_kernel_pa(void)
{
    return kernel_pa;
}

// ---- the walker: one code path, `mmu_levels` levels ---------------

static inline unsigned vpn(uint64_t va, int level)
{
    return (va >> (12 + 9 * level)) & 511;
}

static inline uint64_t* table_at(uint64_t pa)
{
    return (uint64_t*)(pa + pt_virt_off);
}

static inline uint64_t pte_pa(uint64_t pte)
{
    return (pte & PTE_PPN_MASK) << 2;
}

static inline uint64_t pte_make(uint64_t pa, uint64_t flags)
{
    return ((pa >> 2) & PTE_PPN_MASK) | flags | PTE_V;
}

// A zeroed table: from the boot pool (in the image, so reserved with
// it) until the frame allocator is up, then from the allocator (the
// shared one, memory/pfa.c, which kheap_init builds).
static uint64_t pt_alloc(void)
{
    uint64_t pa;
    if (pfa_total_frames()) {
        void* p = pfa_alloc_frames(1);
        if (!p) {
            return 0;
        }
        pa = virt_to_phys(p);
    } else {
        if (pool_used >= (__pt_pool_end - __pt_pool_start) / PAGE_SIZE) {
            return 0;
        }
        pa = kernel_image_pa(__pt_pool_start + pool_used++ * PAGE_SIZE);
    }
    uint64_t* t = table_at(pa);
    for (int i = 0; i < 512; i++) {
        t[i] = 0;
    }
    return pa;
}

// The PTE for `va` at `level` (0 = 4 KiB) under `root`, creating the
// tables above it when `alloc`. NULL when a leaf above is in the way,
// on no memory, or when a new root-level entry would be needed in the
// sealed kernel root.
uint64_t* pt_entry(uint64_t root, uint64_t va, int level, bool alloc)
{
    uint64_t* t = table_at(root);
    for (int l = mmu_levels - 1; l > level; l--) {
        uint64_t* e = &t[vpn(va, l)];
        if (!(*e & PTE_V)) {
            if (!alloc || (sealed && root == kernel_root && l == mmu_levels - 1)) {
                return 0;
            }
            uint64_t n = pt_alloc();
            if (!n) {
                return 0;
            }
            *e = pte_make(n, 0);
        } else if (*e & PTE_RWX) {
            return 0;
        }
        t = table_at(pte_pa(*e));
    }
    return &t[vpn(va, level)];
}

// The 4 KiB PTE for `va`, as pt_entry(.., 0, true), but a
// megapage or gigapage in the way is split into a table of the next
// size down with the same flags (it maps the same addresses, so no
// fence is needed for the others). paging_map's path: graphics_setup
// aliases a kzalloc'd buffer, which may sit in the direct map, onto
// the framebuffer.
uint64_t* pt_entry_split(uint64_t root, uint64_t va)
{
    uint64_t* t = table_at(root);
    for (int l = mmu_levels - 1; l > 0; l--) {
        uint64_t* e = &t[vpn(va, l)];
        if (!(*e & PTE_V) || (*e & PTE_RWX)) {
            if (!(*e & PTE_V) && sealed && root == kernel_root && l == mmu_levels - 1) {
                return 0;
            }
            uint64_t n = pt_alloc();
            if (!n) {
                return 0;
            }
            if (*e & PTE_V) {
                uint64_t child = 1ul << (12 + 9 * (l - 1)), pa = pte_pa(*e), flags = *e & 0x3FF;
                uint64_t* c = table_at(n);
                for (int i = 0; i < 512; i++) {
                    c[i] = ((pa + i * child) >> 2 & PTE_PPN_MASK) | flags;
                }
            }
            *e = pte_make(n, 0);
        }
        t = table_at(pte_pa(*e));
    }
    return &t[vpn(va, 0)];
}

// Map [va, va + size) to pa with the largest pages (up to 1 GiB) that
// alignment and existing tables allow. `replace`: an existing leaf is
// overwritten (the direct map grows over the boot pass's 4 KiB pages);
// otherwise it is an error. No fence: callers flush once at the end.
static int pt_map(uint64_t root, uint64_t va, uint64_t pa, uint64_t size, uint64_t flags, bool replace)
{
    flags |= PTE_A | (flags & PTE_W ? PTE_D : 0);
    while (size) {
        int level = 2;
        for (; level > 0; level--) {
            uint64_t ps = 1ul << (12 + 9 * level);
            uint64_t* e;
            if (!((va | pa) & (ps - 1)) && size >= ps && (e = pt_entry(root, va, level, true)) && !(*e & PTE_V)) {
                break;
            }
        }
        uint64_t* e = pt_entry(root, va, level, true);
        if (!e || ((*e & PTE_V) && (!(*e & PTE_RWX) || !replace))) {
            return -1;
        }
        *e = pte_make(pa, flags);
        uint64_t ps = 1ul << (12 + 9 * level);
        va += ps;
        pa += ps;
        size -= ps;
    }
    return 0;
}

// The leaf PTE for `va`, 0 if none.
uint64_t pt_lookup(uint64_t root, uint64_t va, int* level)
{
    uint64_t* t = table_at(root);
    for (int l = mmu_levels - 1; l >= 0; l--) {
        uint64_t e = t[vpn(va, l)];
        if (!(e & PTE_V)) {
            return 0;
        }
        if (e & PTE_RWX) {
            if (level) {
                *level = l;
            }
            return e;
        }
        t = table_at(pte_pa(e));
    }
    return 0;
}

uint64_t make_satp(uint64_t root, uint64_t asid)
{
    uint64_t mode = mmu_levels == 4 ? SATP_MODE_SV48 : SATP_MODE_SV39;
    return mode << 60 | asid << 44 | root >> 12;
}

// ---- boot pass (MMU off, physical addresses) ----------------------

// The image at `va`, W^X: text RX, rodata R, the rest (data, bss, the
// page-table pool, the boot stack) RW.
static int map_image(uint64_t root, uint64_t va, uint64_t global)
{
    uint64_t text = kernel_image_pa(__text_end) - kernel_pa;
    uint64_t ro = kernel_image_pa(__rodata_end) - kernel_pa;
    uint64_t end = kernel_image_pa(__kernel_end) - kernel_pa;
    return pt_map(root, va, kernel_pa, text, PTE_R | PTE_X | global, false)
         | pt_map(root, va + text, kernel_pa + text, ro - text, PTE_R | global, false)
         | pt_map(root, va + ro, kernel_pa + ro, end - ro, PTE_R | PTE_W | global, false);
}

// The image twice (identity, for the instructions between the satp
// write and the jump, and at KERNEL_VMA), and the image and the DTB in
// the direct map, so mmu_memory_init can reach the pool and the tree.
static uint64_t build_boot_tables(int levels)
{
    set_layout(levels);
    pool_used = 0;
    uint64_t root = pt_alloc();
    uint64_t size = kernel_image_pa(__kernel_end) - kernel_pa;
    int err = !root || map_image(root, kernel_pa, 0) || map_image(root, KERNEL_VMA, PTE_G)
           || pt_map(root, dmap_base + kernel_pa, kernel_pa, size, PTE_R | PTE_W | PTE_G, false);
    if (!err && boot_dtb_size) {
        uint64_t lo = boot_dtb & ~(PAGE_SIZE - 1);
        uint64_t hi = (boot_dtb + boot_dtb_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        err = pt_map(root, dmap_base + lo, lo, hi - lo, PTE_R | PTE_W | PTE_G, true);
    }
    return err ? 0 : root;
}

// The plan's probe: write satp, read it back. An unsupported MODE
// makes the whole write a no-op (privileged spec, satp), so the read
// shows the old value. If the write takes, translation is on for the
// next few instructions, which is why the tables identity-map the
// image; satp goes back to bare before anything else runs.
static uint64_t satp_try(uint64_t satp)
{
    uint64_t got;
    __asm__ volatile("csrw satp, %1\n\tsfence.vma\n\tcsrr %0, satp\n\tcsrw satp, zero\n\tsfence.vma"
                     : "=&r"(got) : "r"(satp) : "memory");
    return got;
}

static uint32_t be32_at(uint64_t pa)
{
    const volatile uint8_t* p = (const volatile uint8_t*)pa;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// The plan's board rule: Sv39 only on the JH7110 (its U74 cores
// have no Sv48), whatever the probe would say. The parser runs on the
// tree's physical address here; it is position independent.
static bool sv39_only(void)
{
    struct fdt f;
    return boot_dtb && fdt_init(&f, (const void*)boot_dtb, boot_dtb_size) == 0
        && fdt_node_is_compatible(&f, fdt_path_offset(&f, "/"), "starfive,jh7110");
}

uint64_t mmu_boot(uint64_t dtb_pa)
{
    kernel_pa = (uint64_t)__kernel_start;   // PC-relative: the load address
    kernel_va_off = KERNEL_VMA - kernel_pa;
    if (dtb_pa && be32_at(dtb_pa) == FDT_MAGIC) {
        boot_dtb = dtb_pa;
        boot_dtb_size = be32_at(dtb_pa + 4);
    }
    sv39_forced = sv39_only();
    for (int levels = sv39_forced ? 3 : 4; levels >= 3; levels--) {
        uint64_t root = build_boot_tables(levels);
        if (!root) {
            return 0;
        }
        uint64_t satp = make_satp(root, 0);
        uint64_t got = satp_try(satp);
        if (levels == 4) {
            sv48_readback = got;
        }
        if (got == satp) {
            kernel_root = root;
            return satp;
        }
    }
    return 0;
}

// ---- after the switch -------------------------------------------

void mmu_init(void)
{
    pt_virt_off = dmap_base;
    // The identity map is the only thing in the lower half.
    uint64_t* root = table_at(kernel_root);
    for (int i = 0; i < 256; i++) {
        root[i] = 0;
    }
    sfence_vma();

    // ASIDLEN: write ones into the field, count the bits that stick.
    uint64_t satp = csr_read(satp);
    csr_write(satp, satp | 0xfffful << 44);
    uint64_t asids = (csr_read(satp) >> 44) & 0xffff;
    csr_write(satp, satp);
    sfence_vma();
    for (asid_bits = 0; asids & 1; asids >>= 1) {
        asid_bits++;
    }

    if (sv39_forced) {
        console_printf("mmu: sv39 only on starfive,jh7110, sv48 not probed\n");
    } else {
        console_printf("mmu: satp probe: sv48 %s (read back 0x%lx)\n",
                       mmu_levels == 4 ? "accepted" : "rejected, sv39", sv48_readback);
    }
    console_printf("mmu: %s, %d levels, root 0x%lx\n", mmu_levels == 4 ? "sv48" : "sv39", mmu_levels, kernel_root);
    console_printf("mmu: kernel 0x%lx at 0x%lx, direct map 0x%lx, mmio 0x%lx, user below 0x%lx\n",
                   KERNEL_VMA, kernel_pa, dmap_base, mmio_base, user_end);
    console_printf("mmu: identity map removed, %d low-half root entries\n", mmu_kernel_low_entries());
    console_printf("mmu: asid %u bits\n", asid_bits);
}

int mmu_kernel_low_entries(void)
{
    uint64_t* root = table_at(kernel_root);
    int n = 0;
    for (int i = 0; i < 256; i++) {
        n += (root[i] & PTE_V) != 0;
    }
    return n;
}

uint64_t mmu_kernel_pte(uint64_t va, int* level)
{
    return pt_lookup(kernel_root, va, level);
}

static unsigned long count_leaves(uint64_t table, int level, uint64_t flags)
{
    uint64_t* t = table_at(table);
    unsigned long n = 0;
    for (int i = 0; i < 512; i++) {
        if (!(t[i] & PTE_V)) {
            continue;
        }
        if (t[i] & PTE_RWX) {
            n += (t[i] & flags) == flags;
        } else if (level > 0) {
            n += count_leaves(pte_pa(t[i]), level - 1, flags);
        }
    }
    return n;
}

unsigned long mmu_kernel_leaves(uint64_t flags)
{
    return count_leaves(kernel_root, mmu_levels - 1, flags);
}

static void reserve(uint64_t base, uint64_t size, uint32_t type, const char* what)
{
    if (nregions < MAX_REGIONS && size) {
        regions[nregions++] = (struct mem_region){ base, size, type };
        if (type != MEM_REGION_USABLE) {
            console_printf("pfa: reserve 0x%lx-0x%lx %s\n", base, base + size, what);
        }
    }
}

// The header's memory reservation block (/memreserve/), then every
// enabled /reserved-memory child with a reg (OpenSBI's
// mmode_resv nodes for its PMP-protected image), through the shared
// fdt helpers the aarch64 MMU work added. A dynamic node has no address to reserve.
// A no-map node is reserved like the rest but stays in the direct map:
// pt_map cannot split the megapages around it yet.
static void reserve_from_fdt(const struct fdt* f)
{
    struct fdt_region r;
    for (int i = 0; fdt_memreserve(f, i, &r) == 0; i++) {
        reserve(r.base, r.size, MEM_REGION_RESERVED, "/memreserve/");
    }
    struct fdt_reserved rsv[16];
    int n = fdt_reserved_memory(f, rsv, 16);
    if (n > 16) {
        console_printf("mmu: %d /reserved-memory entries, only 16 reserved\n", n);
        n = 16;
    }
    for (int i = 0; i < n; i++) {
        if (!(rsv[i].flags & FDT_RESERVED_DYNAMIC)) {
            reserve(rsv[i].base, rsv[i].size, MEM_REGION_RESERVED, fdt_node_name(f, rsv[i].node));
        }
    }
}

// A /chosen property of one or two cells (QEMU writes the initrd
// bounds as 64-bit values), 0 when absent.
static uint64_t chosen_u64(const struct fdt* f, const char* name)
{
    uint32_t len = 0;
    int node = fdt_path_offset(f, "/chosen");
    const uint8_t* p = node < 0 ? 0 : fdt_getprop(f, node, name, &len);
    uint64_t v = 0;
    for (uint32_t i = 0; p && (len == 4 || len == 8) && i < len; i++) {
        v = v << 8 | p[i];
    }
    return v;
}

int mmu_memory_init(const struct fdt* f)
{
    struct fdt_region mem[8];
    uint64_t ram_lo = ~0ul, ram_hi = 0;
    int n = fdt_memory_regions(f, mem, 8);
    for (int i = 0; i < n && i < 8; i++) {
        uint64_t lo = (mem[i].base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t hi = (mem[i].base + mem[i].size) & ~(PAGE_SIZE - 1);
        if (hi <= lo || hi > dmap_size
            || pt_map(kernel_root, dmap_base + lo, lo, hi - lo, PTE_R | PTE_W | PTE_G, true) != 0) {
            console_printf("mmu: cannot direct-map 0x%lx size 0x%lx\n", mem[i].base, mem[i].size);
            return -1;
        }
        reserve(lo, hi - lo, MEM_REGION_USABLE, "memory");
        ram_lo = lo < ram_lo ? lo : ram_lo;
        ram_hi = hi > ram_hi ? hi : ram_hi;
    }
    // The image's text and rodata are read-only in the direct map
    // too (as on x86; memtest checks it). The boot pass mapped the image
    // there in 4 KiB pages, which the loop above kept and replaced.
    if (pt_map(kernel_root, dmap_base + kernel_pa, kernel_pa, kernel_image_pa(__rodata_end) - kernel_pa,
               PTE_R | PTE_G, true) != 0) {
        console_printf("mmu: cannot protect the image in the direct map\n");
        return -1;
    }
    sfence_vma();
    console_printf("mmu: direct map of %d region(s), %u boot page tables used\n", n, pool_used);
    if (!ram_hi) {
        console_printf("pfa: no usable memory\n");
        return -1;
    }

    reserve_from_fdt(f);
    reserve(kernel_pa, kernel_image_pa(__image_end) - kernel_pa, MEM_REGION_KERNEL, "kernel image");
    reserve(kernel_image_pa(__pt_pool_start), __pt_pool_end - __pt_pool_start, MEM_REGION_KERNEL, "boot page tables");
    reserve(kernel_image_pa(__stack_start), __kernel_end - __stack_start, MEM_REGION_KERNEL, "boot stack");
    reserve(boot_dtb, boot_dtb_size, MEM_REGION_KERNEL, "device tree");
    // QEMU's -initrd, the RAM disk the shared kernel boots from.
    initrd_base = chosen_u64(f, "linux,initrd-start");
    uint64_t end = chosen_u64(f, "linux,initrd-end");
    initrd_size = end > initrd_base ? end - initrd_base : 0;
    reserve(initrd_base, initrd_size, MEM_REGION_KERNEL, "initrd");
    // A framebuffer U-Boot left on (scanned out from RAM) is not
    // the frame allocator's either.
    struct fdt_simplefb fb;
    if (fdt_find_simplefb(f, &fb) == 0) {
        reserve(fb.base, fb.size, MEM_REGION_RESERVED, "simple-framebuffer");
    }

    // The root-level entries of the mmio window and of the heap arena
    // over RAM, then no new ones: every address space copies the upper
    // half's root entries once. The tables below them come later.
    uint64_t span = 1ul << (12 + 9 * (mmu_levels - 1));
    if (!pt_entry(kernel_root, mmio_base, mmu_levels - 2, true)) {
        return -1;
    }
    for (uint64_t va = (arena_base + ram_lo) & ~(span - 1); va < arena_base + ram_hi; va += span) {
        if (!pt_entry(kernel_root, va, mmu_levels - 2, true)) {
            return -1;
        }
    }
    sealed = true;
    return 0;
}

int mmu_initrd(uint64_t* base, uint64_t* size)
{
    *base = initrd_base;
    *size = initrd_size;
    return initrd_size ? 0 : -1;
}

uint64_t mmu_kernel_root(void)
{
    return kernel_root;
}

const struct mem_region* mmu_regions(int* count)
{
    *count = nregions;
    return regions;
}

uintptr_t mmu_map_mmio(uint64_t pa, uint64_t size)
{
    const uint64_t mega = 1ul << 21;
    uint64_t lo = pa & ~(PAGE_SIZE - 1);
    uint64_t hi = (pa + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    // Same offset within 2 MiB as the registers, so big blocks (the
    // PLIC) get megapages.
    uint64_t va = ((mmio_next + mega - 1) & ~(mega - 1)) + (lo & (mega - 1));
    if (va + (hi - lo) > mmu_scratch_va()
        || pt_map(kernel_root, va, lo, hi - lo, PTE_R | PTE_W | PTE_G, false) != 0) {
        return 0;
    }
    sfence_vma();
    mmio_next = va + (hi - lo);
    return va + (pa - lo);
}

// The mmio window's last page, which mmu_map_mmio leaves out.
uint64_t mmu_scratch_va(void)
{
    return mmio_base + MMIO_WINDOW - PAGE_SIZE;
}

// Satp for the kernel's root, ASID 0 (another hart's start).
uint64_t mmu_kernel_satp(void)
{
    return make_satp(kernel_root, 0);
}

// ---- address spaces -----------------------------------------------

unsigned mmu_asid_bits(void)
{
    return asid_bits;
}

void mmu_disable_asids(void)
{
    asid_bits = 0;
}

int mm_create(struct mm* mm)
{
    uint64_t* t = pfa_alloc_frames(1);
    if (!t || !sealed) {
        return -1;
    }
    uint64_t* k = table_at(kernel_root);
    for (int i = 0; i < 512; i++) {
        t[i] = i < 256 ? 0 : k[i];
    }
    mm->root = virt_to_phys(t);
    // ASID 0 is the kernel root's; when they run out, 0 means "flush".
    // Never recycled yet (65535 address spaces on QEMU). Several
    // harts create address spaces at once, so the counter is atomic.
    uint32_t asid = asid_bits ? __atomic_add_fetch(&next_asid, 1, __ATOMIC_RELAXED) : 0;
    mm->asid = asid < (1u << asid_bits) ? (uint16_t)asid : 0;
    return 0;
}

static void flush_asid(uint16_t asid)
{
    if (asid) {
        __asm__ volatile("sfence.vma zero, %0" : : "r"((uint64_t)asid) : "memory");
    } else {
        sfence_vma();
    }
}

// One page of `mm` (all of its ASID's when there are none).
void mm_flush_page(struct mm* mm, uint64_t va)
{
    if (mm->asid && asid_bits) {
        __asm__ volatile("sfence.vma %0, %1" : : "r"(va), "r"((uint64_t)mm->asid) : "memory");
    } else {
        sfence_vma();
    }
}

// The tables under one lower-half root entry, `level` being the
// level of the table's own entries. Leaves are the process's pages,
// which process.c frees itself.
static void free_tables(uint64_t table, int level)
{
    uint64_t* t = table_at(table);
    for (int i = 0; level > 0 && i < 512; i++) {
        if ((t[i] & PTE_V) && !(t[i] & PTE_RWX)) {
            free_tables(pte_pa(t[i]), level - 1);
        }
    }
    pfa_free_frames(t);
}

void mm_destroy(struct mm* mm)
{
    if (mm_current() == mm) {
        mm_switch(0);
    }
    uint64_t* t = table_at(mm->root);
    for (int i = 0; i < 256; i++) {
        if ((t[i] & PTE_V) && !(t[i] & PTE_RWX)) {
            free_tables(pte_pa(t[i]), mmu_levels - 2);
        }
    }
    // No TLB entry of this ASID may outlive its tables.
    flush_asid(asid_bits ? mm->asid : 0);
    pfa_free_frames(t);
    mm->root = 0;
}

int mm_map(struct mm* mm, uint64_t va, uint64_t pa, uint64_t size, uint64_t prot)
{
    prot &= PTE_RWX;
    if ((va | pa | size) & (PAGE_SIZE - 1) || !size || va >= user_end || size > user_end - va
        || !(prot & (PTE_R | PTE_X)) || (prot & (PTE_R | PTE_W)) == PTE_W) {
        return -1;
    }
    int err = pt_map(mm->root, va, pa, size, prot | PTE_U, false);
    // Invalid to valid needs a fence too (no Svvptc assumed).
    flush_asid(mm->asid);
    return err;
}

void mm_switch(struct mm* mm)
{
    uint64_t asid = mm && asid_bits ? mm->asid : 0;
    csr_write(satp, make_satp(mm ? mm->root : kernel_root, asid));
    if (!asid) {
        sfence_vma();
    }
    this_cpu()->arch.mm = mm;
}

// This hart's.
struct mm* mm_current(void)
{
    return this_cpu()->arch.mm;
}
