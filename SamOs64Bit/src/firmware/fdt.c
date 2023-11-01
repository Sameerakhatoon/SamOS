// Read-only flattened device tree parser. See fdt.h.
//
// Layout (devicetree spec v0.4, chapter 5): a 40-byte big-endian
// header, then a structure block of 4-byte aligned tokens
// (BEGIN_NODE name, PROP len nameoff value, END_NODE, NOP, END) and
// a strings block holding property names. Node and property offsets
// are relative to the start of the structure block.
#include "firmware/fdt.h"

#include <stddef.h>

#define FDT_BEGIN_NODE  1
#define FDT_END_NODE    2
#define FDT_PROP        3
#define FDT_NOP         4
#define FDT_END         9

#define FDT_HEADER_SIZE 40
#define FDT_MAX_DEPTH   32      // fdt_parent's stack; real trees are < 10 deep
#define FDT_MAX_CELLS   4

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t align4(uint32_t x)
{
    return (x + 3) & ~3u;
}

static int str_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static const uint8_t* sblock(const struct fdt* f)
{
    return f->blob + f->off_struct;
}

// Length of the NUL-terminated string at p, or -1 if no NUL in `max`.
static int bounded_strlen(const uint8_t* p, uint32_t max)
{
    for (uint32_t i = 0; i < max; i++) {
        if (p[i] == 0)
            return (int)i;
    }
    return -1;
}

uint32_t fdt_header_totalsize(const void* blob)
{
    const uint8_t* b = blob;
    if (!b || be32(b) != FDT_MAGIC)
        return 0;
    return be32(b + 4);
}

int fdt_init(struct fdt* f, const void* blob, uint32_t max_size)
{
    const uint8_t* b = blob;
    if (!b || be32(b) != FDT_MAGIC)
        return FDT_ERR_BADMAGIC;
    uint32_t total = be32(b + 4);
    if (total < FDT_HEADER_SIZE || (max_size && total > max_size))
        return FDT_ERR_TRUNCATED;
    uint32_t version = be32(b + 20);
    uint32_t last_comp = be32(b + 24);
    // v16 is the oldest layout we read; 17 adds size_dt_struct.
    if (version < 16 || last_comp > 17)
        return FDT_ERR_BADVERSION;

    f->blob = b;
    f->size = total;
    f->off_struct = be32(b + 8);
    f->off_strings = be32(b + 12);
    f->off_rsvmap = be32(b + 16);
    f->version = version;
    f->boot_cpuid = be32(b + 28);
    f->size_strings = be32(b + 32);
    f->size_struct = version >= 17 ? be32(b + 36) : 0;

    if (f->off_struct < FDT_HEADER_SIZE || f->off_struct > total || (f->off_struct & 3))
        return FDT_ERR_TRUNCATED;
    if (version < 17)
        f->size_struct = total - f->off_struct;
    if (f->size_struct > total - f->off_struct)
        return FDT_ERR_TRUNCATED;
    if (f->off_strings > total || f->size_strings > total - f->off_strings)
        return FDT_ERR_TRUNCATED;
    if (f->off_rsvmap > total)
        return FDT_ERR_TRUNCATED;
    if (f->size_struct < 8 || be32(sblock(f)) != FDT_BEGIN_NODE)
        return FDT_ERR_BADSTRUCT;
    return 0;
}

// Read the token at `off`; set *next to the token after it,
// skipping a node name or a property value.
static int next_tag(const struct fdt* f, int off, uint32_t* tag, int* next)
{
    uint32_t o = (uint32_t)off;
    if (off < 0 || f->size_struct < 4 || o > f->size_struct - 4 || (o & 3))
        return FDT_ERR_BADSTRUCT;
    const uint8_t* s = sblock(f);
    *tag = be32(s + o);
    o += 4;
    switch (*tag) {
    case FDT_BEGIN_NODE: {
        int n = bounded_strlen(s + o, f->size_struct - o);
        if (n < 0)
            return FDT_ERR_BADSTRUCT;
        o = align4(o + n + 1);
        break;
    }
    case FDT_PROP: {
        if (f->size_struct - o < 8)
            return FDT_ERR_BADSTRUCT;
        uint32_t len = be32(s + o);
        o += 8;
        if (len > f->size_struct - o)
            return FDT_ERR_BADSTRUCT;
        o = align4(o + len);
        break;
    }
    case FDT_END_NODE:
    case FDT_NOP:
    case FDT_END:
        break;
    default:
        return FDT_ERR_BADSTRUCT;
    }
    if (o > f->size_struct)
        return FDT_ERR_BADSTRUCT;
    *next = (int)o;
    return 0;
}

