#ifndef SAMOS_BOOT_INFO_H
#define SAMOS_BOOT_INFO_H

// The loader -> kernel handoff block.
//
// SamOS divergence from the book: the book passes four framebuffer
// values in rdi/rsi/rdx/rcx. SamOS passes ONE pointer in rdi to this
// struct, which also carries the memory map, the ACPI RSDP and a
// command line, and has a magic + version so the kernel can reject
// a mismatched loader. It lives in the loader-reserved boot data
// area at SAMOS_BOOT_INFO_LOCATION (config.h).
//
// Shared by SamOs.c (EDK2) and the kernel, so it uses only plain C
// types and no headers. Layout is fixed: append fields at the end
// and bump SAMOS_BOOT_INFO_VERSION.

#define SAMOS_BOOT_INFO_MAGIC    0x4F464E49534F4D53ULL   // "SMOSINFO"
#define SAMOS_BOOT_INFO_VERSION  1
#define SAMOS_BOOT_CMDLINE_MAX   256

// E820 entry types in the memory map. 1..7 are the ACPI ones; the
// loader adds two of its own for UEFI memory types that have no
// E820 equivalent.
#define SAMOS_E820_TYPE_USABLE            1
#define SAMOS_E820_TYPE_RESERVED          2
#define SAMOS_E820_TYPE_ACPI_RECLAIMABLE  3
#define SAMOS_E820_TYPE_ACPI_NVS          4
#define SAMOS_E820_TYPE_BAD               5
#define SAMOS_E820_TYPE_PERSISTENT        7
#define SAMOS_E820_TYPE_RECLAIMABLE       0x1000   // loader + boot services, free once boot_info is consumed
#define SAMOS_E820_TYPE_KERNEL            0x1001   // kernel window and boot data area

// boot_info.fb_format
#define SAMOS_FB_FORMAT_BGRX     0   // byte order B,G,R,X (what the graphics code assumes)
#define SAMOS_FB_FORMAT_RGBX     1   // byte order R,G,B,X (colours come out swapped)

struct samos_boot_info {
    unsigned long long magic;
    unsigned int       version;
    unsigned int       size;            // sizeof(struct samos_boot_info)

    unsigned long long fb_base;         // physical framebuffer address
    unsigned long long fb_size;         // bytes
    unsigned int       fb_width;        // pixels
    unsigned int       fb_height;       // pixels
    unsigned int       fb_pitch;        // pixels per scan line
    unsigned int       fb_format;       // SAMOS_FB_FORMAT_*

    unsigned long long memory_map;      // address of the E820-style table (count + entries)
    unsigned long long acpi_rsdp;       // ACPI 2.0+ RSDP (else 1.0), 0 if none
    unsigned long long kernel_start;    // reserved kernel window (image + bss + boot stack)
    unsigned long long kernel_end;

    char cmdline[SAMOS_BOOT_CMDLINE_MAX];   // NUL-terminated, from \CMDLINE.TXT on the ESP
} __attribute__((packed));

#endif
