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
#include <stddef.h>
#include <stdint.h>

static struct paging_4gb_chunk* kernel_chunk = 0;

uint16_t* video_mem = 0;
uint16_t  terminal_row = 0;
uint16_t  terminal_col = 0;

uint16_t terminal_make_char(char c, char colour){
    return (colour << 8) | c;
}

void terminal_putchar(int x, int y, char c, char colour){
    video_mem[(y * VGA_WIDTH) + x] = terminal_make_char(c, colour);
}

void terminal_writechar(char c, char colour){
    if(c == '\n'){
        terminal_row += 1;
        terminal_col = 0;
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

void print(const char* str){
    int len = strlen(str);
    for(int i = 0; i < len; i++){
        terminal_writechar(str[i], 15);
    }
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
    print("Hello world!\ntest");

    kheap_init();
    fs_init();
    disk_search_and_init();
    idt_init();

    // Set up identity-mapped paging for the entire 4 GiB virtual space.
    kernel_chunk = paging_new_4gb(
        PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);
    paging_switch(paging_4gb_chunk_get_directory(kernel_chunk));
    enable_paging();

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

    // Ch 70 smoke probes: fopen now dispatches through the VFS.
    //   fop_ok  = fopen of a well-formed path returns descriptor index 1
    //             (fat16_open is still a stub returning null).
    //   fop_bad = fopen with a malformed path returns 0.
    //   fop_mod = fopen with an unknown mode returns 0.
    print(" fop_ok=");
    print_hex32((unsigned int)fopen("0:/anything", "r"));
    print(" fop_bad=");
    print_hex32((unsigned int)fopen("notapath", "r"));
    print(" fop_mod=");
    print_hex32((unsigned int)fopen("0:/anything", "z"));

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
}
