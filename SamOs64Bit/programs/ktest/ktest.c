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
// Isolation probes:
//   ktest kwrite|kread|kexec <hex addr>   touch a kernel address -> #PF
//   ktest stack       recurse into the stack guard page        -> #PF
//   ktest nxstack|nxheap|nxdata  run a `ret` from a data page   -> #PF
//   ktest wtext       write to its own code                     -> #PF
//   ktest bss         .bss is zero and writable ("BSS OK")
//   ktest kptr        kernel and bad pointers to syscalls ("KPTR OK")
//   ktest longpath    oversized paths and argument lists ("LONGPATH OK")
//   ktest badelf F... syscall 7 on each file, all refused ("BADELF DONE")

#include "samos.h"
#include "string.h"
#include "memory.h"
#include "stdlib.h"

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

// ---------------------------------------------------------------
// Address-space isolation probes.
// ---------------------------------------------------------------

#define CMD_PRINT        1
#define CMD_FREE         5
#define CMD_LOAD_START   6
#define CMD_SYSTEM       7
#define CMD_GET_ARGS     8
#define CMD_FOPEN        10
#define CMD_FREAD        12
#define CMD_FSEEK        13
#define CMD_FSTAT        14
#define CMD_REALLOC      15
#define CMD_WINDOW_EVENT 18
#define CMD_PIXELS_GET   20
#define CMD_GRAPHICS_NEW 22

// kernel status.h
#define S_EINVARG       (-2)
#define S_EFAULT        (-20)
#define S_ENAMETOOLONG  (-21)

#define KERNEL_TEXT     0xFFFFFFFF80100000ul   // first kernel page (.boot)
#define KERNEL_MARKERS  0xFFFF800000006000ul   // boot markers via the direct map

// regs.asm
extern long ktest_syscall(long cmd, long a0, long a1, long a2, long a3);
extern long ktest_syscall_rsp(long cmd, unsigned long rsp);

static unsigned long parse_hex(const char* p)
{
    unsigned long v = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        v = v * 16 + (unsigned long)d;
    }
    return v;
}

static int kptr_failures;

static void expect(const char* what, long got, long want)
{
    print("KPTR ");
    print(what);
    if (got == want) {
        print(" ok\n");
        return;
    }
    print(" FAIL got ");
    print_num((unsigned long)got, 16);
    print("\n");
    kptr_failures++;
}

