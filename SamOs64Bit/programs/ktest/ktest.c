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
//
// Scheduler and process probes:
//   ktest spin TAG N  N CPU-bound rounds, a "SPIN TAG i" line each
//   ktest sleep MS    sleep_ms(MS), print how long it took by uptime
//   ktest exit CODE   print its pid, return CODE
//   ktest parent      waitpid/kill/getpid checks on children ("PARENT OK")
//   ktest keys        window + child window, keys go to the focused one

#include "samos.h"
#include "string.h"
#include "memory.h"
#include "stdlib.h"
#include "stdio.h"

#define REGS_SLOTS   (15 + 32)   // rax..r15, then xmm0-15 as 2 qwords each
#define REGS_ROUNDS  20

#define CMD_SUM      0
#define CMD_UDELAY   25

// regs.asm
extern void ktest_regs_roundtrip(unsigned long* in, unsigned long* out,
                                 unsigned long cmd);

// in[] slots of the syscall argument registers.
#define SLOT_RSI 4
#define SLOT_RDI 5

static const char* slot_name(int i)
{
    const char* names[15] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8",
        "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    };
    return i < 15 ? names[i] : "xmm";
}

// Format a whole line and print it with one syscall: with preemption
// another process's output can land between two print calls.
static void say(const char* fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    print(line);
}

static int regs_check(unsigned long seed, int round, const char* what,
                      unsigned long* in, unsigned long* out)
{
    for (int i = 0; i < REGS_SLOTS; i++) {
        if (in[i] != out[i]) {
            char xmm[8] = "";
            if (i >= 15) {
                snprintf(xmm, sizeof(xmm), "%d%s", (i - 15) / 2, (i - 15) % 2 ? ".hi" : ".lo");
            }
            say("REGS FAIL %lu round %d after %s: %s%s expected %p got %p\n", seed, round, what,
                slot_name(i), xmm, (void*)in[i], (void*)out[i]);
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
        // Command 0 adds its two arguments (rdi, rsi): rax = 2 * arg.
        unsigned long arg = round + 1;
        in[SLOT_RDI] = arg;
        in[SLOT_RSI] = arg;
        in[0] = 2 * arg;
        ktest_regs_roundtrip(in, out, CMD_SUM);
        if (regs_check(seed, round, "sum", in, out)) {
            return 1;
        }

        // A syscall that sleeps, runs the other tasks and comes back
        // through task_enter: rax = 0. rdi = 1000 microseconds.
        in[SLOT_RDI] = 1000;
        in[0] = 0;
        ktest_regs_roundtrip(in, out, CMD_UDELAY);
        if (regs_check(seed, round, "udelay", in, out)) {
            return 1;
        }
    }
    say("REGS OK %lu\n", seed);
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
#define S_ECHILD        (-23)
#define S_ESRCH         (-24)

// SAMOS_KERNEL_VMA + 1 MiB: the higher-half alias of the physical page
// the kernel image starts at. That page holds .boot, which is linked
// and run at its low load address, so the kernel's high mapping starts
// at .text a few pages later and this address is not mapped at all.
// The kptr checks still hold: syscalls refuse any pointer at or above
// SAMOS_USER_SPACE_END before looking at the page tables.
#define KERNEL_TEXT     0xFFFFFFFF80100000ul
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
    if (got == want) {
        say("KPTR %s ok\n", what);
        return;
    }
    say("KPTR %s FAIL got %p\n", what, (void*)got);
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
        say("KPTR FAIL %d\n", kptr_failures);
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
        say("BADELF %s %s %p\n", argv[i], res < 0 ? "rejected" : "ACCEPTED", (void*)res);
        if (res >= 0) {
            kptr_failures++;
        }
    }
    print(kptr_failures ? "BADELF FAIL\n" : "BADELF DONE\n");
    return kptr_failures != 0;
}

// ---------------------------------------------------------------
// Scheduler and process probes.
// ---------------------------------------------------------------

#define SPIN_LOOPS   20000000ul
#define KEY_PRESS_EVENT 6            // kernel WINDOW_EVENT_TYPE_KEY_PRESS

// struct window_event_userland (kernel graphics/window.h)
struct ktest_window_event {
    int   type;
    int   win_id;
    void* window;
    struct {
        int x;
        int y;
    } data;                          // a key press keeps its key in x
};

static unsigned long parse_dec(const char* p)
{
    unsigned long v = 0;
    for (; p && *p >= '0' && *p <= '9'; p++) {
        v = v * 10 + (unsigned long)(*p - '0');
    }
    return v;
}

// With preemption another task can print between two print()
// calls, so each line goes out in one call.
static void print_line(const char* a, const char* b, unsigned long n)
{
    char line[160];
    snprintf(line, sizeof(line), "%s%s%lu\n", a, b, n);
    print(line);
}

// Two of these run at once: without preemption the first prints
// every line before the second prints one (print never yields).
static int spin_test(const char* tag, unsigned long rounds)
{
    char what[64];
    unsigned long start = samos_uptime();
    snprintf(what, sizeof(what), "%s ", tag);
    for (unsigned long i = 0; i < rounds; i++) {
        for (volatile unsigned long n = 0; n < SPIN_LOOPS; n++) {
        }
        print_line("SPIN ", what, i);
    }
    snprintf(what, sizeof(what), "%s done ms ", tag);
    print_line("SPIN ", what, samos_uptime() - start);
    return 0;
}

static int sleep_test(unsigned long ms)
{
    char line[96];
    unsigned long start = samos_uptime();
    samos_sleep_ms(ms);
    unsigned long took = samos_uptime() - start;
    snprintf(line, sizeof(line), "SLEEP %lu ms took %lu\n", ms, took);
    print(line);
    return 0;
}

