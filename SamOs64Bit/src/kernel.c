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
// interrupts, FAT16, processes, scheduler) is still out of the build
// and comes back lecture by lecture.
//
// Everything that touches the CPU, the interrupt controller or
// the timer goes through arch/arch.h; x86's half of the boot sequence
// is in src/arch/x86_64/ (arch.c, smp.c).

#include "kernel.h"
#include <stddef.h>
#include <stdint.h>
#include "arch/arch.h"
#include "memory/heap/kheap.h"
#include "memory/heap/heap.h"
#include "memory/heap/memtest.h"
#include "memory/memory.h"
#include "string/string.h"
#include "task/task.h"
#include "task/process.h"
#include "isr80h/isr80h.h"
#include "io/pci.h"           // L180 - pci_init
#include "bootmarker.h"       // e2e: stage markers for runtime tests
#include "kernel_selftest.h"  // e2e: feature-level runtime self-tests
#include "fs/storage_selftest.h"  // Storage self-test
#include "keyboard/keyboard.h"
#include "fs/file.h"
#include "fs/pparser.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include "disk/gpt.h"
#include "graphics/graphics.h"   // L87 - root surface struct + API
#include "graphics/image/image.h" // L90 - graphics_image_load
#include "graphics/font.h"        // L95 - font_system_init / font_draw_text
#include "graphics/terminal.h"    // L100 - system_terminal handle
#include "graphics/window.h"      // L122 - window_system_initialize
#include "mouse/mouse.h"          // L150 - mouse_system_init / load_static_drivers
#include "io/serial.h"            // COM1 console
#include "boot/boot_info.h"       // Loader handoff block
#include "boot/cmdline.h"         // samos.fblog
#include "acpi/acpi.h"            // ACPI tables, shutdown, reboot
#include "io/rtc.h"               // Wall-clock time
#include "fs/fat/fat.h"           // RTC time for FAT timestamps
#include "cpu/percpu.h"           // Per-CPU data
#include "cpu/smp.h"              // APs, IPIs, panic stop
#include "cpu/smp_selftest.h"     // IPI and shootdown checks
#include "net/netdev.h"            // Networking

// L87 - bss-resident default_graphics_info lives in the arch's entry
// code (x86: kernel.asm), which stashes the UEFI framebuffer params
// into it before any C code runs. kernel_main only needs the address.
extern struct graphics_info default_graphics_info;
#include "config.h"
#include "status.h"

// Lecture 100 - the legacy VGA-text terminal (video_mem at 0xB8000,
// VGA_WIDTH x VGA_HEIGHT cell grid, the L1-L13 terminal_writechar
// path) is gone. The post-UEFI-pivot replacement is a single
// system_terminal handle into the graphics terminal module; the
// kernel-side terminal_writechar becomes a thin forwarder to
// terminal_write.
struct terminal* system_terminal = NULL;

void terminal_writechar(char c, char colour)
{
    (void)colour;   // L100 - the graphics terminal carries its own colour.
    if (!system_terminal)
    {
        return;
    }
    terminal_write(system_terminal, c);
}

// Framebuffer logging. On a PC without a UART (or with
// samos.fblog) the boot's serial lines (acpi:, apic:, refclock:,
// xhci:, usb:, ...) also go to the system terminal: what was written
// before it existed is replayed, and serial_putc mirrors the rest
// until the first process starts. print() then leaves the screen to
// the mirror, so its lines are not drawn twice.
static bool fblog_active;

static void fblog_putc(char c)
{
    // The same rule as print(): skipped under a lock that comes after
    // the graphics lock.
    if (!system_terminal || !gfx_lock_for_print())
    {
        return;
    }
    terminal_writechar(c, 15);
    gfx_unlock();
}