// Kernel and bad pointers handed to syscalls must come back as
// errors, without a kernel fault and without the kernel reading or
// writing through them.
static int kptr_test(void)
{
    long k = (long)KERNEL_TEXT;
    long m = (long)KERNEL_MARKERS;

    expect("print kernel string", ktest_syscall(CMD_PRINT, k, 0, 0, 0), S_EFAULT);
    expect("fopen kernel path", ktest_syscall(CMD_FOPEN, k, (long)"r", 0, 0), S_EFAULT);
    expect("fopen kernel mode", ktest_syscall(CMD_FOPEN, (long)"@:/HELLO.TXT", k, 0, 0), S_EFAULT);

    long fd = ktest_syscall(CMD_FOPEN, (long)"@:/HELLO.TXT", (long)"r", 0, 0);
    expect("fopen real file", fd > 0, 1);
    expect("fread into kernel text", ktest_syscall(CMD_FREAD, k, 1, 16, fd), S_EFAULT);
    expect("fread into kernel data", ktest_syscall(CMD_FREAD, m, 1, 16, fd), S_EFAULT);
    expect("fstat into kernel", ktest_syscall(CMD_FSTAT, fd, k, 0, 0), S_EFAULT);
    expect("fread fd not owned", ktest_syscall(CMD_FREAD, (long)&fd, 1, 1, 12345), S_EINVARG);
    expect("fstat fd not owned", ktest_syscall(CMD_FSTAT, 12345, (long)&fd, 0, 0), S_EINVARG);
    expect("fseek fd not owned", ktest_syscall(CMD_FSEEK, 12345, 0, 0, 0) < 0, 1);

    expect("get_arguments into kernel", ktest_syscall(CMD_GET_ARGS, k, 0, 0, 0), S_EFAULT);
    expect("system with kernel list", ktest_syscall(CMD_SYSTEM, k, 0, 0, 0), S_EFAULT);
    expect("realloc kernel pointer", ktest_syscall(CMD_REALLOC, k, 16, 0, 0), 0);
    expect("free kernel pointer", ktest_syscall(CMD_FREE, k, 0, 0, 0), 0);
    expect("window event into kernel", ktest_syscall(CMD_WINDOW_EVENT, k, 0, 0, 0), S_EFAULT);
    expect("pixels for kernel handle", ktest_syscall(CMD_PIXELS_GET, k, 0, 0, 0), 0);
    expect("graphics for kernel handle", ktest_syscall(CMD_GRAPHICS_NEW, k, 1, 1, 1), 0);
    expect("syscall with kernel rsp", ktest_syscall_rsp(CMD_PRINT, KERNEL_TEXT), S_EFAULT);

    // A string and a buffer that run into the unmapped guard page
    // after a heap allocation: the kernel must stop at the page.
    char* page = malloc(4096);
    expect("malloc page", page != 0, 1);
    if (page) {
        memset(page, 'A', 4096);
        // 16 bytes without a NUL, then the guard page.
        expect("print past the page", ktest_syscall(CMD_PRINT, (long)(page + 4096 - 16), 0, 0, 0),
               S_EFAULT);
        expect("fopen unterminated path", ktest_syscall(CMD_FOPEN, (long)page, (long)"r", 0, 0),
               S_ENAMETOOLONG);
        expect("fread past the page", ktest_syscall(CMD_FREAD, (long)(page + 4000), 1, 200, fd),
               S_EFAULT);
        expect("fread inside the page", ktest_syscall(CMD_FREAD, (long)page, 1, 5, fd), 5);
    }

    // The kernel still serves this process normally.
    expect("print still works", ktest_syscall(CMD_PRINT, (long)"KPTR still printing\n", 0, 0, 0), 0);

    if (kptr_failures) {
        print("KPTR FAIL ");
        print_num(kptr_failures, 10);
        print("\n");
        return 1;
    }
    print("KPTR OK\n");
    return 0;
}

// The book strcpy'd a user path after "@:/" into a 108-byte kernel
// stack buffer (syscall 6) and walked user-supplied argument lists
// without a bound (syscall 7).
static int longpath_test(void)
{
    static char name[600];
    static struct command_argument arg;

    memset(name, 'A', 300);
    name[300] = 0;
    expect("load_start 300-byte name", ktest_syscall(CMD_LOAD_START, (long)name, 0, 0, 0),
           S_ENAMETOOLONG);
    expect("fopen 300-byte path", ktest_syscall(CMD_FOPEN, (long)name, (long)"r", 0, 0),
           S_ENAMETOOLONG);

    // 104 bytes fit after "@:/" in SAMOS_MAX_PATH (108): not found,
    // not too long. 105 do not.
    name[104] = 0;
    expect("load_start 104-byte name", ktest_syscall(CMD_LOAD_START, (long)name, 0, 0, 0) !=
           S_ENAMETOOLONG, 1);
    name[104] = 'A';
    name[105] = 0;
    expect("load_start 105-byte name", ktest_syscall(CMD_LOAD_START, (long)name, 0, 0, 0),
           S_ENAMETOOLONG);

    // A full 512-byte argument with no NUL.
    memset(arg.argument, 'B', sizeof(arg.argument));
    arg.next = 0;
    expect("system unterminated argument", ktest_syscall(CMD_SYSTEM, (long)&arg, 0, 0, 0),
           S_ENAMETOOLONG);
    // A list that loops on itself: the kernel stops counting.
    strcpy(arg.argument, "NOSUCH.ELF");
    arg.next = &arg;
    expect("system looping argument list", ktest_syscall(CMD_SYSTEM, (long)&arg, 0, 0, 0),
           S_EINVARG);

    // A print longer than the kernel buffer is cut short, not
    // overflowed.
    memset(name, 'C', sizeof(name) - 2);
    name[sizeof(name) - 2] = '\n';
    name[sizeof(name) - 1] = 0;
    expect("print 600 bytes", ktest_syscall(CMD_PRINT, (long)name, 0, 0, 0), 0);

    if (kptr_failures) {
        print("LONGPATH FAIL\n");
        return 1;
    }
    print("LONGPATH OK\n");
    return 0;
}