static int exit_test(unsigned long code)
{
    char line[96];
    snprintf(line, sizeof(line), "EXIT pid %d code %lu\n", samos_getpid(), code);
    print(line);
    return (int)code;
}

static int parent_failures;

static void parent_expect(const char* what, long got, long want)
{
    char line[160];
    snprintf(line, sizeof(line), "PARENT %s %s %ld\n", what, got == want ? "ok" : "FAIL got", got);
    print(line);
    if (got != want) {
        parent_failures++;
    }
}

// Start `cmd`, wait for it, return its status (-1000 on error).
static int parent_run(const char* cmd, int* pid_out)
{
    char line[160];
    int status = -1000;
    int pid = samos_system_run(cmd);
    snprintf(line, sizeof(line), "PARENT started %s pid %d\n", cmd, pid);
    print(line);
    *pid_out = pid;
    if (pid <= 0) {
        return -1000;
    }
    int got = samos_waitpid(pid, &status);
    parent_expect("waitpid returns the child's pid", got, pid);
    return status;
}

// waitpid hands the parent its child's exit status: from exit(), from
// a fault, from kill. getpid differs between parent and child.
static int parent_test(void)
{
    int pid = 0;
    int self = samos_getpid();
    print_line("PARENT pid ", "", (unsigned long)self);

    // A bad status pointer is refused before the child is reaped.
    int child = samos_system_run("ktest.elf exit 42");
    parent_expect("child pid differs from parent", child != self && child > 0, 1);
    parent_expect("waitpid into kernel memory", samos_waitpid(child, (int*)KERNEL_TEXT), S_EFAULT);
    int status = -1;
    parent_expect("waitpid after the refused one", samos_waitpid(child, &status), child);
    parent_expect("exit(42) status", status, 42);

    parent_expect("main returning 7", parent_run("ktest.elf exit 7", &pid), 7);
    parent_expect("divide by zero is 128 + 0", parent_run("ktest.elf de", &pid), 128);
    parent_expect("page fault is 128 + 14", parent_run("ktest.elf pf", &pid), 128 + 14);

    // kill a CPU-bound child; the parent runs again because of
    // preemption.
    pid = samos_system_run("ktest.elf spin K 1000");
    samos_sleep_ms(50);
    parent_expect("kill spinning child", samos_kill(pid), 0);
    status = -1;
    parent_expect("waitpid killed child", samos_waitpid(pid, &status), pid);
    parent_expect("killed status is 128 + 9", status, 137);
    parent_expect("kill a pid that is gone", samos_kill(pid), S_ESRCH);

    parent_expect("no child left to wait for", samos_wait(&status), S_ECHILD);

    print(parent_failures ? "PARENT FAIL\n" : "PARENT OK\n");
    return parent_failures != 0;
}

// Gap: "input goes to the focused process". `keys` opens a window,
// starts `keychild B` (whose window takes the focus), then both read
// keys. Keys typed while B's window is focused go to B only; when B
// exits its window closes, the focus returns to A's window, and the
// next keys go to A. Each prints the keys it got and, at 'q', the key
// presses its window-event ring delivered, in order.
static int keys_loop(const char* tag)
{
    char got[64];
    char line[96];
    int n = 0;
    while (1) {
        int c = samos_getkeyblock();
        snprintf(line, sizeof(line), "KEYS %s got %c\n", tag, c);
        print(line);
        if (c == 'q') {
            break;
        }
    }
    struct ktest_window_event ev;
    while (n < (int)sizeof(got) - 1 && samos_get_window_event(&ev) == 0) {
        if (ev.type == KEY_PRESS_EVENT) {
            got[n++] = (char)ev.data.x;
        }
    }
    got[n] = 0;
    snprintf(line, sizeof(line), "KEYS %s events %s\n", tag, got);
    print(line);
    return 0;
}

static int keys_test(const char* tag, int start_child)
{
    char title[16] = "keys ";
    strcpy(title + 5, tag);
    if (!samos_window_create(title, 120, 60, 0, -1)) {
        print("KEYS no window\n");
        return 1;
    }
    int child = 0;
    if (start_child) {
        child = samos_system_run("ktest.elf keychild B");
    }
    char line[64];
    snprintf(line, sizeof(line), "KEYS %s waiting\n", tag);
    print(line);
    keys_loop(tag);
    if (child > 0) {
        int status = -1;
        samos_waitpid(child, &status);
        print_line("KEYS child status ", "", (unsigned long)status);
    }
    return 0;
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
        say("KTEST kwrite %p\n", (void*)addr);
        *(volatile unsigned long*)addr = 0x4B4B4B4Bul;
    } else if (strncmp(mode, "kread", 6) == 0) {
        say("KTEST kread %p\n", (void*)addr);
        volatile unsigned long v = *(volatile unsigned long*)addr;
        (void)v;
    } else if (strncmp(mode, "kexec", 6) == 0) {
        say("KTEST kexec %p\n", (void*)addr);
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
    } else if (strncmp(mode, "spin", 5) == 0) {
        return spin_test(argc > 2 ? argv[2] : "?", argc > 3 ? parse_dec(argv[3]) : 5);
    } else if (strncmp(mode, "sleep", 6) == 0) {
        return sleep_test(argc > 2 ? parse_dec(argv[2]) : 500);
    } else if (strncmp(mode, "exit", 5) == 0) {
        return exit_test(argc > 2 ? parse_dec(argv[2]) : 0);
    } else if (strncmp(mode, "parent", 7) == 0) {
        return parent_test();
    } else if (strncmp(mode, "keys", 5) == 0) {
        return keys_test("A", 1);
    } else if (strncmp(mode, "keychild", 9) == 0) {
        return keys_test(argc > 2 ? argv[2] : "B", 0);
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
