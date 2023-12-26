// MMU, frame allocator and uaccess tests. See mmu_test.h; the
// expected lines are in tests64/arch/riscv64-mmu.sh.

#include <stdbool.h>
#include "arch/riscv64/mmu_test.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/pfa.h"
#include "arch/riscv64/riscv64.h"
#include "arch/riscv64/timer.h"
#include "arch/riscv64/trap.h"
#include "arch/riscv64/uaccess.h"
#include "status.h"
#include "string/string.h"

#define UVA 0x10000ul   // a's pages: UVA rw, UVA + 0x1000 r, UVA + 0x2000 unmapped

static const char rodata_probe[] = "rodata";
static uint32_t data_probe[2] = { 0x00008067, 0x00008067 };   // ret, ret
static uint64_t bss_probe;
static char big[4096];

// "rwxug" with '-' for each clear bit; "unmapped" for no leaf.
static const char* prot(uint64_t va)
{
    static char s[6];
    uint64_t pte = mmu_kernel_pte(va, 0);
    if (!pte) {
        return "unmapped";
    }
    const char* bits = "rwxug";
    for (int i = 0; i < 5; i++) {
        s[i] = pte & (PTE_R << i) ? bits[i] : '-';
    }
    s[5] = 0;
    return s;
}

void mmu_report(uintptr_t uart_va)
{
    uint64_t stack = (uint64_t)&stack;
    console_printf("mmu: text %s", prot((uint64_t)mmu_report));
    console_printf(" rodata %s", prot((uint64_t)rodata_probe));
    console_printf(" data %s", prot((uint64_t)data_probe));
    console_printf(" bss %s", prot((uint64_t)&bss_probe));
    console_printf(" stack %s\n", prot(stack));
    if (uart_va) {
        console_printf("mmu: uart 0x%lx %s\n", uart_va, prot(uart_va));
    }
    console_printf("mmu: direct map 0x%lx %s\n", dmap_base + mmu_kernel_pa(), prot(dmap_base + mmu_kernel_pa()));
    console_printf("mmu: %lu writable+executable leaves, %lu user leaves in the kernel tables\n",
                   mmu_kernel_leaves(PTE_W | PTE_X), mmu_kernel_leaves(PTE_U));
}

// ---- frame allocator ---------------------------------------------

static bool in_region(uint64_t pa, const struct mem_region* r)
{
    return pa >= r->base && pa + PFA_FRAME_SIZE <= r->base + r->length;
}

static bool overlaps(uint64_t pa, const struct mem_region* r)
{
    return pa < r->base + r->length && r->base < pa + PFA_FRAME_SIZE;
}

// Every free frame once: inside /memory, outside every reservation,
// and writable through the direct map (a frame in OpenSBI's PMP
// region would fault here). The frames hold a list of themselves, so
// they free without a side table.
void pfa_test(void)
{
    int nr;
    const struct mem_region* r = mmu_regions(&nr);
    size_t free0 = pfa_free_frame_count(), n = 0, bad = 0;
    uint64_t* head = 0;
    uint64_t* p;
    while ((p = pfa_alloc_frames(1)) != 0) {
        uint64_t pa = virt_to_phys(p);
        bool usable = false;
        for (int i = 0; i < nr; i++) {
            if (r[i].type == MEM_REGION_USABLE) {
                usable = usable || in_region(pa, &r[i]);
            } else if (overlaps(pa, &r[i])) {
                bad++;
            }
        }
        bad += !usable;
        p[0] = (uint64_t)head;
        p[1] = pa;
        head = p;
        n++;
    }
    console_printf("pfa test: %lu frames allocated (%lu were free), %lu outside memory or reserved\n", n, free0, bad);
    size_t ok = 0, freed = 0;
    while (head) {
        uint64_t* next = (uint64_t*)head[0];
        ok += head[1] == virt_to_phys(head);
        freed += pfa_free_frames(head) == 0;
        head = next;
    }
    console_printf("pfa test: %lu read back, %lu freed, %lu free now\n", ok, freed, pfa_free_frame_count());

    uint8_t* run = pfa_alloc_frames(64);
    bool contiguous = run != 0;
    for (int i = 0; run && i < 64; i++) {
        contiguous = contiguous && !pfa_is_reserved(virt_to_phys(run) + i * PFA_FRAME_SIZE);
    }
    int f1 = pfa_free_frames(run), f2 = pfa_free_frames(run);
    console_printf("pfa test: run of 64 at 0x%lx %s, free %d, again %d, %lu free now\n",
                   run ? virt_to_phys(run) : 0, contiguous ? "ok" : "bad", f1, f2, pfa_free_frame_count());
}

