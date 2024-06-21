// Board devices from the device tree. See fdt_dev.h.

#include "firmware/fdt_dev.h"

static int slen(const char* s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static bool seq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool fdt_node_okay(const struct fdt* f, int node)
{
    const char* s = fdt_getprop_string(f, node, "status");
    return !s || seq(s, "okay") || seq(s, "ok");
}

int fdt_stringlist_index(const struct fdt* f, int node, const char* prop, const char* name)
{
    uint32_t len;
    const char* list = fdt_getprop(f, node, prop, &len);
    if (!list || len == 0 || list[len - 1] != 0)
        return FDT_ERR_NOTFOUND;
    int index = 0;
    for (uint32_t i = 0; i < len; i += slen(list + i) + 1, index++) {
        if (seq(list + i, name))
            return index;
    }
    return FDT_ERR_NOTFOUND;
}

uint32_t fdt_interrupt(const struct fdt* f, int node, int index)
{
    uint32_t len;
    const uint8_t* p = fdt_getprop(f, node, "interrupts", &len);
    if (!p || index < 0 || (uint32_t)(index + 1) * 4 > len)
        return 0;
    p += 4 * index;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// ---- console ----------------------------------------------------

static bool is_uart(const struct fdt* f, int node)
{
    return fdt_node_is_compatible(f, node, "ns16550a") || fdt_node_is_compatible(f, node, "ns16550")
        || fdt_node_is_compatible(f, node, "snps,dw-apb-uart");
}

int fdt_find_uart(const struct fdt* f, struct fdt_uart* out)
{
    int node = fdt_stdout_node(f);
    if (node < 0 || !is_uart(f, node)) {
        for (node = fdt_next_node(f, -1, 0); node >= 0; node = fdt_next_node(f, node, 0)) {
            if (is_uart(f, node) && fdt_node_okay(f, node))
                break;
        }
    }
    struct fdt_region reg;
    if (node < 0 || fdt_reg_cpu(f, node, 0, &reg) != 0)
        return FDT_ERR_NOTFOUND;
    out->node = node;
    out->base = reg.base;
    out->size = reg.size;
    out->reg_shift = fdt_getprop_u32(f, node, "reg-shift", 0);
    out->io_width = fdt_getprop_u32(f, node, "reg-io-width", 1);
    return 0;
}

// ---- harts --------------------------------------------------------

// The base string's letters: "rv64imafdc" from "rv64imafdc_zicsr...".
static bool base_has(const char* isa, char c)
{
    if (slen(isa) < 4)
        return false;
    for (const char* s = isa + 4; *s && *s != '_'; s++) {
        if (*s == c)
            return true;
    }
    return false;
}

bool fdt_hart_has_ext(const struct fdt* f, int node, const char* ext)
{
    const char* isa = fdt_getprop_string(f, node, "riscv,isa");
    if (isa) {
        if (ext[0] && !ext[1] && base_has(isa, ext[0]))
            return true;
        int n = slen(ext);
        for (const char* s = isa; *s; s++) {
            if (*s != '_')
                continue;
            int i = 0;
            while (i < n && s[1 + i] == ext[i])
                i++;
            if (i == n && (s[1 + n] == '_' || s[1 + n] == 0))
                return true;
        }
    }
    return fdt_stringlist_index(f, node, "riscv,isa-extensions", ext) >= 0;
}

const char* fdt_hart_unfit(const struct fdt* f, int node)
{
    if (!fdt_node_okay(f, node))
        return "disabled in the device tree";
    const char* mmu = fdt_getprop_string(f, node, "mmu-type");
    if (!mmu)
        return "no mmu-type";
    if (seq(mmu, "riscv,none"))
        return "no MMU (riscv,none)";
    // Without either ISA property the hart is taken on trust.
    if (!fdt_getprop(f, node, "riscv,isa", 0) && !fdt_getprop(f, node, "riscv,isa-extensions", 0))
        return 0;
    return fdt_hart_has_ext(f, node, "a") ? 0 : "no A extension";
}

// ---- DMA and caches -----------------------------------------------

bool fdt_dma_noncoherent(const struct fdt* f)
{
    for (int node = 0; node >= 0; node = fdt_next_node(f, node, 0)) {
        if (fdt_getprop(f, node, "dma-noncoherent", 0))
            return true;
    }
    return false;
}

int fdt_find_ccache(const struct fdt* f, struct fdt_region* reg, uint32_t* line)
{
    static const char* const compat[] = {
        "sifive,ccache0", "sifive,fu540-c000-ccache", "sifive,fu740-c000-ccache",
    };
    for (unsigned i = 0; i < sizeof(compat) / sizeof(compat[0]); i++) {
        int node = fdt_find_compatible(f, -1, compat[i]);
        if (node >= 0 && fdt_node_okay(f, node) && fdt_reg_cpu(f, node, 0, reg) == 0) {
            *line = fdt_getprop_u32(f, node, "cache-block-size", 64);
            return 0;
        }
    }
    return FDT_ERR_NOTFOUND;
}

// ---- framebuffer --------------------------------------------------

int fdt_find_simplefb(const struct fdt* f, struct fdt_simplefb* out)
{
    int node;
    for (node = fdt_find_compatible(f, -1, "simple-framebuffer"); node >= 0;
         node = fdt_find_compatible(f, node, "simple-framebuffer")) {
        if (fdt_node_okay(f, node))
            break;
    }
    // A /chosen child: /chosen has no ranges unless the loader added
    // them, and its addresses are the CPU's.
    struct fdt_region reg;
    if (node < 0 || (fdt_reg_cpu(f, node, 0, &reg) != 0 && fdt_reg(f, node, 0, &reg) != 0))
        return FDT_ERR_NOTFOUND;
    out->node = node;
    out->base = reg.base;
    out->size = reg.size;
    out->width = fdt_getprop_u32(f, node, "width", 0);
    out->height = fdt_getprop_u32(f, node, "height", 0);
    out->stride = fdt_getprop_u32(f, node, "stride", 0);
    out->format = fdt_getprop_string(f, node, "format");
    out->bpp = 0;
    if (out->format && (seq(out->format, "a8r8g8b8") || seq(out->format, "x8r8g8b8")))
        out->bpp = 32;
    if (!out->width || !out->height || out->stride < out->width * 4 || (uint64_t)out->stride * out->height > reg.size)
        out->bpp = 0;
    return 0;
}

// ---- xHCI -----------------------------------------------------------

static bool enabled_up(const struct fdt* f, int node)
{
    for (; node > 0; node = fdt_parent(f, node)) {
        if (!fdt_node_okay(f, node))
            return false;
    }
    return true;
}

int fdt_find_xhci(const struct fdt* f, struct fdt_xhci* out, int max, const char** skipped)
{
    int n = 0;
    for (int node = fdt_next_node(f, -1, 0); node >= 0; node = fdt_next_node(f, node, 0)) {
        int reg_index = 0, irq_index = 0;
        if (fdt_node_is_compatible(f, node, "cdns,usb3")) {
            const char* mode = fdt_getprop_string(f, node, "dr_mode");
            if (!mode || !seq(mode, "host")) {
                if (skipped && !*skipped && enabled_up(f, node))
                    *skipped = mode && seq(mode, "peripheral") ? "cdns,usb3 dr_mode peripheral"
                                                               : "cdns,usb3 not in host mode";
                continue;
            }
            reg_index = fdt_stringlist_index(f, node, "reg-names", "xhci");
            irq_index = fdt_stringlist_index(f, node, "interrupt-names", "host");
        } else if (!fdt_node_is_compatible(f, node, "generic-xhci") && !fdt_node_is_compatible(f, node, "xhci-platform")) {
            continue;
        }
        struct fdt_region reg;
        if (!enabled_up(f, node) || reg_index < 0 || irq_index < 0 || fdt_reg_cpu(f, node, reg_index, &reg) != 0)
            continue;
        if (n < max) {
            out[n] = (struct fdt_xhci){ node, reg.base, reg.size, fdt_interrupt(f, node, irq_index) };
        }
        n++;
    }
    return n;
}

// ---- DesignWare MMC -----------------------------------------------

int fdt_find_dw_mmc(const struct fdt* f, struct fdt_mmc* out, int max)
{
    int n = 0;
    for (int node = fdt_next_node(f, -1, 0); node >= 0; node = fdt_next_node(f, node, 0)) {
        struct fdt_region reg;
        if ((!fdt_node_is_compatible(f, node, "snps,dw-mshc") && !fdt_node_is_compatible(f, node, "starfive,jh7110-mmc"))
            || !enabled_up(f, node) || fdt_reg_cpu(f, node, 0, &reg) != 0) {
            continue;
        }
        if (n < max) {
            struct fdt_mmc* m = &out[n];
            m->node = node;
            m->base = reg.base;
            m->size = reg.size;
            m->irq = fdt_interrupt(f, node, 0);
            m->fifo_depth = fdt_getprop_u32(f, node, "fifo-depth", 0);
            m->bus_width = fdt_getprop_u32(f, node, "bus-width", 1);
            m->ciu_hz = fdt_getprop_u32(f, node, "clock-frequency", 0);
            m->max_hz = fdt_getprop_u32(f, node, "max-frequency", 0);
            m->non_removable = fdt_getprop(f, node, "non-removable", 0) != 0;
            m->no_sd = fdt_getprop(f, node, "no-sd", 0) != 0;
            m->no_mmc = fdt_getprop(f, node, "no-mmc", 0) != 0;
        }
        n++;
    }
    return n;
}