static void fblog_start(void)
{
    if (!system_terminal || (serial_present() && !boot_cmdline_has("samos.fblog")))
    {
        return;
    }
    unsigned int len = 0;
    const char* log = serial_boot_log_get(&len);
    for (unsigned int i = 0; i < len; i++)
    {
        fblog_putc(log[i]);
    }
    fblog_active = true;
    serial_set_mirror(fblog_putc);
    print(serial_present() ? "fblog: serial lines on the screen (samos.fblog)\n"
                           : "fblog: no serial port, serial lines on the screen\n");
}

static void fblog_stop(void)
{
    if (fblog_active)
    {
        serial_set_mirror(NULL);
        fblog_active = false;
    }
}

void print(const char* str)
{
    // Mirror every kernel print to COM1 (see io/serial.h).
    serial_write(str);
    // The screen is the graphics lock's; skipped when
    // the caller holds a lock that comes after it in the order.
    if (fblog_active || !system_terminal || !gfx_lock_for_print())
    {
        return;
    }
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++)
    {
        terminal_writechar(str[i], 15);
    }
    gfx_unlock();
}

// Lecture 15 - tiny halt-with-message helper. Future fault handlers
// and unrecoverable kernel asserts will call panic("...") instead
// of silent while(1)s, so we get a visible reason on VGA.
void panic(const char* msg)
{
    // Stop the other CPUs first (x86: NMI), then write
    // without locks: their holders may be among the stopped CPUs.
    arch_irq_disable();
    smp_panic_begin();
    serial_write_unlocked("\nKERNEL PANIC: ");
    serial_write_unlocked(msg);
    if (system_terminal && !smp_active())
    {
        for (const char* p = msg; *p; p++)
        {
            terminal_writechar(*p, 15);
        }
    }
    // SamOs divergence: halt with interrupts off instead of a
    // busy spin, so a panicked kernel stops burning a host CPU.
    arch_halt();
}

// Lecture 15 - the kernel's own paging descriptor. Created once in
// kernel_main; afterwards anyone (user task code, fault handlers,
// scheduler) that wants the kernel address space loaded just calls
// kernel_page() instead of re-deriving the descriptor.
struct paging_desc* kernel_paging_desc = 0;

// Lecture 15 - restore the kernel address space + reload segregs.
// Symmetric counterpart to task_page, which loads a task's address
// space and user data selectors. The selectors are the arch's
// kernel context (x86: data segments; nothing on aarch64 and riscv64).
void kernel_page()
{
    arch_kernel_context();
    paging_switch(kernel_paging_desc);
    // No process address space on this CPU any more.
    this_cpu()->user_desc = NULL;
}

// Lecture 44 - public getter for the kernel's paging_desc.
// Anyone needing to walk the kernel address space (e.g.
// task.c::copy_string_from_task) calls this rather than
// reaching for kernel_paging_desc directly.
struct paging_desc* kernel_desc(){
    return kernel_paging_desc;
}

// samos_boot_info_ptr (arch/arch.h) is set by the arch's
// entry code from the loader's handoff.

static int cmdline_has(const char* word)
{
    const char* s = samos_boot_info_ptr->cmdline;
    size_t n = strlen(word);
    for (; *s; s++)
    {
        if (strncmp(s, word, n) == 0 && (s[n] == ' ' || s[n] == 0))
        {
            return 1;
        }
    }
    return 0;
}

// samos.bootlog: everything written to serial so far (also
// without a UART) to @:/BOOTLOG.TXT, replacing the last one.
static void kernel_write_bootlog(void)
{
    unsigned int len = 0;
    const char* log = serial_boot_log_get(&len);
    fremove("@:/BOOTLOG.TXT");
    int fd = fopen("@:/BOOTLOG.TXT", "w");
    int res = fd > 0 ? fwrite(log, len, 1, fd) : -EIO;
    if (fd > 0)
    {
        fclose(fd);
    }
    print(res < 0 ? "bootlog: @:/BOOTLOG.TXT not written\n" : "bootlog: @:/BOOTLOG.TXT written\n");
}

