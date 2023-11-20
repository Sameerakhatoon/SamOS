// Host-side test of the device tree parser, src/firmware/fdt.c.
//
// Fixtures in fixtures/ are QEMU 8.2's own trees, dumped with
//   qemu-system-aarch64 -machine virt,dumpdtb=F -cpu cortex-a72 -m 512M
//       -kernel bin/aarch64/kernel.img -append "samos.test=1 console=ttyAMA0"
//   qemu-system-riscv64 -machine virt,dumpdtb=F -m 512M
//       -kernel bin/aarch64/kernel.img -append "samos.test=1 console=ttyS0"
// (any file does as -kernel; it is only there so -append is allowed).
// A small tree built below covers what QEMU's do not: aliases, a
// translating `ranges`, version 16 and malformed blobs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "firmware/fdt.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if(!(cond)){ failures++; \
    printf("FAIL line %d: %s\n", __LINE__, #cond); } } while(0)
#define CHECK_EQ(got, want) do { long long g_ = (long long)(got), w_ = (long long)(want); \
    checks++; if(g_ != w_){ failures++; \
    printf("FAIL line %d: %s = 0x%llx, want 0x%llx\n", __LINE__, #got, g_, w_); } } while(0)
#define CHECK_STR(got, want) do { const char* g_ = (got); checks++; \
    if(!g_ || strcmp(g_, want) != 0){ failures++; \
    printf("FAIL line %d: %s = \"%s\", want \"%s\"\n", __LINE__, #got, g_ ? g_ : "(null)", want); } } while(0)

static uint8_t* load(const char* dir, const char* name, size_t* size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        printf("FAIL: cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    // One spare byte so the blob starts misaligned (see test_fixture).
    uint8_t* buf = malloc(*size + 1);
    if (fread(buf + 1, 1, *size, fp) != *size)
        exit(1);
    fclose(fp);
    return buf;
}

// ---- QEMU fixtures ----------------------------------------------

static void test_fixture(const char* dir, const char* name, const char* uart_compat,
                         uint64_t uart_base, uint64_t uart_size, uint64_t ram_base,
                         const char* bootargs, const char* stdout_path)
{
    size_t size;
    uint8_t* buf = load(dir, name, &size);
    // Deliberately at an odd address: the parser must only do byte loads.
    const uint8_t* blob = buf + 1;
    struct fdt f;

    CHECK_EQ(fdt_header_totalsize(blob), size);
    CHECK_EQ(fdt_init(&f, blob, size), 0);
    CHECK_EQ(f.version, 17);
    CHECK_EQ(fdt_init(&f, blob, size - 1), FDT_ERR_TRUNCATED);
    CHECK_EQ(fdt_init(&f, blob, 0), 0);

    CHECK_STR(fdt_node_name(&f, 0), "");
    CHECK_EQ(fdt_getprop_u32(&f, 0, "#address-cells", 0), 2);
    CHECK_EQ(fdt_getprop_u32(&f, 0, "#size-cells", 0), 2);
    CHECK_EQ(fdt_parent(&f, 0), FDT_ERR_NOTFOUND);

    CHECK_STR(fdt_bootargs(&f), bootargs);
    int chosen = fdt_path_offset(&f, "/chosen");
    CHECK(chosen > 0);
    CHECK_STR(fdt_getprop_string(&f, chosen, "stdout-path"), stdout_path);
    CHECK_EQ(fdt_parent(&f, chosen), 0);

    // The console: stdout-path, its compatible and translated reg.
    int uart = fdt_stdout_node(&f);
    CHECK(uart > 0);
    CHECK_EQ(fdt_path_offset(&f, stdout_path), uart);
    CHECK(fdt_node_is_compatible(&f, uart, uart_compat));
    CHECK(!fdt_node_is_compatible(&f, uart, "arm,pl01"));
    CHECK_EQ(fdt_find_compatible(&f, -1, uart_compat), uart);
    CHECK_EQ(fdt_address_cells(&f, uart), 2);
    CHECK_EQ(fdt_size_cells(&f, uart), 2);
    struct fdt_region r = { 0, 0 };
    CHECK_EQ(fdt_reg(&f, uart, 0, &r), 0);
    CHECK_EQ(r.base, uart_base);
    CHECK_EQ(r.size, uart_size);
    CHECK_EQ(fdt_reg(&f, uart, 1, &r), FDT_ERR_NOTFOUND);
    CHECK_EQ(fdt_reg_cpu(&f, uart, 0, &r), 0);
    CHECK_EQ(r.base, uart_base);

    // Memory: one 512 MiB bank at the RAM base, found by type and by
    // a path without the unit address.
    struct fdt_region mem[4];
    CHECK_EQ(fdt_memory_regions(&f, mem, 4), 1);
    CHECK_EQ(mem[0].base, ram_base);
    CHECK_EQ(mem[0].size, 512ull << 20);
    CHECK_EQ(fdt_memory_regions(&f, mem, 0), 1);
    int memnode = fdt_path_offset(&f, "/memory");
    CHECK(memnode > 0);
    char full[32];
    snprintf(full, sizeof full, "/memory@%llx", (unsigned long long)ram_base);
    CHECK_EQ(fdt_path_offset(&f, full), memnode);
    CHECK_EQ(fdt_path_offset(&f, "/memory@1"), FDT_ERR_NOTFOUND);
    CHECK_STR(fdt_getprop_string(&f, memnode, "device_type"), "memory");

    // cpus: #address-cells 1, #size-cells 0.
    int cpu = fdt_path_offset(&f, "/cpus/cpu@0");
    CHECK(cpu > 0);
    CHECK_EQ(fdt_address_cells(&f, cpu), 1);
    CHECK_EQ(fdt_size_cells(&f, cpu), 0);
    CHECK_EQ(fdt_reg(&f, cpu, 0, &r), 0);
    CHECK_EQ(r.base, 0);
    CHECK_EQ(r.size, 0);
    CHECK_EQ(fdt_path_offset(&f, "/cpus/cpu@0/"), cpu);
    CHECK_EQ(fdt_path_offset(&f, "//cpus//cpu@0"), cpu);
    CHECK_EQ(fdt_path_offset(&f, "/cpus/nope"), FDT_ERR_NOTFOUND);
    CHECK_EQ(fdt_path_offset(&f, "/"), 0);
    CHECK_EQ(fdt_path_offset(&f, "serial0"), FDT_ERR_NOTFOUND);  // no /aliases

    // A full walk: depth returns to 0 at most once per top-level
    // node, every node's parent is consistent, nothing errors.
    int depth = 0, nodes = 0, n, last = 0;
    for (n = fdt_next_node(&f, -1, &depth); n >= 0; n = fdt_next_node(&f, n, &depth)) {
        nodes++;
        last = n;
        CHECK(depth >= 0);
        if (depth == 1)
            CHECK_EQ(fdt_parent(&f, n), 0);
    }
    CHECK_EQ(n, FDT_ERR_NOTFOUND);
    CHECK(nodes > 10);
    CHECK(fdt_parent(&f, last) >= 0);

    // Children of the root, by sibling links, match depth-1 nodes.
    int children = 0;
    for (n = fdt_first_child(&f, 0); n >= 0; n = fdt_next_sibling(&f, n))
        children++;
    int depth1 = 0;
    depth = 0;
    for (n = fdt_next_node(&f, -1, &depth); n >= 0; n = fdt_next_node(&f, n, &depth))
        depth1 += depth == 1;
    CHECK_EQ(children, depth1);
    CHECK_EQ(fdt_first_child(&f, memnode), FDT_ERR_NOTFOUND);

    // Properties of /chosen by iteration.
    int props = 0, saw_bootargs = 0;
    for (int p = fdt_first_prop(&f, chosen); p >= 0; p = fdt_next_prop(&f, p)) {
        const char* pname;
        const void* value;
        uint32_t len;
        CHECK_EQ(fdt_prop(&f, p, &pname, &value, &len), 0);
        props++;
        if (strcmp(pname, "bootargs") == 0) {
            saw_bootargs = 1;
            CHECK_EQ(len, strlen(bootargs) + 1);
        }
    }
    CHECK(props >= 3);
    CHECK(saw_bootargs);

    // A blob cut short: every header block must fit totalsize.
    uint8_t* copy = malloc(size);
    memcpy(copy, blob, size);
    copy[0] ^= 1;
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_BADMAGIC);
    CHECK_EQ(fdt_header_totalsize(copy), 0);
    copy[0] ^= 1;
    copy[23] = 15;                              // version 15
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_BADVERSION);
    copy[23] = 17;
    copy[27] = 18;                              // last_comp_version 18
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_BADVERSION);
    copy[27] = 16;
    copy[36] = 0x7f;                            // size_dt_struct huge
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_TRUNCATED);
    memcpy(copy, blob, size);
    copy[32] = 0x7f;                            // size_dt_strings huge
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_TRUNCATED);
    memcpy(copy, blob, size);
    copy[8] = 0x7f;                             // off_dt_struct past the end
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_TRUNCATED);
    memcpy(copy, blob, size);
    copy[4] = copy[5] = copy[6] = 0;
    copy[7] = 39;                               // totalsize smaller than a header
    CHECK_EQ(fdt_init(&f, copy, 0), FDT_ERR_TRUNCATED);
    free(copy);
    free(buf);
}

