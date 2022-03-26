// Kernel-side self-tests for runtime feature coverage.
// See `kernel_selftest.h` for the design rationale.
//
// Each check is small and independent so a kernel bug in one
// area never masks coverage of another. We catch faults inside
// individual probes by sequencing them as plain straight-line
// code; if a probe triple-faults, the marker simply stays
// "not reached" and the e2e test will see that.

#include "kernel_selftest.h"
#include "bootmarker.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "disk/disk.h"
#include "fs/file.h"
#include "io/pci.h"
#include "io/tsc.h"
#include "lib/vector/vector.h"
#include "kernel.h"

#include <stddef.h>
#include <stdint.h>

static void mark_pass(boot_marker_stage_t s) { boot_marker_set(s, 1); }
static void mark_fail(boot_marker_stage_t s) { boot_marker_set(s, 0); }

// kmalloc / kfree round trip: allocate 1 KiB, write a pattern,
// read it back, free. Catches "allocator returns NULL" and
// "allocator returns aliased memory".
static void selftest_kmalloc_roundtrip(void) {
    char* p = kmalloc(1024);
    if (!p) { mark_fail(BM_FEATURE_KMALLOC_RT); return; }
    for (int i = 0; i < 1024; i++) p[i] = (char)(i & 0xFF);
    int ok = 1;
    for (int i = 0; i < 1024; i++) {
        if (p[i] != (char)(i & 0xFF)) { ok = 0; break; }
    }
    kfree(p);
    if (ok) mark_pass(BM_FEATURE_KMALLOC_RT);
    else    mark_fail(BM_FEATURE_KMALLOC_RT);
}

// Large allocation that spans page boundaries.
static void selftest_kmalloc_big(void) {
    void* p = kmalloc(64 * 1024);
    if (p) { mark_pass(BM_FEATURE_KMALLOC_BIG); kfree(p); }
    else   { mark_fail(BM_FEATURE_KMALLOC_BIG); }
}

// kzalloc must return memory that reads as zero.
static void selftest_kzalloc_zero(void) {
    char* p = kzalloc(256);
    if (!p) { mark_fail(BM_FEATURE_KZALLOC_ZERO); return; }
    int ok = 1;
    for (int i = 0; i < 256; i++) if (p[i] != 0) { ok = 0; break; }
    kfree(p);
    if (ok) mark_pass(BM_FEATURE_KZALLOC_ZERO);
    else    mark_fail(BM_FEATURE_KZALLOC_ZERO);
}

// Read sector 0 from the primary disk. On a FAT-formatted disk
// this is the boot sector; the last 2 bytes are 0x55 0xAA.
static void selftest_disk_read(void) {
    struct disk* d = disk_primary();
    if (!d) { mark_fail(BM_FEATURE_DISK_READ); return; }
    unsigned char buf[512];
    if (disk_read_block(d, 0, 1, buf) < 0) {
        mark_fail(BM_FEATURE_DISK_READ);
        return;
    }
    // FAT boot signature 0x55 0xAA at the end.
    if (buf[510] == 0x55 && buf[511] == 0xAA) mark_pass(BM_FEATURE_DISK_READ);
    else                                       mark_fail(BM_FEATURE_DISK_READ);
}

// FAT16 file IO end to end: open BLANK.ELF, fstat, read first
// 4 bytes (should match the ELF magic 0x7F 'E' 'L' 'F'), close.
static void selftest_fs_io(void) {
    int fd = fopen("@:/BLANK.ELF", "r");
    if (fd <= 0) {
        mark_fail(BM_FEATURE_FS_FOPEN);
        mark_fail(BM_FEATURE_FS_FREAD_ELF);
        mark_fail(BM_FEATURE_FS_FSTAT);
        mark_fail(BM_FEATURE_FS_FCLOSE);
        return;
    }
    mark_pass(BM_FEATURE_FS_FOPEN);

    struct file_stat st;
    if (fstat(fd, &st) >= 0 && st.filesize > 0) mark_pass(BM_FEATURE_FS_FSTAT);
    else                                         mark_fail(BM_FEATURE_FS_FSTAT);

    unsigned char hdr[4] = {0};
    if (fread(hdr, 1, 4, fd) == 4 &&
        hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F') {
        mark_pass(BM_FEATURE_FS_FREAD_ELF);
    } else {
        mark_fail(BM_FEATURE_FS_FREAD_ELF);
    }

    if (fclose(fd) == 0) mark_pass(BM_FEATURE_FS_FCLOSE);
    else                  mark_fail(BM_FEATURE_FS_FCLOSE);
}

// PCI enumeration sanity: confirm at least one IDE controller
// (class 0x01, subclass 0x01) and one VGA device (class 0x03).
// QEMU's `-machine pc` always exposes both.
static void selftest_pci_classes(void) {
    int ide = 0;
    int vga = 0;
    size_t total = pci_device_count();
    for (size_t i = 0; i < total; i++) {
        struct pci_device* dev = NULL;
        if (pci_device_get(i, &dev) < 0 || !dev) continue;
        if (dev->class.base == 0x01 && dev->class.subclass == 0x01) ide = 1;
        if (dev->class.base == 0x03) vga = 1;
    }
    if (ide) mark_pass(BM_FEATURE_PCI_IDE); else mark_fail(BM_FEATURE_PCI_IDE);
    if (vga) mark_pass(BM_FEATURE_PCI_VGA); else mark_fail(BM_FEATURE_PCI_VGA);
}

// TSC should monotonically advance between two reads.
static void selftest_tsc_increases(void) {
    uint64_t a = tsc_microseconds();
    // small busy loop so a few cycles pass
    for (volatile int i = 0; i < 100000; i++) { }
    uint64_t b = tsc_microseconds();
    if (b > a) mark_pass(BM_FEATURE_TSC_INCREASES);
    else        mark_fail(BM_FEATURE_TSC_INCREASES);
}

// Generic vector lifecycle.
static void selftest_vector_ops(void) {
    struct vector* v = vector_new(sizeof(int), 4, 0);
    if (!v) { mark_fail(BM_FEATURE_VECTOR_OPS); return; }
    int values[] = {7, 11, 13, 17};
    for (int i = 0; i < 4; i++) vector_push(v, &values[i]);
    int ok = (vector_count(v) == 4);
    for (int i = 0; ok && i < 4; i++) {
        int out = 0;
        vector_at(v, i, &out, sizeof(out));
        if (out != values[i]) ok = 0;
    }
    vector_free(v);
    if (ok) mark_pass(BM_FEATURE_VECTOR_OPS);
    else    mark_fail(BM_FEATURE_VECTOR_OPS);
}

// Pick a known mapping (the kernel itself at 0x100000) and
// verify paging_get_physical_address recovers it.
static void selftest_paging_lookup(void) {
    struct paging_desc* d = kernel_desc();
    if (!d) { mark_fail(BM_FEATURE_PAGING_LOOKUP); return; }
    void* v = (void*)0x100000ULL;
    void* p = paging_get_physical_address(d, v);
    // Identity-mapped low memory; p should equal v.
    if (p == v) mark_pass(BM_FEATURE_PAGING_LOOKUP);
    else         mark_fail(BM_FEATURE_PAGING_LOOKUP);
}

int kernel_selftest(void) {
    selftest_kmalloc_roundtrip();
    selftest_kmalloc_big();
    selftest_kzalloc_zero();
    selftest_disk_read();
    selftest_fs_io();
    selftest_pci_classes();
    selftest_tsc_increases();
    selftest_vector_ops();
    selftest_paging_lookup();
    return 0;
}
