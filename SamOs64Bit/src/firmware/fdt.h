#ifndef FIRMWARE_FDT_H
#define FIRMWARE_FDT_H

// Read-only flattened device tree (DTB) parser, libfdt style.
//
// Arch neutral: no arch code, no libc, no allocation. The blob is
// never written. Every read is bounds checked against the header's
// totalsize and the struct/strings block sizes, and every load is
// byte by byte, so a blob at any alignment works even with the MMU
// (and so unaligned access) off.
//
// Nodes and properties are int offsets into the structure block,
// like libfdt. A negative value is "not found" or an error
// (FDT_ERR_*). The root node is offset 0.

#include <stdint.h>

#define FDT_MAGIC           0xd00dfeed

#define FDT_ERR_NOTFOUND    -1
#define FDT_ERR_BADMAGIC    -2
#define FDT_ERR_BADVERSION  -3
#define FDT_ERR_TRUNCATED   -4  // a header field or block points past totalsize
#define FDT_ERR_BADSTRUCT   -5  // malformed token stream
#define FDT_ERR_BADVALUE    -6  // a property has the wrong size or no NUL

struct fdt {
    const uint8_t* blob;
    uint32_t       size;          // totalsize from the header
    uint32_t       off_struct;
    uint32_t       size_struct;
    uint32_t       off_strings;
    uint32_t       size_strings;
    uint32_t       off_rsvmap;
    uint32_t       version;
    uint32_t       boot_cpuid;
};

struct fdt_region {
    uint64_t base;
    uint64_t size;
};

// Total size from the header of the blob at `blob`, or 0 if the magic
// is wrong. Lets a caller check the blob fits its memory before init.
uint32_t    fdt_header_totalsize(const void* blob);

// Validate the header and fill `f`. `max_size` caps totalsize
// (0 = trust the header). 0 on success, FDT_ERR_* otherwise.
int         fdt_init(struct fdt* f, const void* blob, uint32_t max_size);

// ---- walking ----------------------------------------------------
// Next node in document order after `node` (pass -1 to start at the
// root). `depth`, if not NULL, is updated: root is 0. Returns
// FDT_ERR_NOTFOUND at the end of the tree.
int         fdt_next_node(const struct fdt* f, int node, int* depth);
int         fdt_first_child(const struct fdt* f, int node);
int         fdt_next_sibling(const struct fdt* f, int node);
int         fdt_parent(const struct fdt* f, int node);
// Node name with unit address ("pl011@9000000"; "" for the root).
const char* fdt_node_name(const struct fdt* f, int node);

// Properties of `node`: first, then next until FDT_ERR_NOTFOUND.
int         fdt_first_prop(const struct fdt* f, int node);
int         fdt_next_prop(const struct fdt* f, int prop);
// Name, value and length of property `prop`. 0 or FDT_ERR_*.
int         fdt_prop(const struct fdt* f, int prop, const char** name,
                     const void** value, uint32_t* len);
// Value of `node`'s property `name`, NULL if absent. `len` optional.
const void* fdt_getprop(const struct fdt* f, int node, const char* name, uint32_t* len);
// A NUL-terminated string property, NULL if absent or not a string.
const char* fdt_getprop_string(const struct fdt* f, int node, const char* name);
// A one-cell property, `fallback` if absent or not 4 bytes.
uint32_t    fdt_getprop_u32(const struct fdt* f, int node, const char* name, uint32_t fallback);

// ---- lookup -----------------------------------------------------
// "/soc/serial@10000000". A component without "@" also matches a
// node with a unit address ("/memory" finds "memory@40000000"). A
// path not starting with '/' is looked up in /aliases first. Anything
// from ':' on is ignored (stdout-path options like ":115200n8").
int         fdt_path_offset(const struct fdt* f, const char* path);
// 1 if `node`'s compatible string list contains `compat`.
int         fdt_node_is_compatible(const struct fdt* f, int node, const char* compat);
// Next node after `start` (-1 = from the root) compatible with `compat`.
int         fdt_find_compatible(const struct fdt* f, int start, const char* compat);

// ---- addresses --------------------------------------------------
// #address-cells / #size-cells that apply to `node`'s reg, read from
// its parent (spec defaults 2 and 1).
uint32_t    fdt_address_cells(const struct fdt* f, int node);
uint32_t    fdt_size_cells(const struct fdt* f, int node);
// Entry `index` of `node`'s reg, in the parent bus's address space.
// 0 on success, FDT_ERR_NOTFOUND past the last entry.
int         fdt_reg(const struct fdt* f, int node, int index, struct fdt_region* out);
// Like fdt_reg, then translated up through every parent's `ranges`
// to a CPU physical address. An empty ranges is identity; a parent
// without ranges (other than the root) is not translatable.
int         fdt_reg_cpu(const struct fdt* f, int node, int index, struct fdt_region* out);

// ---- well-known nodes -------------------------------------------
// Every reg entry of every top-level memory node (device_type
// "memory", or named memory / memory@...). Fills up to `max` regions,
// returns the number found (may exceed `max`), FDT_ERR_* on a bad reg.
int         fdt_memory_regions(const struct fdt* f, struct fdt_region* out, int max);
// /chosen bootargs, NULL if absent.
const char* fdt_bootargs(const struct fdt* f);
// The node /chosen stdout-path (or linux,stdout-path) names, or
// FDT_ERR_NOTFOUND.
int         fdt_stdout_node(const struct fdt* f);

#endif
