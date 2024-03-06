// MMU test hooks (main.c, bootargs):
//   samos.mmu_test    probe each mapping kind with AT instructions, run
//                     the PFA over all free RAM, two user address spaces
//   samos.fault_test=textwrite|dataexec|ident|wxn|uexec
//                     one abort each, dumped by trap.c (mmu_fault_test)
// tests64/arch/aarch64-mmu.sh checks the lines.
#include "arch/aarch64/console.h"
#include "arch/aarch64/memmap.h"
#include "arch/aarch64/mmu.h"
#include "arch/aarch64/pfa.h"
#include "arch/aarch64/pl011.h"

#define USER_VA     0x400000ul
#define SCRATCH_VA  (KERNEL_VBASE + 0x40000000ul)
#define INSN_RET    0xd65f03c0u
#define PFA_TEST_RUNS 64

void fault_test_store(uint64_t addr, uint64_t value);
void fault_test_dabort(uint64_t addr);
void fault_test_call(uint64_t addr);
void fault_test_brk(void);
void mmu_test(const struct fdt* f);
int  mmu_fault_test(const char* kind);

extern char _text_end[], _stack_guard[];

static uint32_t data_ret[2] = { INSN_RET, 0 };     // .data: RW, never executable
static uint64_t bss_word;

static int str_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

// One line per mapping: what AT S1E1R/W say, plus EL1 execute from
// the descriptor (PXN clear and, with SCTLR_EL1.WXN, not writable).
static void probe(const char* name, uint64_t va)
{
    uint64_t par = mmu_at(va, AT_S1E1R);
    if (par & 1) {
        console_printf("probe: %s 0x%lx: fault, FST 0x%lx\n", name, va, (par >> 1) & 0x3f);
        return;
    }
    int level = 0;
    uint64_t d = mmu_lookup(va, &level);
    int w = !(mmu_at(va, AT_S1E1W) & 1);
    int x = !(d & PTE_PXN) && !w;
    console_printf("probe: %s 0x%lx: pa 0x%lx, L%d, R%c%c, attr 0x%lx\n", name, va,
                   (par & PTE_ADDR) | (va & 0xfff), level, w ? 'W' : '-', x ? 'X' : '-', par >> 56);
}

static void probe_layout(const struct fdt* f)
{
    uint64_t local = 0;
    uint64_t frame = early_pfa_alloc();
    probe("text", (uint64_t)probe);
    probe("rodata", (uint64_t)_text_end);
    probe("data", (uint64_t)data_ret);
    probe("bss", (uint64_t)&bss_word);
    probe("stack", (uint64_t)&local);
    probe("guard", (uint64_t)_stack_guard);
    probe("directmap", (uint64_t)phys_to_virt(frame));
    probe("dtb", (uint64_t)f->blob);
    probe("uart", DIRECT_MAP_BASE + pl011_base());
    probe("identity", kernel_phys);
    early_pfa_free(frame);
    int n;
    const struct mem_region* m = memmap_regions(&n);
    for (int i = 0; i < n; i++)
        if (m[i].type & MEMMAP_NOMAP)
            probe("nomap", DIRECT_MAP_BASE + m[i].base);
}

// Is pa in a frame the allocator must never hand out?
static int must_not_allocate(uint64_t pa)
{
    int n, in_ram = 0;
    const struct mem_region* m = memmap_regions(&n);
    for (int i = 0; i < n; i++) {
        uint64_t s = m[i].base & ~(PAGE_SIZE - 1), e = m[i].base + m[i].length;
        if (pa + PAGE_SIZE <= s || pa >= e)
            continue;
        if (m[i].type != MEM_REGION_USABLE)
            return 1;
        in_ram = 1;
    }
    uint64_t t = early_pfa_table_pa();
    return !in_ram || (pa >= t && pa < t + early_pfa_table_bytes());
}