int fdt_next_node(const struct fdt* f, int node, int* depth)
{
    int local = 0;
    if (!depth)
        depth = &local;
    if (node < 0) {
        *depth = 0;
        return 0;   // fdt_init checked the root's BEGIN_NODE
    }
    uint32_t tag;
    int off;
    int r = next_tag(f, node, &tag, &off);
    if (r < 0)
        return r;
    if (tag != FDT_BEGIN_NODE)
        return FDT_ERR_BADSTRUCT;
    for (;;) {
        int here = off;
        r = next_tag(f, here, &tag, &off);
        if (r < 0)
            return r;
        if (tag == FDT_BEGIN_NODE) {
            ++*depth;
            return here;
        }
        if (tag == FDT_END_NODE)
            --*depth;
        else if (tag == FDT_END)
            return FDT_ERR_NOTFOUND;
    }
}

int fdt_first_child(const struct fdt* f, int node)
{
    int depth = 0;
    int n = fdt_next_node(f, node, &depth);
    if (n < 0)
        return n;
    return depth == 1 ? n : FDT_ERR_NOTFOUND;
}

int fdt_next_sibling(const struct fdt* f, int node)
{
    int depth = 0;
    int n = node;
    for (;;) {
        n = fdt_next_node(f, n, &depth);
        if (n < 0)
            return n;
        if (depth == 0)
            return n;
        if (depth < 0)
            return FDT_ERR_NOTFOUND;
    }
}

int fdt_parent(const struct fdt* f, int node)
{
    int stack[FDT_MAX_DEPTH];
    int depth = 0;
    for (int n = fdt_next_node(f, -1, &depth); n >= 0; n = fdt_next_node(f, n, &depth)) {
        if (depth < 0 || depth >= FDT_MAX_DEPTH)
            return FDT_ERR_BADSTRUCT;
        stack[depth] = n;
        if (n == node)
            return depth == 0 ? FDT_ERR_NOTFOUND : stack[depth - 1];
    }
    return FDT_ERR_NOTFOUND;
}

const char* fdt_node_name(const struct fdt* f, int node)
{
    uint32_t tag;
    int next;
    if (next_tag(f, node, &tag, &next) < 0 || tag != FDT_BEGIN_NODE)
        return NULL;
    return (const char*)sblock(f) + node + 4;
}

// ---- properties -------------------------------------------------

// The first PROP at or after `off`, skipping NOPs.
static int prop_at(const struct fdt* f, int off)
{
    for (;;) {
        uint32_t tag;
        int next;
        int r = next_tag(f, off, &tag, &next);
        if (r < 0)
            return r;
        if (tag == FDT_PROP)
            return off;
        if (tag != FDT_NOP)
            return FDT_ERR_NOTFOUND;
        off = next;
    }
}

int fdt_first_prop(const struct fdt* f, int node)
{
    uint32_t tag;
    int next;
    int r = next_tag(f, node, &tag, &next);
    if (r < 0)
        return r;
    if (tag != FDT_BEGIN_NODE)
        return FDT_ERR_BADSTRUCT;
    return prop_at(f, next);
}

int fdt_next_prop(const struct fdt* f, int prop)
{
    uint32_t tag;
    int next;
    int r = next_tag(f, prop, &tag, &next);
    if (r < 0)
        return r;
    if (tag != FDT_PROP)
        return FDT_ERR_BADSTRUCT;
    return prop_at(f, next);
}

int fdt_prop(const struct fdt* f, int prop, const char** name, const void** value, uint32_t* len)
{
    uint32_t tag;
    int next;
    int r = next_tag(f, prop, &tag, &next);
    if (r < 0)
        return r;
    if (tag != FDT_PROP)
        return FDT_ERR_BADSTRUCT;
    const uint8_t* p = sblock(f) + prop;
    uint32_t nameoff = be32(p + 8);
    if (nameoff >= f->size_strings)
        return FDT_ERR_BADSTRUCT;
    const uint8_t* s = f->blob + f->off_strings + nameoff;
    if (bounded_strlen(s, f->size_strings - nameoff) < 0)
        return FDT_ERR_BADSTRUCT;
    if (name)
        *name = (const char*)s;
    if (value)
        *value = p + 12;
    if (len)
        *len = be32(p + 4);
    return 0;
}

