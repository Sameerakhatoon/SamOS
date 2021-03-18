#include "kernel.h"
#include "idt/idt.h"
#include "io/io.h"
#include "memory/heap/kheap.h"
#include "memory/paging/paging.h"
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

size_t strlen(const char* str){
    size_t len = 0;
    while(str[len]){
        len++;
    }
    return len;
}

void print(const char* str){
    size_t len = strlen(str);
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
    idt_init();

    // Set up identity-mapped paging for the entire 4 GiB virtual space.
    kernel_chunk = paging_new_4gb(
        PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);
    paging_switch(paging_4gb_chunk_get_directory(kernel_chunk));

    // Ch 52 demo: remap virtual 0x1000 onto a fresh heap page so two
    // pointers (ptr and the literal 0x1000) alias the same physical bytes.
    char* ptr = kzalloc(4096);
    paging_set(paging_4gb_chunk_get_directory(kernel_chunk), (void*)0x1000,
               (uint32_t)ptr | PAGING_ACCESS_FROM_ALL | PAGING_IS_PRESENT | PAGING_IS_WRITEABLE);

    enable_paging();

    char* ptr2 = (char*)0x1000;
    ptr2[0] = 'A';
    ptr2[1] = 'B';
    print("\nremap:");
    print(ptr2);
    print(ptr);

    enable_interrupts();

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
