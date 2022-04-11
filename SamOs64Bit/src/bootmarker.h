#ifndef BOOTMARKER_H
#define BOOTMARKER_H
//
// Boot-stage markers for end-to-end runtime testing.
//
// The kernel writes a magic value to a fixed physical address as
// it completes each subsystem init. After QEMU boot, the host-side
// test harness uses `pmemsave` to dump the marker region and
// verifies which stages the kernel reached.
//
// This is invisible to userland and adds about 100 ns per stage
// (a single 64-bit MMIO write), so leaving it on in production
// builds is fine.
//
// ## Wire format
//
// The marker region is 13 * 8 = 104 bytes at BOOT_MARKER_BASE.
// Each marker is one uint64_t with this layout:
//
//   bits 63..32  : magic prefix (0xB007C0DE | stage_id)
//   bits 31..0   : caller-supplied value (PCI device count,
//                  primary fs disk id, framebuffer base addr,
//                  etc - or 0 if the stage is a bare "I got here").
//
// A test for "did stage 5 (PCI) run, and were >0 devices found?"
// reads marker_block[5] and verifies:
//   1. high 32 bits == (BOOT_MARKER_MAGIC_BASE >> 32) | stage_id
//   2. low 32 bits > 0
//
// ## Why 0x6000?
//
// Conventional RAM, well below the kernel at 0x100000. Not used
// by BIOS data area (0x0..0x500), the IVT, boot.asm (0x7C00), or
// boot.asm's transition page tables at 0x9000+. Identity-mapped
// by both boot.asm and the kernel's post-paging-switch tables.
//
#include <stdint.h>

#define BOOT_MARKER_BASE          0x6000ULL
#define BOOT_MARKER_MAGIC_PREFIX  0xB007C0DEULL  // ascii "Boot Code"

