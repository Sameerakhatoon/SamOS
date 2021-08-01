#include "kernel.h"
#include "idt/idt.h"
#include "io/io.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "disk/disk.h"
#include "string/string.h"
#include "fs/pparser.h"
#include "disk/streamer.h"
#include "fs/file.h"
#include "fs/fat/fat16.h"
#include "gdt/gdt.h"
#include "task/tss.h"
#include "task/task.h"
#include "task/process.h"
#include "isr80h/isr80h.h"
#include "keyboard/keyboard.h"
#include "config.h"
#include "status.h"
#include <stddef.h>
#include <stdint.h>

static struct paging_4gb_chunk* kernel_chunk = 0;

struct tss tss;
struct gdt gdt_real[SAMOS_TOTAL_GDT_SEGMENTS];
struct gdt_structured gdt_structured[SAMOS_TOTAL_GDT_SEGMENTS] = {
    { .base = 0x00,            .limit = 0x00,        .type = 0x00 }, // NULL segment
    { .base = 0x00,            .limit = 0xFFFFFFFF,  .type = 0x9A }, // Kernel code segment
    { .base = 0x00,            .limit = 0xFFFFFFFF,  .type = 0x92 }, // Kernel data segment
    { .base = 0x00,            .limit = 0xFFFFFFFF,  .type = 0xF8 }, // User code segment
    { .base = 0x00,            .limit = 0xFFFFFFFF,  .type = 0xF2 }, // User data segment
    { .base = (uint32_t)&tss,  .limit = sizeof(tss), .type = 0xE9 }  // TSS segment
};

uint16_t* video_mem = 0;
uint16_t  terminal_row = 0;
uint16_t  terminal_col = 0;

uint16_t terminal_make_char(char c, char colour){
    return (colour << 8) | c;
}

void terminal_putchar(int x, int y, char c, char colour){
    video_mem[(y * VGA_WIDTH) + x] = terminal_make_char(c, colour);
}

void terminal_backspace(){
    if(terminal_row == 0 && terminal_col == 0){
        return;
    }
    if(terminal_col == 0){
        terminal_row -= 1;
        terminal_col = VGA_WIDTH;
    }
    terminal_col -= 1;
    terminal_writechar(' ', 15);
    terminal_col -= 1;
}