// ---- user address spaces and uaccess -------------------------------

static uint64_t fill_page(const char* s, char c)
{
    char* p = pfa_alloc_frames(1);
    if (!p) {
        return 0;
    }
    for (int i = 0; i < 4096; i++) {
        p[i] = c;
    }
    strcpy(p, s);
    return virt_to_phys(p);
}

static unsigned nonzero_prefix(const char* s, unsigned max)
{
    unsigned n = 0;
    while (n < max && s[n]) {
        n++;
    }
    return n;
}

void uaccess_test(void)
{
    struct mm a, b;
    uint64_t rw = fill_page("user a", 0), ro = fill_page("", 'r'), bp = fill_page("user b", 0);
    // A string that runs off the end of the read-only page.
    char* ro_k = phys_to_virt(ro);
    ro_k[0] = 'r';
    strcpy(ro_k + 4092, "abc");
    ro_k[4095] = 'd';
    strcpy((char*)phys_to_virt(rw) + 512, "hello");
    if (!rw || !ro || !bp || mm_create(&a) || mm_create(&b)
        || mm_map(&a, UVA, rw, 4096, PTE_R | PTE_W) || mm_map(&a, UVA + 0x1000, ro, 4096, PTE_R)
        || mm_map(&b, UVA, bp, 4096, PTE_R | PTE_W)) {
        console_printf("uaccess: setup failed\n");
        return;
    }
    console_printf("uaccess: a: 0x%lx rw, 0x%lx r, 0x%lx unmapped; b: 0x%lx rw; asid a %u b %u (%u bits)\n",
                   UVA, UVA + 0x1000, UVA + 0x2000, UVA, a.asid, b.asid, mmu_asid_bits());
    mm_switch(&a);
    char buf[64];
    int r = copy_from_user(buf, (void*)UVA, 7);
    console_printf("uaccess: read %d '%s'\n", r, r ? "" : buf);

    r = copy_to_user((void*)(UVA + 256), "written by the kernel", 22);
    console_printf("uaccess: write %d, page holds '%s'\n", r, (char*)phys_to_virt(rw) + 256);

    uint64_t fx = trap_fixup_count();
    r = copy_to_user((void*)(UVA + 0x1000), "x", 1);
    console_printf("uaccess: write to the read-only page %d, %lu fault(s), scause 0x%lx, page holds '%c'\n",
                   r, trap_fixup_count() - fx, trap_fixup_last_cause(), ro_k[0]);

    for (int i = 0; i < 4096; i++) {
        big[i] = 0;
    }
    fx = trap_fixup_count();
    r = copy_from_user(big, (void*)(UVA + 0x1800), 4096);
    console_printf("uaccess: read across into the unmapped page %d after %u bytes, %lu fault(s), scause 0x%lx\n",
                   r, nonzero_prefix(big, 4096), trap_fixup_count() - fx, trap_fixup_last_cause());

    fx = trap_fixup_count();
    int k1 = copy_from_user(buf, (void*)KERNEL_VMA, 8);
    int k2 = copy_to_user(phys_to_virt(rw), "x", 1);
    int k3 = copy_from_user(buf, (void*)(user_end - 8), 16);
    int k4 = copy_from_user(buf, (void*)UVA, ~0ul - UVA + 2);
    console_printf("uaccess: kernel image %d, direct map %d, past user_end %d, wrapping %d, %lu fault(s)\n",
                   k1, k2, k3, k4, trap_fixup_count() - fx);

    long l1 = strncpy_from_user(buf, (void*)(UVA + 512), sizeof(buf));
    console_printf("uaccess: strncpy %ld '%s'\n", l1, buf);
    long l2 = strncpy_from_user(buf, (void*)(UVA + 512), 4);
    console_printf("uaccess: strncpy into 4 bytes %ld '%s'\n", l2, buf);
    long l3 = strncpy_from_user(buf, (void*)(UVA + 0x1000 + 4092), sizeof(buf));
    long l4 = strncpy_from_user(buf, (void*)KERNEL_VMA, sizeof(buf));
    console_printf("uaccess: strncpy into the unmapped page %ld, from the kernel %ld\n", l3, l4);

    // SUM is set inside the copies only; without it the hardware
    // refuses a kernel access to a U page.
    console_printf("uaccess: sstatus.SUM after the copies %lu\n", (csr_read(sstatus) & SSTATUS_SUM) >> 18);
    fx = trap_fixup_count();
    size_t left = uaccess_copy_bytes(buf, (void*)UVA, 1);
    console_printf("uaccess: user page without SUM: %lu byte(s) left, %lu fault(s), scause 0x%lx\n",
                   left, trap_fixup_count() - fx, trap_fixup_last_cause());
    // Ticks while SUM is set (as when a copy is interrupted):
    // trap_entry clears it for the handler, sret restores it.
    uint64_t f0, h0, f1, h1;
    trap_sum_counts(&f0, &h0);
    csr_set(sstatus, SSTATUS_SUM);
    uint64_t t = timer_ticks();
    while (timer_ticks() < t + 2) {
        wfi();
    }
    uint64_t still = (csr_read(sstatus) & SSTATUS_SUM) != 0;
    csr_clear(sstatus, SSTATUS_SUM);
    trap_sum_counts(&f1, &h1);
    console_printf("uaccess: ticks during SUM: %s, %lu handler(s) ran with SUM, SUM after sret %lu\n",
                   f1 - f0 >= 2 ? "taken" : "missing", h1 - h0, still);

    // Same address, other address space, and back.
    mm_switch(&b);
    int rb = copy_from_user(buf, (void*)UVA, 7);
    console_printf("uaccess: mm b reads %d '%s'", rb, rb ? "" : buf);
    mm_switch(&a);
    int ra = copy_from_user(buf, (void*)UVA, 7);
    console_printf(", mm a again %d '%s'\n", ra, ra ? "" : buf);
    mm_switch(0);
    r = copy_from_user(buf, (void*)UVA, 1);
    console_printf("uaccess: kernel root %d\n", r);
}

