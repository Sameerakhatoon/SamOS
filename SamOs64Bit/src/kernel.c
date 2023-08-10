// Lecture 9 - restore simple terminal so kernel_main can paint
// progress to VGA without needing the QEMU monitor.
//
// What's live in this file right now:
//   - VGA cell helpers       (terminal_make_char, terminal_putchar)
//   - Cursor + backspace     (terminal_writechar, terminal_backspace)
//   - Screen init            (terminal_initialize)
//   - String print           (print)
//   - kernel_main banner     (Hello 64-bit!)
//
// Everything else from the 32-bit kernel_main (GDT/TSS, kheap, paging,
// IDT, FAT16, processes, scheduler) is still out of the build and
// comes back lecture by lecture.

#include "kernel.h"
#include <stddef.h>
#include <stdint.h>
#include "idt/idt.h"
#include "memory/heap/kheap.h"
#include "memory/heap/heap.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "string/string.h"
#include "task/tss.h"
#include "task/task.h"
#include "task/process.h"
#include "isr80h/isr80h.h"
#include "keyboard/keyboard.h"
#include "fs/file.h"
#include "fs/pparser.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include "disk/gpt.h"
#include "graphics/graphics.h"   // L87 - root surface struct + API
#include "graphics/image/image.h" // L90 - graphics_image_load

// L87 - bss-resident default_graphics_info lives in kernel.asm;
// the long-mode entry stashes the UEFI framebuffer params into it
// before any C code runs. kernel_main only needs the address.
extern struct graphics_info default_graphics_info;
#include "gdt/gdt.h"
#include "config.h"
#include "status.h"

uint16_t* video_mem = 0;
uint16_t terminal_row = 0;
uint16_t terminal_col = 0;

uint16_t terminal_make_char(char c, char colour)
{
    return (colour << 8) | c;
}

void terminal_putchar(int x, int y, char c, char colour)
{
    video_mem[(y * VGA_WIDTH) + x] = terminal_make_char(c, colour);
}

void terminal_backspace()
{
    if (terminal_row == 0 && terminal_col == 0)
    {
        return;
    }

    if (terminal_col == 0)
    {
        terminal_row -= 1;
        terminal_col = VGA_WIDTH;
    }

    terminal_col -= 1;
    terminal_writechar(' ', 15);
    terminal_col -= 1;
}

void terminal_writechar(char c, char colour)
{
    if (c == '\n')
    {
        terminal_row += 1;
        terminal_col = 0;
        return;
    }

    if (c == 0x08)
    {
        terminal_backspace();
        return;
    }

    terminal_putchar(terminal_col, terminal_row, c, colour);
    terminal_col += 1;
    if (terminal_col >= VGA_WIDTH)
    {
        terminal_col = 0;
        terminal_row += 1;
    }
}

void terminal_initialize()
{
    video_mem = (uint16_t*)(0xB8000);
    terminal_row = 0;
    terminal_col = 0;
    for (int y = 0; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            terminal_putchar(x, y, ' ', 0);
        }
    }
}

void print(const char* str)
{
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++)
    {
        terminal_writechar(str[i], 15);
    }
}

// Lecture 15 - tiny halt-with-message helper. Future fault handlers
// and unrecoverable kernel asserts will call panic("...") instead
// of silent while(1)s, so we get a visible reason on VGA.
void panic(const char* msg)
{
    print(msg);
    while (1)
    {
    }
}

// Lecture 15 - the kernel's own paging descriptor. Created once in
// kernel_main; afterwards anyone (user task code, fault handlers,
// scheduler) that wants the kernel address space loaded just calls
// kernel_page() instead of re-deriving the descriptor.
struct paging_desc* kernel_paging_desc = 0;

// Lecture 15 - restore the kernel address space + reload segregs.
// Symmetric counterpart to a (future) user_page() that loads a
// task's PML4 + user data selectors. Order matters: switch CR3
// first so subsequent loads hit kernel mappings; reload segregs
// after so any latent user-mode selector is gone before we run
// kernel code.
void kernel_page()
{
    kernel_registers();
    paging_switch(kernel_paging_desc);
}

// Lecture 38 - asm-side #DE trigger (kept around for ad-hoc
// debugging; not currently called).
extern void div_test(void);

// Lecture 50 - the static GDT lives in kernel.asm. C side
// installs the TSS descriptor into slot 7 + 8.
extern struct gdt_entry gdt[];
struct tss tss;

// Lecture 44 - public getter for the kernel's paging_desc.
// Anyone needing to walk the kernel address space (e.g.
// task.c::copy_string_from_task) calls this rather than
// reaching for kernel_paging_desc directly.
struct paging_desc* kernel_desc(){
    return kernel_paging_desc;
}