// ---- a hand-built tree ------------------------------------------

struct builder {
    uint8_t st[4096];
    uint32_t st_len;
    char strs[1024];
    uint32_t strs_len;
};

static void put32(uint8_t* p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static void tok(struct builder* b, uint32_t t)
{
    put32(b->st + b->st_len, t);
    b->st_len += 4;
}

static void bytes(struct builder* b, const void* p, uint32_t n)
{
    memcpy(b->st + b->st_len, p, n);
    b->st_len += n;
    while (b->st_len & 3)
        b->st[b->st_len++] = 0;
}

static void begin(struct builder* b, const char* name)
{
    tok(b, 1);
    bytes(b, name, strlen(name) + 1);
}

static void end(struct builder* b)
{
    tok(b, 2);
}

static void prop(struct builder* b, const char* name, const void* value, uint32_t len)
{
    uint32_t off;
    for (off = 0; off < b->strs_len; off += strlen(b->strs + off) + 1)
        if (strcmp(b->strs + off, name) == 0)
            break;
    if (off == b->strs_len) {
        strcpy(b->strs + off, name);
        b->strs_len += strlen(name) + 1;
    }
    tok(b, 3);
    tok(b, len);
    tok(b, off);
    bytes(b, value, len);
}

static void prop_str(struct builder* b, const char* name, const char* s)
{
    prop(b, name, s, strlen(s) + 1);
}

static void prop_cells(struct builder* b, const char* name, int n, ...)
{
    uint8_t v[64];
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    for (int i = 0; i < n; i++)
        put32(v + 4 * i, __builtin_va_arg(ap, uint32_t));
    __builtin_va_end(ap);
    prop(b, name, v, 4 * n);
}

// Header + empty reserve map + struct + strings. version 16 omits
// size_dt_struct (left 0) to check the older layout.
static uint32_t finish(struct builder* b, uint8_t* out, uint32_t version)
{
    tok(b, 9);
    uint32_t off_rsv = 40, off_st = off_rsv + 16, off_strs = off_st + b->st_len;
    uint32_t total = off_strs + b->strs_len;
    memset(out, 0, total);
    put32(out, FDT_MAGIC);
    put32(out + 4, total);
    put32(out + 8, off_st);
    put32(out + 12, off_strs);
    put32(out + 16, off_rsv);
    put32(out + 20, version);
    put32(out + 24, 16);
    put32(out + 32, b->strs_len);
    put32(out + 36, version >= 17 ? b->st_len : 0);
    memcpy(out + off_st, b->st, b->st_len);
    memcpy(out + off_strs, b->strs, b->strs_len);
    return total;
}

static uint32_t build_tree(uint8_t* out, uint32_t version, int with_ranges)
{
    static struct builder b;
    memset(&b, 0, sizeof b);
    begin(&b, "");
    prop_cells(&b, "#address-cells", 1, 1);
    prop_cells(&b, "#size-cells", 1, 1);
    prop_str(&b, "compatible", "samos,test");
    tok(&b, 4);                                         // a NOP between properties
    begin(&b, "aliases");
    prop_str(&b, "serial0", "/soc/uart@1000");
    prop_str(&b, "soc", "/soc");
    end(&b);
    begin(&b, "chosen");
    prop_str(&b, "stdout-path", "serial0:115200n8");
    end(&b);
    begin(&b, "memory@80000000");                       // no device_type, found by name
    prop_cells(&b, "reg", 4, 0x80000000, 0x10000000, 0xa0000000, 0x08000000);
    end(&b);
    begin(&b, "ram");
    prop_str(&b, "device_type", "memory");
    prop_cells(&b, "reg", 2, 0x40000000, 0x1000);
    end(&b);
    begin(&b, "memory-controller");                     // name starts with memory, is not memory
    prop_cells(&b, "reg", 2, 0x1000, 0x100);
    end(&b);
    begin(&b, "soc");
    prop_cells(&b, "#address-cells", 1, 1);
    prop_cells(&b, "#size-cells", 1, 1);
    if (with_ranges)    // child 0x0 maps to CPU 0x10000000, 64 KiB
        prop_cells(&b, "ranges", 3, 0x0, 0x10000000, 0x10000);
    begin(&b, "uart@1000");
    prop(&b, "compatible", "ns16550a\0snps,dw-apb-uart", sizeof "ns16550a\0snps,dw-apb-uart");
    prop_cells(&b, "reg", 2, 0x1000, 0x100);
    end(&b);
    begin(&b, "outside@20000");                         // not covered by ranges
    prop_cells(&b, "reg", 2, 0x20000, 0x100);
    end(&b);
    begin(&b, "bad-reg");
    prop_cells(&b, "reg", 3, 1, 2, 3);                  // not a multiple of 2 cells
    end(&b);
    end(&b);
    end(&b);
    return finish(&b, out, version);
}

static void test_built(void)
{
    static uint8_t blob[8192];
    struct fdt f;
    struct fdt_region r;

    for (uint32_t version = 16; version <= 17; version++) {
        uint32_t size = build_tree(blob, version, 1);
        CHECK_EQ(fdt_init(&f, blob, sizeof blob), 0);
        CHECK_EQ(f.size, size);
        CHECK_EQ(f.version, version);

        CHECK_STR(fdt_getprop_string(&f, 0, "compatible"), "samos,test");
        int uart = fdt_path_offset(&f, "/soc/uart@1000");
        CHECK(uart > 0);
        CHECK_EQ(fdt_path_offset(&f, "serial0"), uart);
        CHECK_EQ(fdt_path_offset(&f, "serial0:115200n8"), uart);
        CHECK_EQ(fdt_path_offset(&f, "soc/uart"), uart);    // alias plus a relative path
        CHECK_EQ(fdt_path_offset(&f, "serial"), FDT_ERR_NOTFOUND);
        CHECK_EQ(fdt_path_offset(&f, "serial00"), FDT_ERR_NOTFOUND);
        CHECK_EQ(fdt_stdout_node(&f), uart);
        CHECK(fdt_node_is_compatible(&f, uart, "ns16550a"));
        CHECK(fdt_node_is_compatible(&f, uart, "snps,dw-apb-uart"));
        CHECK(!fdt_node_is_compatible(&f, uart, "snps"));
        CHECK_EQ(fdt_find_compatible(&f, -1, "snps,dw-apb-uart"), uart);
        CHECK_EQ(fdt_find_compatible(&f, uart, "snps,dw-apb-uart"), FDT_ERR_NOTFOUND);
        CHECK_EQ(fdt_bootargs(&f), 0);

        CHECK_EQ(fdt_reg(&f, uart, 0, &r), 0);
        CHECK_EQ(r.base, 0x1000);
        CHECK_EQ(r.size, 0x100);
        CHECK_EQ(fdt_reg_cpu(&f, uart, 0, &r), 0);
        CHECK_EQ(r.base, 0x10001000);
        int outside = fdt_path_offset(&f, "/soc/outside");
        CHECK_EQ(fdt_reg_cpu(&f, outside, 0, &r), FDT_ERR_NOTFOUND);
        CHECK_EQ(fdt_reg(&f, fdt_path_offset(&f, "/soc/bad-reg"), 0, &r), FDT_ERR_BADVALUE);
        CHECK_EQ(fdt_reg(&f, fdt_path_offset(&f, "/chosen"), 0, &r), FDT_ERR_NOTFOUND);

        struct fdt_region mem[2];
        CHECK_EQ(fdt_memory_regions(&f, mem, 2), 3);    // more than fit
        CHECK_EQ(mem[0].base, 0x80000000);
        CHECK_EQ(mem[0].size, 0x10000000);
        CHECK_EQ(mem[1].base, 0xa0000000);
        CHECK_EQ(mem[1].size, 0x08000000);

        // The NOP does not end the property list.
        int props = 0;
        for (int p = fdt_first_prop(&f, 0); p >= 0; p = fdt_next_prop(&f, p))
            props++;
        CHECK_EQ(props, 3);
    }

    // A bus without ranges cannot be translated.
    build_tree(blob, 17, 0);
    CHECK_EQ(fdt_init(&f, blob, 0), 0);
    CHECK_EQ(fdt_reg_cpu(&f, fdt_path_offset(&f, "/soc/uart"), 0, &r), FDT_ERR_NOTFOUND);

    // Malformed structure blocks: walks stop with an error, no reads
    // outside the blob (run.sh builds this test with -fsanitize).
    uint32_t off_st = 56;
    build_tree(blob, 17, 1);
    CHECK_EQ(fdt_init(&f, blob, 0), 0);
    int uart = fdt_path_offset(&f, "/soc/uart@1000");
    int p = fdt_first_prop(&f, uart);
    CHECK(p > uart);
    put32(blob + off_st + uart, 7);                     // an unknown token
    CHECK_EQ(fdt_path_offset(&f, "/soc/uart@1000"), FDT_ERR_BADSTRUCT);
    CHECK(fdt_find_compatible(&f, -1, "ns16550a") < 0);
    // A property length that runs past the structure block.
    build_tree(blob, 17, 1);
    put32(blob + off_st + p + 4, 0x10000);
    CHECK(fdt_getprop(&f, uart, "reg", NULL) == NULL);
    CHECK(fdt_find_compatible(&f, -1, "snps,dw-apb-uart") < 0);
    // A property name offset past the strings block.
    build_tree(blob, 17, 1);
    put32(blob + off_st + p + 8, 0x10000);
    CHECK(fdt_getprop(&f, uart, "reg", NULL) == NULL);
    // A struct block cut so a node name has no NUL: size_dt_struct
    // ends inside the root's first property.
    build_tree(blob, 17, 1);
    put32(blob + 36, 6);
    CHECK_EQ(fdt_init(&f, blob, 0), FDT_ERR_BADSTRUCT);
    put32(blob + 36, 10);
    CHECK_EQ(fdt_init(&f, blob, 0), 0);
    CHECK(fdt_first_prop(&f, 0) < 0);
    CHECK(fdt_next_node(&f, 0, NULL) < 0);
    CHECK(fdt_path_offset(&f, "/soc") < 0);
    CHECK_EQ(fdt_memory_regions(&f, NULL, 0), 0);
}

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "fixtures";
    test_fixture(dir, "qemu-virt-aarch64.dtb", "arm,pl011", 0x09000000, 0x1000, 0x40000000,
                 "samos.test=1 console=ttyAMA0", "/pl011@9000000");
    test_fixture(dir, "qemu-virt-riscv64.dtb", "ns16550a", 0x10000000, 0x100, 0x80000000,
                 "samos.test=1 console=ttyS0", "/soc/serial@10000000");
    test_built();
    printf("fdt host test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