// ---- fatal faults --------------------------------------------------

// `which` is the rest of the command line: match one word.
static bool is(const char* which, const char* word)
{
    int n = strlen(word);
    return strncmp(which, word, n) == 0 && (which[n] == 0 || which[n] == ' ');
}

void mmu_fault_test(const char* which)
{
    if (is(which, "wtext")) {
        console_printf("mmu fault test: store to text 0x%lx at 0x%lx\n", (uint64_t)mmu_fault_test,
                       (uint64_t)trap_test_store);
        trap_test_store((uint64_t)mmu_fault_test);
    } else if (is(which, "wrodata")) {
        console_printf("mmu fault test: store to rodata 0x%lx at 0x%lx\n", (uint64_t)rodata_probe,
                       (uint64_t)trap_test_store);
        trap_test_store((uint64_t)rodata_probe);
    } else if (is(which, "xdata")) {
        console_printf("mmu fault test: execute data 0x%lx at 0x%lx\n", (uint64_t)data_probe, (uint64_t)data_probe);
        ((void (*)(void))data_probe)();
    } else if (is(which, "ident")) {
        console_printf("mmu fault test: load from the old identity map 0x%lx at 0x%lx\n", mmu_kernel_pa(),
                       (uint64_t)trap_test_load);
        trap_test_load(mmu_kernel_pa());
    }
    console_printf("mmu fault test: no fault\n");
}