// e2e hook: CMDLINE.TXT "samos.run=PROG.ELF arg ...;PROG2.ELF ..."
// (last on the line) loads each program from @:/ with its arguments,
// ahead of BLANK.ELF, so it runs first. Not from the book.
static int cmdline_end(char c)
{
    return c == 0 || c == '\n' || c == '\r';
}

static void kernel_load_cmdline_programs(void)
{
    const char* key = "samos.run=";
    const char* s = samos_boot_info_ptr->cmdline;
    size_t key_len = strlen(key);
    while (*s && strncmp(s, key, key_len) != 0)
    {
        s++;
    }
    if (!*s)
    {
        return;
    }
    s += key_len;

    while (!cmdline_end(*s))
    {
        struct command_argument* root = NULL;
        struct command_argument* last = NULL;
        while (!cmdline_end(*s) && *s != ';')
        {
            if (*s == ' ')
            {
                s++;
                continue;
            }
            struct command_argument* arg = kzalloc(sizeof(struct command_argument));
            size_t n = 0;
            while (!cmdline_end(*s) && *s != ' ' && *s != ';' && n < sizeof(arg->argument) - 1)
            {
                arg->argument[n++] = *s++;
            }
            if (last)
            {
                last->next = arg;
            }
            else
            {
                root = arg;
            }
            last = arg;
        }
        if (*s == ';')
        {
            s++;
        }
        if (!root)
        {
            continue;
        }

        char path[SAMOS_MAX_PATH] = "@:/";
        strncpy(path + 3, root->argument, sizeof(path) - 4);
        struct process* process = NULL;
        int res = process_load(path, &process);
        if(res == SAMOS_ALL_OK)
        {
            res = process_inject_arguments(process, root);
            // Started only once its arguments are in place.
            if(res == SAMOS_ALL_OK)
            {
                process_start(process);
            }
            else
            {
                process_terminate(process);
            }
        }
        print(res == SAMOS_ALL_OK ? "samos.run: loaded " : "samos.run: failed to load ");
        print(path);
        print("\n");

        while (root)
        {
            struct command_argument* next = root->next;
            kfree(root);
            root = next;
        }
    }
}

