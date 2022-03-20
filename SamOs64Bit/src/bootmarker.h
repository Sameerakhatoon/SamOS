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
    BM_STAGE_MAX              = 13,
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