// Marker slot ids. Add new stages at the END and bump
// BM_STAGE_MAX so existing tests do not need to be renumbered.
typedef enum {
    BM_STAGE_NONE             = 0,
    BM_STAGE_KERNEL_MAIN      = 1,   // very first kernel_main statement
    BM_STAGE_PAGING_SWITCHED  = 2,   // kernel_paging_desc is live
    BM_STAGE_MULTIHEAP_READY  = 3,   // kheap_post_paging completed
    BM_STAGE_IDT_LIVE         = 4,   // idt_init completed
    BM_STAGE_PCI_SCANNED      = 5,   // pci_init: value = device count
    BM_STAGE_DISK_INITED      = 6,   // disk_search_and_init returned
    BM_STAGE_FS_RESOLVED      = 7,   // fs_init returned
    BM_STAGE_GRAPHICS_UP      = 8,   // graphics_setup_stage_two done
    BM_STAGE_PROCESS_INIT     = 9,   // process_system_init returned
    BM_STAGE_KEYBOARD_INIT    = 10,  // keyboard_init returned
    BM_STAGE_WINDOW_STAGE2    = 11,  // window_system_initialize_stage2 done
    BM_STAGE_ISR80H_READY     = 12,  // isr80h_register_commands done

    // Reserved free slots for ad-hoc kernel-side probes during
    // diagnosis. Tests in tests64/e2e/ ignore them by default.
    BM_STAGE_DEBUG_A          = 13,
    BM_STAGE_DEBUG_B          = 14,
    BM_STAGE_DEBUG_C          = 15,

    // Feature-test slots (kernel_selftest writes these). Each
    // slot stores 1 (pass) or 0 (fail) in the value half. Add
    // new feature checks at the end and bump BM_STAGE_MAX.
    BM_FEATURE_KMALLOC_RT     = 16,  // kmalloc(N) + kfree round trip
    BM_FEATURE_KMALLOC_BIG    = 17,  // kmalloc(64K) returns non-NULL
    BM_FEATURE_DISK_READ      = 18,  // disk_read_block(0) returns boot signature
    BM_FEATURE_FS_FOPEN       = 19,  // fopen("@:/BLANK.ELF") returns fd > 0
    BM_FEATURE_FS_FREAD_ELF   = 20,  // first 4 bytes of BLANK.ELF == ELF magic
    BM_FEATURE_FS_FSTAT       = 21,  // fstat reports BLANK.ELF size > 0
    BM_FEATURE_FS_FCLOSE      = 22,  // fclose succeeds
    BM_FEATURE_PCI_IDE        = 23,  // PCI list contains class 1 subclass 1 device
    BM_FEATURE_PCI_VGA        = 24,  // PCI list contains class 3 device
    BM_FEATURE_TSC_INCREASES  = 25,  // tsc_microseconds advances between calls
    BM_FEATURE_VECTOR_OPS     = 26,  // vector_new + push + at + free works
    BM_FEATURE_PAGING_LOOKUP  = 27,  // paging_get_physical_address roundtrips a known mapping
    BM_FEATURE_KZALLOC_ZERO   = 28,  // kzalloc returns memory that reads as zero
    BM_FEATURE_CPUID_VENDOR   = 29,  // cpuid leaf 0 returns a non-zero vendor signature
    // slot 30 reserved (disk_streamer probe; folded into fs_io)
    BM_FEATURE_KREALLOC_RT    = 31,  // krealloc(p, larger) preserves prefix bytes
    BM_FEATURE_STREAMER_CACHE = 32,  // diskstreamer_cache_new returns non-NULL
    BM_FEATURE_FS_PARSE_PATH  = 33,  // pathparser_parse("@:/X") returns root + first part
    BM_FEATURE_E820_ENTRIES   = 34,  // e820_total_entries() returns > 0
    BM_FEATURE_E820_ACCESS    = 35,  // e820_total_accessible_memory() > 1 MiB
    BM_FEATURE_DISK_ENUM      = 36,  // disk_get(0) returns a real disk
    BM_FEATURE_GRAPHICS_FB    = 37,  // graphics_screen_info() returns non-NULL
    BM_FEATURE_GRAPHICS_SIZE  = 38,  // screen info has width>0 + height>0
    BM_FEATURE_FB_PAINTED     = 39,  // back-buffer pixels are non-zero (graphics drew)
    BM_FEATURE_FB_HAS_FONT    = 51,  // back-buffer has a font-bitmap signature (text was drawn)
    BM_FEATURE_FB_HAS_WINDOW  = 52,  // back-buffer has a window-border-coloured pixel
    BM_FEATURE_IRQ_API        = 56,  // IRQ_enable/IRQ_disable round trip on mask
    BM_FEATURE_DISK_MULTIPLE  = 57,  // disk_get(1) returns non-NULL (GPT virtual disks)
    BM_FEATURE_SYSTEM_FONT_RC = 58,  // font_get_system_font: NULL OK (no sysfont.bmp on ESP)
    BM_FEATURE_FAT16_LABEL    = 59,  // primary FS reports a non-empty volume label
    BM_FEATURE_PCI_BARS       = 60,  // at least one PCI device has a non-zero BAR
    BM_FEATURE_TASK_SLEEP     = 61,  // kernel-side task_sleep call does not crash

    // User-side feature slots written by the selftest ELF via
    // SYSTEM_COMMAND26_E2E_MARK. The first user slot is
    // BM_USER_FEATURE_BASE; entries below are kernel-only and
    // user writes are rejected by the syscall handler.
    BM_USER_FEATURE_BASE      = 40,
    BM_USER_FOPEN             = 40,  // user fopen("@:/BLANK.ELF") > 0
    BM_USER_FREAD_ELF         = 41,  // user fread(4) returns ELF magic
    BM_USER_FSTAT_SIZE        = 42,  // user fstat reports size > 0
    BM_USER_FSEEK_ZERO        = 43,  // user fseek(0) succeeds
    BM_USER_FCLOSE_OK         = 44,  // user fclose returns 0
    BM_USER_MALLOC_FREE       = 45,  // user malloc/free round trip
    BM_USER_REALLOC_GROW      = 46,  // user realloc preserves prefix
    BM_USER_GETPID_OR_SELF    = 47,  // reserved
    BM_USER_INT80_REACHABLE   = 48,  // first slot the user ELF writes
    BM_USER_TSC_UDELAY        = 49,  // udelay(1000) advances TSC
    BM_USER_TSC_MONOTONIC     = 50,  // two TSC reads with gap differ

    BM_STAGE_MAX              = 64,
} boot_marker_stage_t;

// Pack stage + value into the marker slot.
static inline void boot_marker_set(boot_marker_stage_t stage, uint32_t value)
{
    volatile uint64_t* slots = (volatile uint64_t*)BOOT_MARKER_BASE;
    uint64_t magic = (BOOT_MARKER_MAGIC_PREFIX << 32) | ((uint64_t)stage << 24);
    slots[stage] = magic | (uint64_t)value;
}

// Used by the e2e test harness to compute the expected high half
// without duplicating the layout. Tests read the marker via
// pmemsave + od and compare against this.
static inline uint64_t boot_marker_expected_high(boot_marker_stage_t stage)
{
    return (BOOT_MARKER_MAGIC_PREFIX << 32) | ((uint64_t)stage << 24);
}

#endif