// Post-book extension (extras/terminal-scroll): shift rows 1..24 up
// one row, clear the new bottom row, and pin the cursor on row 24.
// Without this every write past row 24 silently disappeared.
void terminal_scroll(){
    for(int y = 0; y < VGA_HEIGHT - 1; y++){
        for(int x = 0; x < VGA_WIDTH; x++){
            video_mem[y * VGA_WIDTH + x] = video_mem[(y + 1) * VGA_WIDTH + x];
        }
    }
    for(int x = 0; x < VGA_WIDTH; x++){
        video_mem[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = terminal_make_char(' ', 0);
    }
    terminal_row = VGA_HEIGHT - 1;
}

static void serial_putchar(char c);

void terminal_writechar(char c, char colour){
    // Mirror every char that hits the terminal (VGA-bound or routed
    // through terminal_backspace) to COM1 so tests get a deterministic
    // log that contains exactly what the user saw, including putchar
    // (cmd 3) bytes that bypass print().
    serial_putchar(c);

    if(terminal_row >= VGA_HEIGHT){
        terminal_scroll();
    }

    if(c == '\n'){
        terminal_row += 1;
        terminal_col = 0;
        return;
    }

    if(c == 0x08){
        terminal_backspace();
        return;
    }

    terminal_putchar(terminal_col, terminal_row, c, colour);
    terminal_col += 1;
    if(terminal_col >= VGA_WIDTH){
        terminal_col = 0;
        terminal_row += 1;
    }
}

void terminal_initialize(){
    video_mem = (uint16_t*)(0xB8000);
    terminal_row = 0;
    terminal_col = 0;
    for(int y = 0; y < VGA_HEIGHT; y++){
        for(int x = 0; x < VGA_WIDTH; x++){
            terminal_putchar(x, y, ' ', 0);
        }
    }
}

// Post-book extension (extras/serial-mirror): mirror every print() char
// to QEMU's COM1 (-serial file:...) so tests can grep a never-scrolling
// log instead of racing the VGA snapshot against terminal_scroll(). Not
// in McCarthy; tests get a deterministic source of truth.
//   COM1 base   = 0x3F8
//   line-status = base + 5; bit 5 (THR empty) = ready-to-transmit
#define SERIAL_COM1 0x3F8

void serial_init(){
    outb(SERIAL_COM1 + 1, 0x00);   // disable serial IRQs
    outb(SERIAL_COM1 + 3, 0x80);   // DLAB on
    outb(SERIAL_COM1 + 0, 0x03);   // divisor low (38400 baud)
    outb(SERIAL_COM1 + 1, 0x00);   // divisor high
    outb(SERIAL_COM1 + 3, 0x03);   // 8N1, DLAB off
    outb(SERIAL_COM1 + 2, 0xC7);   // FIFO enable + clear
    outb(SERIAL_COM1 + 4, 0x0B);   // OUT2 + RTS + DTR
}

static void serial_putchar(char c){
    while((insb(SERIAL_COM1 + 5) & 0x20) == 0){
    }
    outb(SERIAL_COM1, (unsigned char)c);
}

void print(const char* str){
    int len = strlen(str);
    for(int i = 0; i < len; i++){
        terminal_writechar(str[i], 15);
    }
}

void panic(const char* msg){
    print(msg);
    while(1){}
}

void kernel_page(){
    kernel_registers();
    paging_switch(kernel_chunk);
}

// Helper: emit a 32-bit value as 8 hex digits.
static void print_hex32(unsigned int v){
    static const char hex[] = "0123456789ABCDEF";
    char s[9];
    for(int i = 0; i < 8; i++){
        s[7 - i] = hex[v & 0xF];
        v >>= 4;
    }
    s[8] = 0;
    print(s);
}

void kernel_main(){
    terminal_initialize();
    serial_init();
    print("Hello world!\ntest");

    // Ch 89: reload the GDT from C so future user code/data + TSS
    // descriptors can be added without touching boot.asm. The new
    // GDT mirrors the bootloader's null/kernel-code/kernel-data
    // entries so existing segment-register values stay valid.
    memset(gdt_real, 0x00, sizeof(gdt_real));
    gdt_structured_to_gdt(gdt_real, gdt_structured, SAMOS_TOTAL_GDT_SEGMENTS);
    gdt_load(gdt_real, sizeof(gdt_real));

    kheap_init();
    fs_init();
    disk_search_and_init();
    idt_init();

    // Ch 90: install the TSS as GDT offset 0x28. The CPU consults this
    // on ring-3 -> ring-0 traps to find the kernel stack to switch to.
    memset(&tss, 0x00, sizeof(tss));
    tss.esp0 = 0x600000;
    tss.ss0  = KERNEL_DATA_SELECTOR;
    tss_load(0x28);
    // Post-book marker for tests/35: ltr loaded TR with 0x28; a later
    // ring 3 trap will need TR to find esp0, so reaching this line is
    // proof the TSS descriptor is wired correctly.
    print("\nTSS_OK");

    // Set up identity-mapped paging for the entire 4 GiB virtual space.
    kernel_chunk = paging_new_4gb(
        PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);
    paging_switch(kernel_chunk);
    enable_paging();
    // Post-book marker for tests/14: if paging were broken every later
    // print would page-fault, so seeing this string on serial is proof
    // CR0.PG and CR3 are correct.
    print("\nPAGING_ON");

    isr80h_register_commands();
    keyboard_init();

    // Ch 55 smoke probe: read sector 0 via the new disk_read_block
    // abstraction. The last two bytes must be 55 AA (boot signature).
    char buf[512];
    disk_read_block(disk_get(0), 0, 1, buf);
    print("\nbootsig=");
    print_hex32(((unsigned int)(unsigned char)buf[510] << 8) | (unsigned char)buf[511]);

    // Ch 58 smoke probe: strnlen of "hello" capped at 100 must equal 5.
    print(" slen=");
    print_hex32(strnlen("hello", 100));

    // Ch 64 smoke probe: strcpy must copy "hi" + null terminator,
    // and the returned strlen must be 2.
    char sbuf[8];
    strcpy(sbuf, "hi");
    print(" scp=");
    print(sbuf);
    print_hex32(strlen(sbuf));

    // Ch 69 smoke probes: case-insensitive compare matches, strict
    // compare returns the byte difference, strnlen_terminator stops on
    // the supplied terminator char.
    print(" istr=");
    print_hex32(istrncmp("AbC", "abc", 3) == 0 ? 1 : 0);
    print(" scmp=");
    print_hex32(strncmp("abc", "abd", 3) != 0 ? 1 : 0);
    print(" sterm=");
    print_hex32(strnlen_terminator("foo bar", 100, ' '));

    // Ch 71 smoke probe: memcpy copies "abc" then strlen of the dest
    // is 3. (mbuf is zeroed by declaration of a static-ish stack var.)
    char mbuf[8] = {0};
    memcpy(mbuf, "abc", 3);
    print(" mcp=");
    print_hex32(strlen(mbuf));

    enable_interrupts();

    // Ch 60 smoke probe: stream 2 bytes from byte position 0x1FE (the
    // last two bytes of sector 0, i.e. the boot signature 0x55 0xAA).
    struct disk_stream* stream = diskstreamer_new(0);
    diskstreamer_seek(stream, 0x1FE);
    unsigned char ss[2] = { 0, 0 };
    diskstreamer_read(stream, ss, 2);
    diskstreamer_close(stream);
    print("\nss=");
    print_hex32(((unsigned int)ss[0] << 8) | (unsigned int)ss[1]);

    // Ch 73 smoke probe: fread of fd=0 hits the EINVARG early-return
    // before any dispatch. Cast -EINVARG (-2) to unsigned -> FFFFFFFE.
    char fbuf[4];
    print(" frd=");
    print_hex32((unsigned int)fread(fbuf, 1, 1, 0));

    // Ch 77 smoke probe: fopen hello.txt, fseek 2 bytes forward, fread 11.
    // hello.txt contents are "Hello world!\n" so we expect "llo world!\n"
    // after the seek. Print as "hs=llo world!".
    int fd = fopen("0:/hello.txt", "r");
    if(fd){
        char hbuf[14];
        fseek(fd, 2, SEEK_SET);
        fread(hbuf, 11, 1, fd);
        hbuf[11] = 0x00;
        print("\nhs=");
        print(hbuf);

        // Ch 79 smoke probe: fstat the same descriptor. hello.txt is
        // 13 bytes, not read-only -> filesize=13, flags=0.
        struct file_stat s = { 0 };
        fstat(fd, &s);
        print(" fz=");   print_hex32(s.filesize);
        print(" ffl=");  print_hex32(s.flags);

        // Ch 81 smoke probe: fclose the descriptor and print after.
        // If fclose double-frees or smashes the heap we wouldn't see
        // "afterclose" come out.
        fclose(fd);
        print(" afterclose=ok");
    } else {
        print("\nhs=NOPEN");
    }

    // Ch 65 smoke probe: FAT16 driver registered itself with the VFS,
    // and disk.filesystem was filled in via fs_resolve. Print the name.
    print(" fs=");
    print(disk_get(0)->filesystem ? disk_get(0)->filesystem->name : "NONE");

    // Ch 66 smoke probe: packed FAT16 structs must have the on-disk
    // sizes the spec demands. Expect "fszs=00000001".
    print(" fszs=");
    print_hex32(fat16_check_sizes());

    // Ch 67 smoke probe: disk.id was initialised to 0 in
    // disk_search_and_init.
    print(" did=");
    print_hex32((unsigned int)disk_get(0)->id);

    // Ch 68 smoke probe: fat16_resolve allocated a fat_private blob and
    // stored it; disk.fs_private should now be a heap pointer (non-zero,
    // upper byte 0x01). The FAT16 root directory should have zero entries
    // because we haven't mcopy'd any files into the volume yet.
    print(" pnz=");
    print_hex32(disk_get(0)->fs_private != 0 ? 1 : 0);
    print(" rdt=");
    print_hex32((unsigned int)fat16_root_dir_total(disk_get(0)));

    // kmalloc smoke probe: two distinct allocations and one free.
    void* p1 = kmalloc(100);
    void* p2 = kmalloc(8000);
    print("\nkm1=");  print_hex32((unsigned int)p1);
    print(" km2=");   print_hex32((unsigned int)p2);
    kfree(p1);
    void* p3 = kmalloc(100);
    print(" km3=");   print_hex32((unsigned int)p3);
    print("\n");

    // Ch 98: the actual ring-3 launch is deferred - see
    // docs/gotchas/G04-iret-to-ring3-resets.md. The CPU triple-faults
    // shortly after iretd in this build of the kernel, which would
    // knock out the rest of the test suite. The supporting changes
    // for Ch 98 (current_task set when the first task is created,
    // registers.cs = USER_CODE_SEGMENT) are already in task.c so the
    // wiring is correct; we just don't pull the trigger yet. Once
    // G04 is investigated and patched, the three lines below get
    // un-commented and test 37 starts asserting CS=0x1b, EIP=0x400000.
    print("\nentering userland... (deferred, G04)");
    struct process* process = 0;

    // Ch 150 grand finale demo: load TWO blank.elf instances with different
    // argv. The timer-driven round-robin will print "Testing!" and "Abc!"
    // interleaved on VGA, proving multitasking works.
    //
    // G09: disable interrupts across the dual-load. Once the first task
    // exists and process_load_switch sets it as current_process, the very
    // next PIT IRQ runs idt_clock -> task_next, which iretds into task 1
    // and never returns to finish loading task 2. Re-enabled by task_return
    // setting IF=1 in the user EFLAGS image.
    disable_interrupts();

    int res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    struct command_argument argument;
    strcpy(argument.argument, "Testing!");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "Abc!");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    // Behavioural-coverage tasks. Each blank.elf instance dispatches
    // on argv[0] and exercises one Ch 145-149 user-visible path; the
    // surviving Testing!/Abc! pair keeps the scheduler scrolling once
    // the transients have died.
    //   CRASH -> idt_handle_exception (Ch 147) on a user fault
    //   EXIT  -> samos_exit / cmd 9    (Ch 148)
    //   BS    -> terminal_backspace    (Ch 118) via "BS-ABC\b\b\bXYZ"
    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "CRASH");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "EXIT");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "BS");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "MF");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "PC");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "KEY");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "LD");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "SH");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "IT");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "PF");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    res = process_load_switch("0:/blank.elf", &process);
    if(res != SAMOS_ALL_OK){
        panic("Failed to load blank.elf\n");
    }
    strcpy(argument.argument, "RL");
    argument.next = 0x00;
    process_inject_arguments(process, &argument);

    task_run_first_ever_task();
}
