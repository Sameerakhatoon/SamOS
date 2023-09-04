// ktest - kernel behaviour probes run from user mode.
//
// Not from the book. The e2e tests start it through CMDLINE.TXT
// (samos.run=KTEST.ELF <mode> ..., see kernel.c), next to BLANK.ELF.
//
//   ktest de          divide by zero               -> #DE, process killed
//   ktest gp          hlt in ring 3                -> #GP, process killed
//   ktest pf          write to an unmapped page    -> #PF, process killed
//   ktest ud          ud2                          -> #UD, process killed
//   ktest regs <seed> load all 15 GPRs and xmm0-15, make a syscall
//                     that returns directly (sum) and one that switches
//                     tasks (udelay), check nothing changed; prints
//                     "REGS OK <seed>" or "REGS FAIL ...".
//   ktest             prints "KTEST HELLO"
//
// No static or global writable data: the loader does not give a
// program fresh .bss pages yet, so everything lives on the stack.

#include "samos.h"
#include "string.h"

#define REGS_SLOTS   (15 + 32)   // rax..r15, then xmm0-15 as 2 qwords each
#define REGS_ROUNDS  20

#define CMD_SUM      0
#define CMD_UDELAY   25

// regs.asm
extern void ktest_regs_roundtrip(unsigned long* in, unsigned long* out,
                                 unsigned long cmd, unsigned long arg);

static const char* slot_name(int i)
{
    const char* names[15] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8",
        "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    };
    return i < 15 ? names[i] : "xmm";
}

static void print_num(unsigned long v, int base)
{
    char buf[24];
    int i = sizeof(buf) - 1;
    buf[i] = 0;
    do {
        buf[--i] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    if (base == 16) {
        buf[--i] = 'x';
        buf[--i] = '0';
    }
    print(&buf[i]);
}

static int regs_check(unsigned long seed, int round, const char* what,
                      unsigned long* in, unsigned long* out)
{
    for (int i = 0; i < REGS_SLOTS; i++) {
        if (in[i] != out[i]) {
            print("REGS FAIL ");
            print_num(seed, 10);
            print(" round ");
            print_num(round, 10);
            print(" after ");
            print(what);
            print(": ");
            print(slot_name(i));
            if (i >= 15) {
                print_num((i - 15) / 2, 10);
                print((i - 15) % 2 ? ".hi" : ".lo");
            }
            print(" expected ");
            print_num(in[i], 16);
            print(" got ");
            print_num(out[i], 16);
            print("\n");
            return 1;
        }
    }
    return 0;
}

static int regs_test(unsigned long seed)
{
    unsigned long in[REGS_SLOTS];
    unsigned long out[REGS_SLOTS];
    for (int round = 0; round < REGS_ROUNDS; round++) {
        for (int i = 0; i < REGS_SLOTS; i++) {
            in[i] = (seed << 56) ^ ((unsigned long)round << 40) ^
                    (0x0101010101ul * (i + 1)) ^ 0x5A5A0000ul;
        }

        // A syscall that returns straight through the trap frame.
        // Command 0 adds its two stack items: rax = 2 * arg.
        unsigned long arg = round + 1;
        in[0] = 2 * arg;
        ktest_regs_roundtrip(in, out, CMD_SUM, arg);
        if (regs_check(seed, round, "sum", in, out)) {
            return 1;
        }

        // A syscall that sleeps, runs the other tasks and comes back
        // through task_enter: rax = 0.
        in[0] = 0;
        ktest_regs_roundtrip(in, out, CMD_UDELAY, 1000);
        if (regs_check(seed, round, "udelay", in, out)) {
            return 1;
        }
    }
    print("REGS OK ");
    print_num(seed, 10);
    print("\n");
    return 0;
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "";

    if (strncmp(mode, "de", 3) == 0) {
        print("KTEST de: dividing by zero\n");
        volatile int zero = 0;
        volatile int x = 10 / zero;
        (void)x;
    } else if (strncmp(mode, "gp", 3) == 0) {
        print("KTEST gp: executing hlt in ring 3\n");
        __asm__ volatile("hlt");
    } else if (strncmp(mode, "pf", 3) == 0) {
        print("KTEST pf: writing to an unmapped page\n");
        *(volatile unsigned long*)0x0000700000000000ul = 1;
    } else if (strncmp(mode, "ud", 3) == 0) {
        print("KTEST ud: ud2\n");
        __asm__ volatile("ud2");
    } else if (strncmp(mode, "regs", 5) == 0) {
        unsigned long seed = 0;
        for (const char* p = argc > 2 ? argv[2] : "1"; *p >= '0' && *p <= '9'; p++) {
            seed = seed * 10 + (unsigned long)(*p - '0');
        }
        return regs_test(seed);
    } else {
        print("KTEST HELLO\n");
        return 0;
    }
    print("KTEST survived the fault (bug)\n");
    return 1;
}