void kernel_main(void)
{
    // Lecture 100 - terminal_initialize() (VGA-text grid setup)
    // is gone; system_terminal is created later, after the
    // framebuffer-side graphics_setup and font_system_init.
    struct graphics_info* screen_info = NULL;
    serial_init();
    serial_write("SamOS: kernel_main\n");

    // Refuse a loader that does not speak this boot_info.
    if (samos_boot_info_ptr->magic != SAMOS_BOOT_INFO_MAGIC ||
        samos_boot_info_ptr->version != SAMOS_BOOT_INFO_VERSION)
    {
        panic("boot_info magic/version mismatch: rebuild SamOs.efi\n");
    }
    serial_write("boot_info: cmdline \"");
    serial_write(samos_boot_info_ptr->cmdline);
    serial_write("\"\n");

    // Check the CPU and set its control registers before any
    // code that depends on them; the cycle counter's
    // frequency. Both are the arch's early set-up.
    arch_early_init();

    // e2e hook: fault before arch_trap_init so the early trap
    // table has to catch it and dump registers on the console.
    if (cmdline_has("samos.early_fault_test"))
    {
        arch_fault_test(ARCH_FAULT_TEST_UNDEFINED);
    }

    boot_marker_set(BM_STAGE_KERNEL_MAIN, 0);
    print("Hello 64-bit!\n");

    // Lecture 18 - the book read the E820 map here, before kheap_init,
    // because its heap bitmap overwrote the map. Since the UEFI loader and the memory manager rework the
    // map sits in the loader's boot data area (SAMOS_E820_TYPE_KERNEL),
    // which pfa_init reserves (memtest pfa checks it), so the map is
    // still readable afterwards; this is only the boot log line.
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
    // kheap_init now builds the page-frame allocator from the
    // memory map first; the E820 entries stay readable afterwards
    // because the PFA reserves them.
    kheap_init();
    char* data = kmalloc(50);
    if (!data)
    {
        panic("kmalloc failed right after kheap_init\n");
    }
    data[0] = 'A';
    data[1] = 'B';
    data[2] = 'C';
    data[3] = 0x00;
    print(data);
    kfree(data);

    // Lecture 21 - build a kernel paging descriptor and identity-map
    // every E820 usable region into it.
    //
    // SAFETY: paging_desc_new uses kzalloc internally. L20 stubbed
    // kzalloc to return NULL pending the L22+ multiheap_zalloc, so
    // this WILL return NULL here. We guard the call so the kernel
    // does not deref NULL and #PF. Once kzalloc starts returning
    // real memory the guard is harmless and the mapping fires.
    // Upstream PeachOS64 commits this code unguarded: a deferred
    // bug they accept until L22 lands.
    //
    // paging_kernel_desc_new builds the real kernel
    // address space: the direct map of RAM, the kernel image at
    // SAMOS_KERNEL_VMA with W^X, and every upper-half PML4 slot so
    // processes can share them. No identity map, so a NULL or user
    // pointer dereferenced in the kernel faults.
    kernel_paging_desc = paging_kernel_desc_new();
    if (!kernel_paging_desc)
    {
        panic("Failed to create kernel paging descriptor\n");
    }

    // Lecture 23 - switch to it. The L21 marker print is gone;
    // if we got here without faulting, paging_switch worked.
    paging_switch(kernel_paging_desc);
    boot_marker_set(BM_STAGE_PAGING_SWITCHED, 0);

    // Lecture 32 - hand the live kernel multiheap off to
    // multiheap_ready. Builds shadow heaps for every paging-
    // defragment sub-heap, reserves their virtual ranges in
    // the live PML4, locks the sub-heap set against further
    // adds. Must run AFTER paging_switch (multiheap_ready
    // panics if no descriptor is loaded).
    kheap_post_paging();
    boot_marker_set(BM_STAGE_MULTIHEAP_READY, 0);
    print("multiheap ready\n");

    // ACPI tables (MADT, FADT, MCFG) before PCI and the
    // interrupt controllers; the RTC needs the FADT century register.
    // The arch's firmware tables (x86: ACPI).
    arch_firmware_init();
    rtc_init();
    fat_set_time_source(rtc_unix_time);   // File timestamps

    // Lecture 87 - bring up the graphics subsystem. The struct
    // was filled in by long-mode entry from the UEFI handoff
    // regs. Lecture 100 stashes the resolved screen_info now so
    // terminal_create can size the system terminal to the full
    // framebuffer below.
    graphics_setup(&default_graphics_info);
    screen_info = graphics_screen_info();

    // Lecture 50 - bring up the trap vectors and the trap stacks so
    // user-mode traps have a kernel-side stack to land on (the hardware layer:
    // arch_trap_init; x86: the TSS first, since the #DF, NMI and #MC
    // gates name its IST stacks).
    arch_trap_init();
    boot_marker_set(BM_STAGE_TRAPS_LIVE, 0);

    // The 100 Hz scheduler tick.
    // Interrupts stay off in the kernel until the first return into a
    // task (or the idle loop) opens them. The interrupt
    // controller first (x86: IOAPIC + LAPIC, or the 8259s with
    // samos.legacy_pic or without a MADT). The choice of tick
    // source is the arch's; true when every CPU has a tick of its own.
    arch_irq_init();
    bool smp_tick = arch_timer_start(SAMOS_TICK_HZ);

    // e2e hooks: a kernel-mode fault must panic with a
    // register dump; a fault that cannot push its frame must reach
    // the double-fault handler on its own stack.
    if (cmdline_has("samos.kernel_fault_test"))
    {
        arch_fault_test(ARCH_FAULT_TEST_UNDEFINED);
    }
    if (cmdline_has("samos.double_fault_test"))
    {
        arch_fault_test(ARCH_FAULT_TEST_BAD_STACK);
    }

    // Lecture 180 - enumerate PCI devices once traps are up.
    pci_init();
    boot_marker_set(BM_STAGE_PCI_SCANNED, (uint32_t)pci_device_count());

    // Lecture 57 - subsystem initializers. fs_init wires the
    // generic VFS layer (registers the FAT16 driver). disk_
    // search_and_init walks the ATA channels for primary disks
    // and binds the filesystem driver to each. After this we
    // can fopen("0:/foo") from kernel context.
    fs_init();
    boot_marker_set(BM_STAGE_FS_RESOLVED, 0);
    disk_search_and_init();

    // Lecture 82 - moved the GPT scan into disk_search_and_init;
    // gpt_init only reports whether any GPT partition was found.
    gpt_init();

    // Marker fires AFTER gpt_init so the value reflects whether
    // a partition with the right volume label was resolved. With
    // UEFI boot the kernel sees IDE drive disk0 plus GPT-derived
    // partition disks; the SAMOS-labeled p2 should resolve.
    boot_marker_set(BM_STAGE_DISK_INITED,
                    disk_primary_fs_disk() ? (uint32_t)disk_primary_fs_disk()->id + 1 : 0);

    // Lecture 95 - load the system font now that disks exist.
    // The load is best-effort: a missing sysfont.bmp leaves
    // system_font NULL and font_draw_text picks up the absence
    // by returning early when the lookup fails.
    font_system_init();

    // Lecture 100 - set up the terminal module and build the
    // system terminal that covers the whole screen surface.
    // upstream panics when the font load failed; we do too,
    // matching the post-L74 boot model where graphics is
    // mandatory.
    terminal_system_setup();

    // Lecture 196 - keyboard_init must run before
    // window_system_initialize_stage2 because L175's stage 2
    // registers a window-level keyboard listener and
    // keyboard_register_handler resolves NULL to
    // keyboard_default(), which is the head of the
    // inserted-keyboards list. Upstream L196 hoists keyboard_init
    // up here from later in kernel_main.
    keyboard_init();
    boot_marker_set(BM_STAGE_KEYBOARD_INIT, 0);

    // Lecture 122 - bring up the window system after the
    // graphics terminal infrastructure but before any user-mode
    // launch. The stage-2 hook is where mouse + keyboard
    // listener registration will land in later lectures.
    // SamOs deviation from upstream order: mouse_system_init MUST
    // run before window_system_initialize_stage2 because stage2
    // calls mouse_register_move_handler(NULL, ...) which iterates
    // mouse_driver_vector - that vector is allocated by
    // mouse_system_init. Upstream calls stage2 first and only
    // gets away with it because their headless-test coverage
    // never reached this code path. The e2e runtime sweep
    // surfaced it.
    mouse_system_init();
    mouse_system_load_static_drivers();

    window_system_initialize();
    window_system_initialize_stage2();
    boot_marker_set(BM_STAGE_WINDOW_STAGE2, 0);

    // Lecture 150 - graphics stage-2 setup runs AFTER mouse +
    // window are wired so it can register click + move handlers
    // against the fully-initialised pair.
    graphics_setup_stage_two(&default_graphics_info);
    // Value is "has the GOP-provided framebuffer been recorded".
    // The actual address has bits above the 24-bit value slot, so
    // we just signal presence (1) or absence (0).
    boot_marker_set(BM_STAGE_GRAPHICS_UP,
                    default_graphics_info.framebuffer ? 1u : 0u);

    // SamOs e2e: upstream panics if sysfont.bmp is absent
    // or the terminal cannot be created. With the empty
    // bin/os.img the asset is missing and the kernel cannot
    // reach later subsystems. Treat both failures as "skip
    // creating the system_terminal" so headless boot can
    // continue. Any code that needs system_terminal already
    // guards on NULL (terminal_writechar early-returns).
    struct font* system_font_local = font_get_system_font();
    if(system_font_local){
        struct framebuffer_pixel font_color = {0};
        font_color.red = 0xff;
        system_terminal = terminal_create(screen_info, 0, 0,
                                          screen_info->width, screen_info->height,
                                          system_font_local, font_color,
                                          TERMINAL_FLAG_BACKSPACE_ALLOWED);
    }
    // No UART (or samos.fblog): the serial lines on the screen.
    fblog_start();

    // Lecture 50/55 - the TSS is built and loaded by
    // arch_trap_init.

    // Lecture 114 - build the process slot vector before any
    // process_load can touch it.
    process_system_init();
    boot_marker_set(BM_STAGE_PROCESS_INIT, 0);

    print("tss load was fine\n");

    // Lecture 54 - register every syscall handler the user can
    // ask for via int 0x80. isr80h_register_commands fills the
    // isr80h_commands[] table; isr80h_handler dispatches a
    // user-supplied command id into that table on each int 0x80.
    isr80h_register_commands();
    boot_marker_set(BM_STAGE_ISR80H_READY, BM_STAGE_MAX - 1);
    print("register isr80h\n");

    // e2e: run the kernel-side feature self-tests now that all
    // subsystems are up. Each probe writes a 1/0 result into
    // its dedicated marker slot; host-side tests in
    // tests64/e2e/feature-*.sh dump the markers and assert.
    // The NICs, the loopback and the network stack, before the
    // APs start (net/netdev.h).
    net_init();

    kernel_selftest();
    // Only writes to disks labelled SMTEST (test images).
    storage_selftest();

    // Memory-manager self-tests (leak, stress, PFA, slabs).
    memtest_run(cmdline_has("samos.memtest=full"));

    // Logging without serial: the boot's output to the boot
    // volume (the USB stick on a real PC).
    if (cmdline_has("samos.bootlog"))
    {
        kernel_write_bootlog();
    }

    // e2e hooks: power off or reset once the boot is done.
    if (cmdline_has("samos.shutdown_test"))
    {
        acpi_shutdown();
    }
    if (cmdline_has("samos.reboot_test"))
    {
        acpi_reboot();
    }

    // The other CPUs, once everything they could touch
    // exists and before the first process. They need a tick of their
    // own (x86: the LAPIC timer): on the legacy 8259/PIT path the BSP
    // runs alone. samos.nosmp keeps one CPU on a multi-CPU machine.
    if (smp_tick && !cmdline_has("samos.nosmp"))
    {
        arch_start_cpus();
        smp_selftest_run(cmdline_has("samos.smp_panic_test"));
    }

    // SamOs e2e (Phase 2): if a user-side selftest ELF is on the
    // primary FS, load it as the first task and drop into ring 3
    // so it can exercise the user syscall surface (fopen, fread,
    // fseek, fstat, fclose, malloc, free, realloc) and report
    // back through SYSTEM_COMMAND26_E2E_MARK into the BM_USER_*
    // slots. task_run_first_ever_task never returns, so the
    // window_create + spin path below is skipped when SELFTEST.ELF
    // is present. In a normal build without the selftest ELF,
    // process_load_switch returns non-zero and we fall through
    // to the legacy boot path.
    // We load BLANK.ELF as the canonical user-side selftest.
    // blank.c was extended to write its own marker slots (see
    // BM_USER_* in src/bootmarker.h) so the act of loading +
    // running it exercises the full ring-3 syscall surface
    // (fopen, fread, fseek, fstat, fclose, malloc, free,
    // realloc, e2e_mark itself). Bypassing the window_create
    // path below is intentional: it is unreachable in the
    // current build since the window-create path spins forever
    // and `task_run_first_ever_task` never returns either.
    {
        struct process* user_selftest_proc = 0;
        // The boot is logged; from here on the processes own
        // the screen (their output also goes to serial_putc).
        fblog_stop();
        kernel_load_cmdline_programs();
        if(process_load_switch("@:/BLANK.ELF", &user_selftest_proc) == SAMOS_ALL_OK){
            print("e2e: running user-side BLANK.ELF selftest\n");
            process_start(user_selftest_proc);
            // No sti here: arch_task_return sets IF in the iretq
            // frame. An IRQ taken in this window (a latched IRQ1)
            // saved the kernel frame as the task's user state, and
            // the first iretq faulted on SS 0x20.
            task_run_first_ever_task();
            // unreachable
        }
    }

    // Lecture 58 / 196 - keyboard_init was originally called
    // here. L196 moves it before window_system_initialize_stage2
    // so the keyboard listener registration succeeds. The call
    // up at the top of kernel_main is the live one; this site
    // is left as a marker so the audit trail records the move.
    // keyboard_init();

    print("tss ready\n");

    // Lecture 59 - load + run a user program. process_load_switch
    // opens the file, kzallocs a copy of its bytes, builds a
    // task + process around it, and marks it current. Then
    // task_run_first_ever_task iretq's into ring 3 at the user
    // entry point. The simple.bin user program is just
    //   [BITS 64]
    //   jmp $
    // so the user task spins forever; we never come back.
    // Lecture 181 - report the PCI device count to confirm the
    // L180 scan walked the bus tree.
    print("Total PCI devices:");
    print(itoa((int)pci_device_count()));
    print("\n");

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
    // Lecture 100 - the L95 standalone Hello-world draw is gone;
    // the system_terminal now owns the screen and `print` routes
    // through it via terminal_writechar -> terminal_write.

    // Lecture 110 - swap to blank.elf so the L107/L110 fread
    // test program actually runs. SIMPLE.BIN was kept for the
    // L67-L69 ATA-PIO speed budget; with fread under test we
    // accept the longer load time.
    // Lecture 129 - swap the L122 Test Window for a 10-second
    // udelay smoke loop. Each iteration prints "Another second"
    // and waits 1_000_000 microseconds. With the L128 frequency
    // overshoot bug still live the messages burst near-instantly
    // (canary); a frequency-fix follow-up will make them tick at
    // 1 Hz. After the loop the normal user-program path is
    // restored.
    // Lecture 136 - swap the L129 udelay loop for a real
    // window_create test and spin so the L134 position helper
    // + L135 recalculate + L136 redraw chain are exercised
    // under OVMF.
    struct window* win = window_create(graphics_screen_info(), NULL,
                                       "Test Window",
                                       100, 100, 200, 200, 0, -1);
    if(!win){
        print("WIndow creation problem\n");   // sic - upstream typo
    }

    // Lecture 140 - enable interrupts so the L137-L138 PS/2
    // mouse driver can actually fire IRQ12 callbacks.
    // No sti here any more: the timer is live, and a
    // tick before the first task would have nothing to preempt. The
    // iretq into the shell (or the idle loop) enables interrupts.

    // Lecture 208 - upstream switches the default user program
    // from blank.elf to shell.elf. The L136 `while(1){}`
    // that made this load unreachable is gone, so a disk without
    // BLANK.ELF boots straight into the shell. With BLANK.ELF the
    // selftest above starts the shell itself when it is done.
    int res = process_load_switch("@:/shell.elf", &p);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load user program\n");
    }
    print("user enter\n");
    process_start(p);
    // task_run_first_ever_task never returns. After this:
    //   - the task's address space is loaded
    //   - the CPU drops to user mode at SAMOS_PROGRAM_VIRTUAL_ADDRESS
    //   - user code (jmp $) spins forever
    //
    // L52 enabled the timer IRQ callback. With the PIC not yet
    // remapped (L61), IRQ0 landed at vector 0x08 - the same
    // vector the CPU uses for #DF, whose handler panicked "Panic
    // Exception", so the book showed both "user enter" AND "Panic
    // Exception" on screen, with the user code running between
    // them. L61 remaps the PIC to vectors 0x20+ to clear the
    // collision.
    task_run_first_ever_task();

    while (1)
    {
    }
}