const void* fdt_getprop(const struct fdt* f, int node, const char* name, uint32_t* len)
{
    for (int p = fdt_first_prop(f, node); p >= 0; p = fdt_next_prop(f, p)) {
        const char* pname;
        const void* value;
        uint32_t plen;
        if (fdt_prop(f, p, &pname, &value, &plen) < 0)
            return NULL;
        if (str_eq(pname, name)) {
            if (len)
                *len = plen;
            return value;
        }
    }
    return NULL;
}

const char* fdt_getprop_string(const struct fdt* f, int node, const char* name)
{
    uint32_t len;
    const char* s = fdt_getprop(f, node, name, &len);
    if (!s || len == 0 || s[len - 1] != 0)
        return NULL;
    return s;
}

uint32_t fdt_getprop_u32(const struct fdt* f, int node, const char* name, uint32_t fallback)
{
    uint32_t len;
    const uint8_t* v = fdt_getprop(f, node, name, &len);
    if (!v || len != 4)
        return fallback;
    return be32(v);
}

// ---- lookup -----------------------------------------------------

// Does node name `name` match path component c[0..clen)? Without an
// '@' in the component, the unit address is optional.
static int name_matches(const char* name, const char* c, int clen)
{
    int has_at = 0;
    for (int i = 0; i < clen; i++) {
        if (name[i] != c[i])
            return 0;
        if (c[i] == '@')
            has_at = 1;
    }
    return name[clen] == 0 || (!has_at && name[clen] == '@');
}

// Walk path[0..end) down from `node`, one component at a time.
static int walk_path(const struct fdt* f, int node, const char* path, int end)
{
    int i = 0;
    while (i < end) {
        while (i < end && path[i] == '/')
            i++;
        if (i == end)
            break;
        int start = i;
        while (i < end && path[i] != '/')
            i++;
        int child;
        for (child = fdt_first_child(f, node); child >= 0; child = fdt_next_sibling(f, child)) {
            const char* name = fdt_node_name(f, child);
            if (name && name_matches(name, path + start, i - start))
                break;
        }
        if (child < 0)
            return child;
        node = child;
    }
    return node;
}

int fdt_path_offset(const struct fdt* f, const char* path)
{
    int end = 0;
    while (path[end] && path[end] != ':')
        end++;
    if (path[0] == '/')
        return walk_path(f, 0, path, end);

    // An alias: the first component names a property of /aliases
    // whose value is an absolute path.
    int alen = 0;
    while (alen < end && path[alen] != '/')
        alen++;
    int aliases = walk_path(f, 0, "/aliases", 8);
    if (aliases < 0)
        return FDT_ERR_NOTFOUND;
    for (int p = fdt_first_prop(f, aliases); p >= 0; p = fdt_next_prop(f, p)) {
        const char* name;
        const void* value;
        uint32_t len;
        if (fdt_prop(f, p, &name, &value, &len) < 0)
            return FDT_ERR_BADSTRUCT;
        if (bounded_strlen((const uint8_t*)name, alen + 1) != alen || !name_matches(name, path, alen))
            continue;
        const char* target = value;
        if (len < 2 || target[len - 1] != 0 || target[0] != '/')
            return FDT_ERR_BADVALUE;
        int node = walk_path(f, 0, target, (int)len - 1);
        if (node < 0)
            return node;
        return walk_path(f, node, path + alen, end - alen);
    }
    return FDT_ERR_NOTFOUND;
}

int fdt_node_is_compatible(const struct fdt* f, int node, const char* compat)
{
    uint32_t len;
    const char* list = fdt_getprop(f, node, "compatible", &len);
    if (!list)
        return 0;
    uint32_t i = 0;
    while (i < len) {
        int n = bounded_strlen((const uint8_t*)list + i, len - i);
        if (n < 0)
            return 0;
        if (str_eq(list + i, compat))
            return 1;
        i += n + 1;
    }
    return 0;
}

int fdt_find_compatible(const struct fdt* f, int start, const char* compat)
{
    for (int n = fdt_next_node(f, start, NULL); n >= 0; n = fdt_next_node(f, n, NULL)) {
        if (fdt_node_is_compatible(f, n, compat))
            return n;
    }
    return FDT_ERR_NOTFOUND;
}

// ---- addresses --------------------------------------------------

// `cells` big-endian cells at p as one number; with more than two
// cells (PCI's three) only the low 64 bits are kept.
static uint64_t read_cells(const uint8_t* p, uint32_t cells)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < cells; i++)
        v = (v << 32) | be32(p + 4 * i);
    return v;
}

uint32_t fdt_address_cells(const struct fdt* f, int node)
{
    int parent = fdt_parent(f, node);
    return fdt_getprop_u32(f, parent < 0 ? 0 : parent, "#address-cells", 2);
}

