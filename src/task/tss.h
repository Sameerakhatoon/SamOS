#ifndef TASKSWITCHSEGMENT_H
#define TASKSWITCHSEGMENT_H

#include <stdint.h>

// Lecture 50 - long-mode TSS.
//
// In 64-bit mode the TSS no longer holds a full CPU state -
// just a set of stack pointers for ring transitions plus a
// small reserved tail. rsp0 is what we actually care about:
// the kernel stack the CPU loads on a ring-3 -> ring-0
// transition (interrupts from user mode, int 0x80 syscalls).
//
// iopb_offset = sizeof(tss) disables the I/O permission bitmap
// (the offset points past the end of the TSS, so the CPU
// concludes "no IOPB present").
struct tss {
    uint16_t link;
    uint16_t reserved0;

    uint64_t rsp0;        // kernel stack for CPL=0 entries
    uint64_t rsp1;        // CPL=1 (unused on amd64)
    uint64_t rsp2;        // CPL=2 (unused on amd64)

    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

void tss_load(int tss_segment);

#endif
