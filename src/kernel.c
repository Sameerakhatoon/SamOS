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
#include "memory/heap/kheap.h"
#include "memory/heap/heap.h"
#include "memory/memory.h"
#include "memory/paging/paging.h"
#include "string/string.h"
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

    while (1)
    {
    }
}