uint32_t fdt_size_cells(const struct fdt* f, int node)
{
    int parent = fdt_parent(f, node);
    return fdt_getprop_u32(f, parent < 0 ? 0 : parent, "#size-cells", 1);
}

int fdt_reg(const struct fdt* f, int node, int index, struct fdt_region* out)
{
    uint32_t ac = fdt_address_cells(f, node);
    uint32_t sc = fdt_size_cells(f, node);
    if (ac == 0 || ac > FDT_MAX_CELLS || sc > FDT_MAX_CELLS)
        return FDT_ERR_BADVALUE;
    uint32_t len;
    const uint8_t* reg = fdt_getprop(f, node, "reg", &len);
    if (!reg)
        return FDT_ERR_NOTFOUND;
    uint32_t entry = 4 * (ac + sc);
    if (len % entry)
        return FDT_ERR_BADVALUE;
    if (index < 0 || (uint32_t)index >= len / entry)
        return FDT_ERR_NOTFOUND;
    reg += entry * index;
    out->base = read_cells(reg, ac);
    out->size = read_cells(reg + 4 * ac, sc);
    return 0;
}

int fdt_reg_cpu(const struct fdt* f, int node, int index, struct fdt_region* out)
{
    int r = fdt_reg(f, node, index, out);
    if (r < 0)
        return r;
    // Each bus between the node and the root maps its child
    // addresses to its parent's with (child, parent, size) triples.
    for (int bus = fdt_parent(f, node); bus > 0; bus = fdt_parent(f, bus)) {
        uint32_t len;
        const uint8_t* ranges = fdt_getprop(f, bus, "ranges", &len);
        if (!ranges)
            return FDT_ERR_NOTFOUND;
        if (len == 0)
            continue;
        uint32_t cac = fdt_getprop_u32(f, bus, "#address-cells", 2);
        uint32_t csc = fdt_getprop_u32(f, bus, "#size-cells", 1);
        uint32_t pac = fdt_address_cells(f, bus);
        if (cac == 0 || cac > FDT_MAX_CELLS || csc > FDT_MAX_CELLS || pac == 0 || pac > FDT_MAX_CELLS)
            return FDT_ERR_BADVALUE;
        uint32_t entry = 4 * (cac + pac + csc);
        if (len % entry)
            return FDT_ERR_BADVALUE;
        uint32_t i;
        for (i = 0; i < len; i += entry) {
            uint64_t child = read_cells(ranges + i, cac);
            uint64_t parent = read_cells(ranges + i + 4 * cac, pac);
            uint64_t size = read_cells(ranges + i + 4 * (cac + pac), csc);
            if (out->base >= child && out->base - child < size) {
                out->base = out->base - child + parent;
                break;
            }
        }
        if (i == len)
            return FDT_ERR_NOTFOUND;
    }
    return 0;
}

// ---- well-known nodes -------------------------------------------

static int is_memory_node(const struct fdt* f, int node)
{
    const char* type = fdt_getprop_string(f, node, "device_type");
    if (type)
        return str_eq(type, "memory");
    const char* name = fdt_node_name(f, node);
    return name && name_matches(name, "memory", 6);
}

int fdt_memory_regions(const struct fdt* f, struct fdt_region* out, int max)
{
    int count = 0;
    for (int n = fdt_first_child(f, 0); n >= 0; n = fdt_next_sibling(f, n)) {
        if (!is_memory_node(f, n))
            continue;
        for (int i = 0;; i++) {
            struct fdt_region region;
            int r = fdt_reg(f, n, i, &region);
            if (r == FDT_ERR_NOTFOUND)
                break;
            if (r < 0)
                return r;
            if (count < max)
                out[count] = region;
            count++;
        }
    }
    return count;
}

const char* fdt_bootargs(const struct fdt* f)
{
    int chosen = fdt_path_offset(f, "/chosen");
    if (chosen < 0)
        return NULL;
    return fdt_getprop_string(f, chosen, "bootargs");
}

int fdt_stdout_node(const struct fdt* f)
{
    int chosen = fdt_path_offset(f, "/chosen");
    if (chosen < 0)
        return FDT_ERR_NOTFOUND;
    const char* path = fdt_getprop_string(f, chosen, "stdout-path");
    if (!path)
        path = fdt_getprop_string(f, chosen, "linux,stdout-path");
    if (!path)
        return FDT_ERR_NOTFOUND;
    return fdt_path_offset(f, path);
}