// Take every free frame, check it is outside every reservation and
// writable at its direct-map address (AT S1E1W, so QEMU need not back
// all of RAM with host memory), then give them all back. Frames come
// out in address order, so a few runs record them; a frame handed out
// twice would be a refused second free.
static void pfa_test(void)
{
    static uint64_t run_pa[PFA_TEST_RUNS], run_len[PFA_TEST_RUNS];
    uint64_t before = early_pfa_free_frames(), n = 0, bad = 0, unmapped = 0, lost = 0, pa;
    int runs = 0;
    while ((pa = early_pfa_alloc())) {
        n++;
        bad += must_not_allocate(pa);
        uint64_t par = mmu_at((uint64_t)phys_to_virt(pa), AT_S1E1W);
        unmapped += (par & 1) || (par & PTE_ADDR) != pa;
        if (runs && run_pa[runs - 1] + run_len[runs - 1] * PAGE_SIZE == pa)
            run_len[runs - 1]++;
        else if (runs < PFA_TEST_RUNS)
            run_pa[runs] = pa, run_len[runs++] = 1;
        else
            lost++;
    }
    uint64_t refused = 0;
    for (int i = 0; i < runs; i++)
        for (uint64_t k = 0; k < run_len[i]; k++)
            refused += early_pfa_free(run_pa[i] + k * PAGE_SIZE) != 0;
    console_printf("pfa test: allocated %lu of %lu free frames in %d runs (%lu not recorded)\n", n, before, runs,
                   lost);
    console_printf("pfa test: %lu in reserved memory, %lu not writable through the direct map\n", bad, unmapped);
    console_printf("pfa test: %lu frees refused, %lu free again\n", refused, early_pfa_free_frames());
    pa = early_pfa_alloc();
    int first = early_pfa_free(pa), second = early_pfa_free(pa);
    console_printf("pfa test: free %d, double free %d, free of the kernel %d, free of the table %d\n", first,
                   second, early_pfa_free(kernel_phys), early_pfa_free(early_pfa_table_pa()));
}

static const char* fault_name(uint64_t par)
{
    uint64_t fst = (par >> 1) & 0x3f;
    if (!(par & 1))
        return "ok";
    if (fst >= 0x04 && fst <= 0x07)
        return "translation fault";
    if (fst >= 0x0c && fst <= 0x0f)
        return "permission fault";
    return "other fault";
}

static void aspace_test(void)
{
    uint64_t free0 = early_pfa_free_frames(), tables0 = mmu_table_frames();
    uint64_t fa = early_pfa_alloc(), fb = early_pfa_alloc(), fr = early_pfa_alloc();
    *(uint64_t*)phys_to_virt(fa) = 0xaaaaaaaa;
    *(uint64_t*)phys_to_virt(fb) = 0xbbbbbbbb;
    struct aspace a, b;
    if (aspace_create(&a) < 0 || aspace_create(&b) < 0 ||
        aspace_map(&a, USER_VA, fa, USER_R | USER_W) < 0 || aspace_map(&a, USER_VA + PAGE_SIZE, fr, USER_R) < 0 ||
        aspace_map(&b, USER_VA, fb, USER_R | USER_W) < 0) {
        console_printf("aspace: setup failed\n");
        return;
    }
    console_printf("aspace: A asid %u, B asid %u, 0x%lx -> 0x%lx in A, 0x%lx in B\n", a.asid, b.asid, USER_VA,
                   fa, fb);
    console_printf("aspace: map of a kernel address refused: %d\n", aspace_map(&a, KERNEL_VBASE, fa, USER_R));

    aspace_switch(&a);
    uint64_t d = mmu_lookup(USER_VA, 0);
    console_printf("aspace: A user page descriptor nG %lu AP[1] %lu AP[2] %lu PXN %lu UXN %lu\n",
                   d >> 11 & 1, d >> 6 & 1, d >> 7 & 1, d >> 53 & 1, d >> 54 & 1);
    console_printf("aspace: A reads 0x%lx through 0x%lx\n", *(volatile uint64_t*)USER_VA, USER_VA);
    uint64_t par = mmu_at(USER_VA, AT_S1E0R);
    console_printf("aspace: A EL0 read 0x%lx: %s, pa 0x%lx\n", USER_VA, fault_name(par), par & PTE_ADDR);
    console_printf("aspace: A EL0 write 0x%lx (read-only page): %s\n", USER_VA + PAGE_SIZE,
                   fault_name(mmu_at(USER_VA + PAGE_SIZE, AT_S1E0W)));
    console_printf("aspace: A EL0 read of kernel text: %s\n", fault_name(mmu_at((uint64_t)probe, AT_S1E0R)));

    aspace_switch(&b);
    console_printf("aspace: B reads 0x%lx through 0x%lx\n", *(volatile uint64_t*)USER_VA, USER_VA);
    *(volatile uint64_t*)USER_VA = 0xb0b0b0b0;
    console_printf("aspace: B wrote through 0x%lx, frame holds 0x%lx\n", USER_VA, *(uint64_t*)phys_to_virt(fb));
    aspace_switch(&a);      // no TLB flush: the ASID keeps A's entries apart
    console_printf("aspace: back on A, reads 0x%lx\n", *(volatile uint64_t*)USER_VA);
    aspace_switch(0);
    console_printf("aspace: no user space, EL1 read 0x%lx: %s\n", USER_VA,
                   fault_name(mmu_at(USER_VA, AT_S1E1R)));

    aspace_destroy(&a);
    aspace_destroy(&b);
    early_pfa_free(fa);
    early_pfa_free(fb);
    early_pfa_free(fr);
    console_printf("aspace: destroyed, %lu free frames (was %lu), %lu table frames (was %lu)\n",
                   early_pfa_free_frames(), free0, mmu_table_frames(), tables0);
}

