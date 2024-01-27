#ifndef ARCH_X86_64_SEGMENTS_H
#define ARCH_X86_64_SEGMENTS_H
// GDT layout, selectors, TSS and IST sizes, moved here from
// config.h. kernel.asm has its own copies of the selectors.

// Lecture 37 - the 64-bit code segment selector lives at GDT
// offset 0x18 (kernel.asm's GDT entry with L=1).
#define KERNEL_LONG_MODE_CODE_SELECTOR  0x18

// Lecture 55 - TSS selector. The 64-bit TSS descriptor lives
// in GDT slot 7 (its high half spills into slot 8). Selector
// = slot * 8 = 0x38.
#define KERNEL_LONG_MODE_TSS_SELECTOR   0x38

// Lecture 49 - TSS descriptor access byte.
//   bit 7 (P)     = 1  present
//   bits 5..6 (DPL) = 00  ring 0
//   bit 4 (S)     = 0  system descriptor (TSS lives in system class)
//   bits 0..3 (type) = 1001 = available 64-bit TSS
//   = 1000 1001 = 0x89
#define TSS_DESCRIPTOR_TYPE  0x89

// Lecture 50 - GDT slot of the TSS descriptor.
#define KERNEL_LONG_MODE_TSS_GDT_INDEX   7

#define SAMOS_TOTAL_INTERRUPTS  256

// Stacks the TSS IST slots switch to for #DF, NMI and #MC.
#define KERNEL_IST_STACK_SIZE   (16 * 1024)

// A 1 MiB ring-0 stack per CPU (TSS rsp0), guard page at the
// bottom.
#define CPU_KERNEL_STACK_SIZE   (1024 * 1024)

// Lecture 40 + L41 fix - user segment selectors point at the
// L39 user GDT entries with RPL=3 OR'd in. The L39 GDT lays out
// the user code seg at slot 0x28 and the user data seg at
// 0x30, so with RPL=3:
//   USER_CODE_SEGMENT = 0x28 | 3 = 0x2B
//   USER_DATA_SEGMENT = 0x30 | 3 = 0x33
//
// SamOs deviation: upstream PeachOS64 swaps these two macros
// (#define USER_DATA_SEGMENT 0x2B / USER_CODE_SEGMENT 0x33)
// which contradicts the GDT layout - their task.c then assigns
// task->registers.cs = USER_CODE_SEGMENT = 0x33 (their "code"
// macro), but 0x33 indexes the data seg descriptor. It works
// on QEMU because long-mode SS / CS validation is loose, but
// it's wrong. We use the correct mapping.
#define USER_CODE_SEGMENT       0x2B
#define USER_DATA_SEGMENT       0x33

#endif
