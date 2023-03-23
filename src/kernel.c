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

void kernel_main(void)
{
    terminal_initialize();
    print("Hello 64-bit!\n");

    // L10 kheap probe is back: kernel.asm's PD is again 2-MiB PS=1
    // leaves covering 1 GiB, well past the kheap at 16 MiB.
    kheap_init();
    char* data = kmalloc(50);
    data[0] = 'A';
    data[1] = 'B';
    data[2] = 'C';
    data[3] = 0x00;
    print(data);

    // Lecture 13 - C-side paging probe. Build a fresh PML4 from
    // scratch, identity-map the first ~400 MiB into it (100 * 1024
    // pages = 102400 pages), load it via paging_switch (which does
    // mov cr3, ...). The data buffer at 0x01XXXXXX must still be
    // reachable through the new map, otherwise data[0] = 'M' below
    // would #PF and the second print never happens.
    struct paging_desc* desc = paging_desc_new(PAGING_MAP_LEVEL_4);
    paging_map_range(desc, (void*)0x00000000, (void*)0x00000000,
                     1024 * 100,
                     PAGING_IS_WRITEABLE | PAGING_IS_PRESENT);
    paging_switch(desc);
    data[0] = 'M';
    print(data);

    while (1)
    {
    }
}
