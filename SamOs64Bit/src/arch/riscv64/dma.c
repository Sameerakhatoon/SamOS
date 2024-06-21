// DMA cache maintenance on riscv64: arch_impl.h's arch_dma_*.
//
// Whether devices snoop the harts' caches is the device tree's word: a
// bus with dma-noncoherent (Linux's binding) holds devices that do
// not, and the plan says the JH7110's do not. samos.dma_noncoherent
// forces it for the tests (QEMU's devices always see memory, so there
// the paths run but stale data cannot show).
//
// Two ways to reach memory past the caches, by cache block:
//   - Zicbom, when the boot hart's ISA lists it: cbo.clean, cbo.inval,
//     cbo.flush on each block of riscv,cbom-block-size bytes (64 if
//     absent).
//   - A SiFive composable cache (sifive,ccache0; the JH7110's U74 has
//     no Zicbom): a physical address written to its Flush64 register
//     (offset 0x200) writes that line back and drops it. The L2 is
//     inclusive of the L1 data caches (SiFive's FU540 / FU740 manuals),
//     so the line leaves those too. It has no clean or invalidate of
//     its own: all three are a flush. Linux did the same on the JH7100.
// Neither: a line says so and nothing is done.

#include "arch/arch.h"
#include "arch/riscv64/console.h"
#include "arch/riscv64/riscv64.h"
#include "boot/cmdline.h"
#include "cpu/percpu.h"
#include "firmware/fdt_dev.h"
#include "memory/heap/kheap.h"

#define DMA_ZICBOM          1
#define DMA_CCACHE          2
#define CCACHE_FLUSH64      0x200

int riscv64_dma_mode;
static uint64_t dma_line = 64;
static volatile uint8_t* ccache;

static inline void cbo(uintptr_t a, int op)
{
    if (op == RISCV64_DMA_CLEAN) {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 1" : : "r"(a) : "memory");
    } else if (op == RISCV64_DMA_INVAL) {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 0" : : "r"(a) : "memory");
    } else {
        __asm__ volatile(".insn i 0x0f, 2, x0, %0, 2" : : "r"(a) : "memory");
    }
}

// Whole blocks over [va, va + size): the ones at both ends may hold
// other data too (arch.h says what inval does to it).
void riscv64_dma_op(const volatile void* va, size_t size, int op)
{
    uintptr_t a = (uintptr_t)va & ~(dma_line - 1);
    uintptr_t end = (uintptr_t)va + size;
    __asm__ volatile("fence rw, rw" ::: "memory");
    while (a < end) {
        if (riscv64_dma_mode == DMA_ZICBOM) {
            cbo(a, op);
            a += dma_line;
            continue;
        }
        // The cache takes physical addresses: one translation a page.
        uintptr_t page_end = (a | (PAGING_PAGE_SIZE - 1)) + 1;
        uint64_t pa = paging_kernel_virt_to_phys((void*)a);
        for (; a < end && a < page_end; a += dma_line, pa += dma_line) {
            mmio_write64(ccache + CCACHE_FLUSH64, pa);
        }
    }
    __asm__ volatile("fence rw, rw" ::: "memory");
}

// A page written, cleaned, invalidated and read back: the instructions
// or the register work here, and a clean before an invalidate keeps
// the data.
static bool dma_selftest(void)
{
    uint8_t* p = kpage_zalloc(4096);
    if (!p) {
        return false;
    }
    for (int i = 0; i < 4096; i++) {
        p[i] = (uint8_t)(i * 7);
    }
    arch_dma_clean(p, 4096);
    arch_dma_inval(p, 4096);
    arch_dma_flush(p + 100, 1000);
    bool ok = true;
    for (int i = 0; i < 4096; i++) {
        ok = ok && p[i] == (uint8_t)(i * 7);
    }
    kfree(p);
    return ok;
}

// Before the drivers (arch_firmware_init).
void riscv64_dma_init(const struct fdt* f)
{
    bool forced = boot_cmdline_has("samos.dma_noncoherent");
    if (!forced && !fdt_dma_noncoherent(f)) {
        console_printf("dma: coherent\n");
        return;
    }
    const char* why = forced ? "samos.dma_noncoherent" : "dma-noncoherent in the device tree";
    int cpu = riscv64_cpu_node(f, this_cpu()->arch.hart_id);
    struct fdt_region reg;
    uint32_t line;
    if (cpu >= 0 && fdt_hart_has_ext(f, cpu, "zicbom")) {
        dma_line = fdt_getprop_u32(f, cpu, "riscv,cbom-block-size", 64);
        riscv64_dma_mode = DMA_ZICBOM;
        console_printf("dma: non-coherent (%s), zicbom, %lu-byte blocks\n", why, dma_line);
    } else if (fdt_find_ccache(f, &reg, &line) == 0
               && (ccache = paging_map_mmio(reg.base, reg.size, PAGING_CACHE_DISABLED))) {
        dma_line = line;
        riscv64_dma_mode = DMA_CCACHE;
        console_printf("dma: non-coherent (%s), cache controller flush64 at 0x%lx, %lu-byte lines\n",
                       why, reg.base + CCACHE_FLUSH64, dma_line);
    } else {
        console_printf("dma: non-coherent (%s), but no zicbom and no cache controller: buffers unmaintained\n",
                       why);
        return;
    }
    if (dma_line < 16 || (dma_line & (dma_line - 1))) {
        dma_line = 64;
    }
    console_printf("dma: self-test %s\n", dma_selftest() ? "ok" : "FAILED");
}