// Each named file must be refused by syscall 7; nothing may run.
static int badelf_test(int argc, char** argv)
{
    static struct command_argument arg;
    for (int i = 2; i < argc; i++) {
        memset(&arg, 0, sizeof(arg));
        strncpy(arg.argument, argv[i], sizeof(arg.argument) - 1);
        long res = ktest_syscall(CMD_SYSTEM, (long)&arg, 0, 0, 0);
        print("BADELF ");
        print(argv[i]);
        print(res < 0 ? " rejected " : " ACCEPTED ");
        print_num((unsigned long)res, 16);
        print("\n");
        if (res >= 0) {
            kptr_failures++;
        }
    }
    print(kptr_failures ? "BADELF FAIL\n" : "BADELF DONE\n");
    return kptr_failures != 0;
}

static volatile int recurse_forever = 1;

static int recurse(int depth)
{
    volatile char buf[512];
    buf[0] = (char)depth;
    return (recurse_forever ? recurse(depth + 1) : 0) + buf[0];
}

// .bss must be zero and writable, in its own segment.
static char bss_block[3 * 4096];
static int  data_word = 0x5A5A;

static int bss_test(void)
{
    for (unsigned i = 0; i < sizeof(bss_block); i++) {
        if (bss_block[i]) {
            print("BSS FAIL: not zero\n");
            return 1;
        }
        bss_block[i] = (char)i;
    }
    if (data_word != 0x5A5A) {
        print("BSS FAIL: .data\n");
        return 1;
    }
    print("BSS OK\n");
    return 0;
}

static unsigned char data_code[16] = { 0xC3 };   // ret, in .data

// Returns 1 if the mode was one of these, after the fault (which
// kills the process, so a return means the fault did not happen).
static int isolation_mode(const char* mode, int argc, char** argv)
{
    unsigned long addr = argc > 2 ? parse_hex(argv[2]) : 0;
    if (strncmp(mode, "kwrite", 7) == 0) {
        print("KTEST kwrite ");
        print_num(addr, 16);
        print("\n");
        *(volatile unsigned long*)addr = 0x4B4B4B4Bul;
    } else if (strncmp(mode, "kread", 6) == 0) {
        print("KTEST kread ");
        print_num(addr, 16);
        print("\n");
        volatile unsigned long v = *(volatile unsigned long*)addr;
        (void)v;
    } else if (strncmp(mode, "kexec", 6) == 0) {
        print("KTEST kexec ");
        print_num(addr, 16);
        print("\n");
        ((void (*)(void))addr)();
    } else if (strncmp(mode, "stack", 6) == 0) {
        print("KTEST stack: recursing\n");
        recurse(0);
    } else if (strncmp(mode, "nxstack", 8) == 0) {
        unsigned char code[16] = { 0xC3 };
        print("KTEST nxstack\n");
        ((void (*)(void))code)();
    } else if (strncmp(mode, "nxheap", 7) == 0) {
        unsigned char* code = malloc(16);
        code[0] = 0xC3;
        print("KTEST nxheap\n");
        ((void (*)(void))code)();
    } else if (strncmp(mode, "nxdata", 7) == 0) {
        print("KTEST nxdata\n");
        ((void (*)(void))data_code)();
    } else if (strncmp(mode, "wtext", 6) == 0) {
        print("KTEST wtext\n");
        *(volatile unsigned char*)(void*)isolation_mode = 0xCC;
    } else {
        return 0;
    }
    print("KTEST survived the fault (bug)\n");
    return 1;
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
    } else if (strncmp(mode, "kptr", 5) == 0) {
        return kptr_test();
    } else if (strncmp(mode, "longpath", 9) == 0) {
        return longpath_test();
    } else if (strncmp(mode, "badelf", 7) == 0) {
        return badelf_test(argc, argv);
    } else if (strncmp(mode, "bss", 4) == 0) {
        return bss_test();
    } else if (isolation_mode(mode, argc, argv)) {
        return 1;
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