void mmu_test(const struct fdt* f)
{
    probe_layout(f);
    pfa_test();
    aspace_test();
}

// An executable copy of `ret` in a fresh frame.
static uint64_t ret_frame(void)
{
    uint64_t pa = early_pfa_alloc();
    *(uint32_t*)phys_to_virt(pa) = INSN_RET;
    return pa;
}

int mmu_fault_test(const char* kind)
{
    if (str_eq(kind, "textwrite")) {
        console_printf("fault test: store to kernel text 0x%lx\n", (uint64_t)fault_test_brk);
        fault_test_store((uint64_t)fault_test_brk, 0);
    } else if (str_eq(kind, "dataexec")) {
        console_printf("fault test: call into .data 0x%lx\n", (uint64_t)data_ret);
        fault_test_call((uint64_t)data_ret);
    } else if (str_eq(kind, "ident")) {
        console_printf("fault test: load from the old identity address 0x%lx\n", kernel_phys);
        fault_test_dabort(kernel_phys);
    } else if (str_eq(kind, "wxn")) {
        // Writable and PXN clear: only SCTLR_EL1.WXN stops the fetch.
        mmu_map_kernel(SCRATCH_VA, ret_frame(), PAGE_SIZE, PTE_ATTR(MT_NORMAL) | PTE_SH_INNER | PTE_UXN);
        uint64_t d = mmu_lookup(SCRATCH_VA, 0);
        console_printf("fault test: call into a W+X page 0x%lx (AP[2] %lu, PXN %lu)\n", SCRATCH_VA,
                       d >> 7 & 1, d >> 53 & 1);
        fault_test_call(SCRATCH_VA);
    } else if (str_eq(kind, "uexec")) {
        // EL0 executable (UXN clear), so only PXN stops EL1.
        static struct aspace as;
        if (aspace_create(&as) < 0 || aspace_map(&as, USER_VA, ret_frame(), USER_R | USER_X) < 0)
            return -1;
        aspace_switch(&as);
        uint64_t d = mmu_lookup(USER_VA, 0);
        console_printf("fault test: call into a user page 0x%lx (UXN %lu, PXN %lu)\n", USER_VA, d >> 54 & 1,
                       d >> 53 & 1);
        fault_test_call(USER_VA);
    } else {
        return -1;
    }
    return 0;
}