void kernel_main(void)
{
    terminal_initialize();
    print("Hello 64-bit!\n");

    // Lecture 18 - read the E820 memory map BEFORE kheap_init.
    // The E820 entries live at SAMOS_MEMORY_MAP_LOCATION which is
    // the same physical address as SAMOS_HEAP_TABLE_ADDRESS - the
    // heap init writes its block bitmap on top, so any E820 read
    // after kheap_init returns garbage.
    print("e820 total: ");
    print(itoa((int)e820_total_accessible_memory()));
    print("\n");

    // Lecture 20 - kheap_init now no-args and routes through the
    // multiheap. The kernel_paging_desc / paging_switch dance and
    // the heap accounting prints from L13/L15/L16 go away here -
    // upstream PeachOS64 comments them out at this point. SamOs
    // follows: the early probes did their job in their lectures
    // and now multi-heap setup is the only thing happening before
    // the wait loop.
    kheap_init();
    char* data = kmalloc(50);
    data[0] = 'A';
    data[1] = 'B';
    data[2] = 'C';
    data[3] = 0x00;
    print(data);

    // Lecture 21 - build a kernel paging descriptor and identity-map
    // every E820 usable region into it.
    //
    // SAFETY: paging_desc_new uses kzalloc internally. L20 stubbed
    // kzalloc to return NULL pending the L22+ multiheap_zalloc, so
    // this WILL return NULL here. We guard the call so the kernel
    // does not deref NULL and #PF. Once kzalloc starts returning
    // real memory the guard is harmless and the mapping fires.
    // Upstream PeachOS64 commits this code unguarded - documented
    // in docs/64bit/chapters/L21-paging-map-e820.md as a deferred
    // bug they accept until L22 lands.
    kernel_paging_desc = paging_desc_new(PAGING_MAP_LEVEL_4);
    if (!kernel_paging_desc)
    {
        panic("Failed to create kernel paging descriptor\n");
    }
    paging_map_e820_memory_regions(kernel_paging_desc);

    // Lecture 23 - now that the new PML4 maps every E820 usable
    // region AND the first 1 MiB (BIOS / VGA / boot / kernel),
    // switch CR3 to it. The L21 marker print is gone; if we got
    // here without faulting, paging_switch worked.
    paging_switch(kernel_paging_desc);

    // Lecture 32 - hand the live kernel multiheap off to
    // multiheap_ready. Builds shadow heaps for every paging-
    // defragment sub-heap, reserves their virtual ranges in
    // the live PML4, locks the sub-heap set against further
    // adds. Must run AFTER paging_switch (multiheap_ready
    // panics if no descriptor is loaded).
    kheap_post_paging();
    print("multiheap ready\n");

    // Lecture 87 - bring up the graphics subsystem. The struct
    // was filled in by long-mode entry from the UEFI handoff
    // regs. Lecture 90 replaces the red-square smoke test with a
    // real BMP draw once disks are up; see further below.
    graphics_setup(&default_graphics_info);

    // Lecture 50 - bring up IDT, then build a TSS so future
    // ring-3 traps have a kernel-side stack to land on. The
    // L38 div_test smoke is gone; from L50 onward the test
    // token is "tss ready".
    idt_init();

    // Lecture 57 - subsystem initializers. fs_init wires the
    // generic VFS layer (registers the FAT16 driver). disk_
    // search_and_init walks the ATA channels for primary disks
    // and binds the filesystem driver to each. After this we
    // can fopen("0:/foo") from kernel context.
    fs_init();
    disk_search_and_init();

    // Lecture 82 - now that disks are enumerated, parse GPT
    // headers and spin up a virtual disk per partition.
    gpt_init();

    // Allocate a 1 MiB kernel stack for ring transitions.
    // kzalloc lays it out low-to-high; rsp0 needs the TOP of
    // the region (stack grows down), so we add stack_size to
    // the base.
    size_t stack_size = 1024 * 1024;
    void* tss_stack_low  = kzalloc(stack_size);
    void* tss_stack_high = (void*)((uintptr_t)tss_stack_low + stack_size);

    // Mark the lowest page of the stack as not-present so any
    // overflow #PFs visibly instead of silently corrupting
    // whatever sits below. paging_map with flags=0 unmaps the
    // page (the "unmap idiom" introduced in L27).
    paging_map(kernel_desc(), tss_stack_low, tss_stack_low, 0);

    // Initialise the TSS.
    memset(&tss, 0x00, sizeof(tss));
    tss.rsp0        = (uint64_t)tss_stack_high;
    tss.iopb_offset = sizeof(tss);   // no IO bitmap

    // Install the TSS descriptor into GDT slot 7 (+ slot 8 for
    // the high half of the base). The reserved slots were added
    // to kernel.asm in L50.
    struct tss_desc_64* tssdesc =
        (struct tss_desc_64*)&gdt[KERNEL_LONG_MODE_TSS_GDT_INDEX];
    gdt_set_tss(tssdesc, &tss, sizeof(tss) - 1, TSS_DESCRIPTOR_TYPE, 0x00);

    // Lecture 55 - ltr the TSS. The GDTR limit was set at boot
    // to gdt_end - gdt - 1, which (since L50 reserved the two
    // TSS slots BEFORE gdt_end) already covers selector 0x38.
    // So ltr finds the descriptor we just wrote and loads it.
    tss_load(KERNEL_LONG_MODE_TSS_SELECTOR);
    print("tss load was fine\n");

    // Lecture 54 - register every syscall handler the user can
    // ask for via int 0x80. isr80h_register_commands fills the
    // isr80h_commands[] table; isr80h_handler dispatches a
    // user-supplied command id into that table on each int 0x80.
    isr80h_register_commands();
    print("register isr80h\n");

    // Lecture 58 - initialise the PS/2 keyboard driver. Won't
    // actually receive scancodes until IRQ1 is unmasked at the
    // PIC AND interrupts are sti'd, both still pending.
    keyboard_init();

    print("tss ready\n");

    // Lecture 59 - load + run a user program. process_load_switch
    // opens the file, kzallocs a copy of its bytes, builds a
    // task + process around it, and marks it current. Then
    // task_run_first_ever_task iretq's into ring 3 at the user
    // entry point. The simple.bin user program is just
    //   [BITS 64]
    //   jmp $
    // so the user task spins forever; we never come back.
    print("Loading program...\n");
    struct process* p = 0;
    // L63 - upstream switches to "0:/blank.elf" here. SamOs
    // sticks with SIMPLE.BIN until L66 (the L62 ELF64 build +
    // the L65 elfloader refactor are both prerequisites for
    // actually running the ELF). The 2 MiB blank.elf is also
    // glacially slow to PIO-read in QEMU TCG. The ELF loader
    // code is now wired into the build (L63 milestone); we'll
    // call it once the prerequisites land.
    // Lecture 66 - flipping to "0:/BLANK.ELF" works correctness-
    // wise (the L65 ELF64 loader recognises and parses our
    // x86_64-elf-built blank.elf, validates EI_CLASS=64, walks
    // the PT_LOAD program header, maps the user code at
    // 0x400000, and iretq's in). But blank.elf is ~2 MiB on
    // disk because the linker's PT_LOAD file offset = VMA mod
    // p_align (defaults to 0x200000 in elf64 ld), so the file
    // is padded with zeros from offset 0 to 0x200000 before the
    // real segment. ATA-PIO sector-at-a-time read in QEMU TCG
    // takes ~40+ seconds for that. The regression test budgets
    // 15 seconds and would fail.
    //
    // We keep SIMPLE.BIN here for fast CI. The L67+ work
    // (probably IRQ-driven IO) or a switch to a smaller linker
    // script will let us flip to BLANK.ELF without timing out.
    // Lecture 81 - "@" resolves to the primary filesystem disk
    // through pparser_get_drive_by_path. Drops the literal "0:".
    // Lecture 90 - drop the background BMP onto the screen
    // surface before the user task takes over. The image lives
    // on the primary FS; @ resolves to the L81 primary disk.
    // Errors are intentionally swallowed: a missing/bad
    // bkground.bmp must not block boot.
    struct image* bg = graphics_image_load("@:/bkground.bmp");
    if(bg){
        graphics_draw_image(NULL, bg, 0, 0);
        graphics_redraw_all();
    }

    int res = process_load_switch("@:/SIMPLE.BIN", &p);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load user program\n");
    }
    print("user enter\n");
    // task_run_first_ever_task never returns. After this:
    //   - CR3 = task's PML4
    //   - iretq into ring 3 at SAMOS_PROGRAM_VIRTUAL_ADDRESS
    //   - user code (jmp $) spins forever
    //
    // L52 enabled the timer IRQ callback. With the PIC not yet
    // remapped (L61), IRQ0 lands at vector 0x08 - the same
    // vector the CPU uses for #DF. Our IDT vector 8 is
    // registered as idt_handle_exception which panics "Panic
    // Exception" so we observe both "user enter" AND "Panic
    // Exception" on screen, with the user code running between
    // them. L61 remaps the PIC to vectors 0x20+ to clear the
    // collision.
    task_run_first_ever_task();

    while (1)
    {
    }
}
