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
//
// Syscalls 40-46:
//   ktest files       errors, bad pointers, page-crossing buffers,
//                     fclose, descriptors closed on exit ("FILES OK")
//   ktest opendirs N  open the root N times and exit without closing
//
// SMP probes, syscalls 51-53:
//   ktest cpubusy N MS     N children burn the CPU for MS ms; per-CPU
//                          busy ticks over the middle of that time
//                          ("CPUBUSY cpu I ... pct P", "CPUBUSY OK")
//   ktest burn MS          spin for MS ms; exit status = mask of the
//                          CPUs it ran on
//   ktest stress R C N F H...  R rounds (after a warm-up round) of C
//                          process create/exit cycles in 4 spawners
//                          and N readers hashing files F (FNV-1a H,
//                          hex), heap and frames unchanged after each
//                          round ("STRESS round r ok", "STRESS OK")
//   ktest quiet CODE       return CODE, print nothing
//   ktest spawner K        K x (start "quiet", waitpid, check status)
//   ktest reader N F H...  N passes over the files, hash each
//   ktest readers N P F H...  N readers at once, P passes each

#include "samos.h"
#include "string.h"
#include "memory.h"
#include "stdlib.h"
#include "stdio.h"
#include "file.h"

#if defined(__aarch64__)
// x0-x30, the x0 the svc gets, then q0-q31 as 2 qwords each
// (arch/aarch64/regs.S). x8 is the syscall number.
#define REGS_GPRS    32
#define REGS_VECS    32
#define SLOT_ARG0    31          // x0 at the svc; slot 0 is x0 after it
#define SLOT_ARG1    1
#define SLOT_NR      8
#define VEC_NAME     "q"
#define REGS_EXTRA   2           // then FPCR and FPSR
#define SLOT_FPCR    (REGS_GPRS + 2 * REGS_VECS)
#define FPCR_BITS    0x07c00000ul    // AHP, DN, FZ, RMode
#define FPSR_BITS    0x0800009ful    // QC, IDC, IXC..IOC
#else
#define REGS_GPRS    15          // rax..r15
#define REGS_VECS    16          // xmm0-15
#define SLOT_ARG0    5           // rdi
#define SLOT_ARG1    4           // rsi
#define VEC_NAME     "xmm"
#define REGS_EXTRA   0
#endif
#define REGS_SLOTS   (REGS_GPRS + 2 * REGS_VECS + REGS_EXTRA)
#define REGS_ROUNDS  20

#define CMD_SUM      0
#define CMD_UDELAY   25

// regs.asm / arch/aarch64/regs.S
extern void ktest_regs_roundtrip(unsigned long* in, unsigned long* out,
                                 unsigned long cmd);

static const char* slot_name(int i)
{
#if defined(__aarch64__)
    static char name[8];
    if (i >= SLOT_FPCR) {
        return i == SLOT_FPCR ? "fpcr" : "fpsr";
    }
    if (i >= REGS_GPRS) {
        return VEC_NAME;
    }
    snprintf(name, sizeof(name), i == SLOT_ARG0 ? "x0 in" : "x%d", i);
    return name;
#else
    const char* names[15] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8",
        "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    };
    return i < 15 ? names[i] : VEC_NAME;
#endif
}

// The registers a syscall gets: number, first and second argument;
// `ret` is the result expected in the return register.
static void regs_set_call(unsigned long* in, unsigned long cmd, unsigned long a0,
                          unsigned long a1, unsigned long ret)
{
    in[SLOT_ARG0] = a0;
    in[SLOT_ARG1] = a1;
    in[0] = ret;
#if defined(__aarch64__)
    in[SLOT_NR] = cmd;
#endif
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
            if (i >= REGS_GPRS && i < REGS_GPRS + 2 * REGS_VECS) {
                snprintf(xmm, sizeof(xmm), "%d%s", (i - REGS_GPRS) / 2, (i - REGS_GPRS) % 2 ? ".hi" : ".lo");
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
#if defined(__aarch64__)
        in[SLOT_FPCR] &= FPCR_BITS;          // only bits that read back
        in[SLOT_FPCR + 1] &= FPSR_BITS;
#endif

        // A syscall that returns straight through the trap frame.
        // Command 0 adds its two arguments (rdi, rsi): rax = 2 * arg.
        unsigned long arg = round + 1;
        regs_set_call(in, CMD_SUM, arg, arg, 2 * arg);
        ktest_regs_roundtrip(in, out, CMD_SUM);
        if (regs_check(seed, round, "sum", in, out)) {
            return 1;
        }

        // A syscall that sleeps, runs the other tasks and comes back
        // through task_enter: rax = 0. rdi = 1000 microseconds.
        regs_set_call(in, CMD_UDELAY, 1000, in[SLOT_ARG1], 0);
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
#if defined(__aarch64__)
    // No divide trap here; a fault's status uses x86's numbers.
    parent_expect("undefined instruction is 128 + 6", parent_run("ktest.elf ud", &pid), 128 + 6);
#else
    parent_expect("divide by zero is 128 + 0", parent_run("ktest.elf de", &pid), 128);
#endif
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

// ---------------------------------------------------------------
// Syscalls 40-46 and the fopen/fclose changes.
// ---------------------------------------------------------------

#define CMD_FCLOSE    11
#define CMD_FWRITE    40
#define CMD_UNLINK    42
#define CMD_MKDIR     43
#define CMD_OPENDIR   45
#define CMD_READDIR   46

#define S_EBADPATH    (-4)
#define S_ERDONLY     (-6)
#define S_ENOENT      (-14)
#define S_EEXIST      (-16)
#define S_ENOTDIR     (-17)
#define S_EISDIR      (-18)
#define S_ENOTEMPTY   (-19)

static int files_failures;

static void files_expect(const char* what, long got, long want)
{
    if (got == want) {
        say("FILES %s ok\n", what);
        return;
    }
    say("FILES %s FAIL got %ld want %ld\n", what, got, want);
    files_failures++;
}

static long file_size(const char* path)
{
    struct file_stat st = {0};
    int fd = fopen(path, "r");
    if (fd < 0) {
        return fd;
    }
    int res = fstat(fd, &st);
    fclose(fd);
    return res < 0 ? res : (long)st.filesize;
}

// Open the root directory n times and exit with them all open: the
// kernel must close them on exit (two runs of 300 need 600 of the
// 512 kernel descriptors otherwise).
static int opendirs_test(unsigned long n)
{
    for (unsigned long i = 0; i < n; i++) {
        int fd = opendir("@:/");
        if (fd <= 0) {
            say("OPENDIRS FAIL at %lu: %d\n", i, fd);
            return 1;
        }
    }
    say("OPENDIRS %lu open\n", n);
    return 0;
}

static int files_test(void)
{
    long k = (long)KERNEL_TEXT;
    struct file_dirent ent;

    // fopen says why it failed (it returned -EIO or 0 for everything).
    files_expect("fopen missing file", fopen("@:/NOSUCH.TXT", "r"), S_ENOENT);
    files_expect("mkdir", mkdir("@:/KTDIR"), 0);
    files_expect("mkdir existing", mkdir("@:/KTDIR"), S_EEXIST);
    files_expect("mkdir missing parent", mkdir("@:/NODIR/SUB"), S_EBADPATH);
    files_expect("mkdir under a file", mkdir("@:/HELLO.TXT/SUB"), S_ENOTDIR);
    files_expect("fopen directory for writing", fopen("@:/KTDIR", "w"), S_EISDIR);
    files_expect("mkdir kernel path", ktest_syscall(CMD_MKDIR, k, 0, 0, 0), S_EFAULT);
    files_expect("unlink kernel path", ktest_syscall(CMD_UNLINK, k, 0, 0, 0), S_EFAULT);
    files_expect("opendir kernel path", ktest_syscall(CMD_OPENDIR, k, 0, 0, 0), S_EFAULT);

    // A write buffer that crosses a page boundary: 300 bytes from 96
    // bytes before the end of the first page of a two-page block.
    char* block = malloc(8192);
    files_expect("malloc two pages", block != 0, 1);
    if (!block) {
        return 1;
    }
    for (int i = 0; i < 8192; i++) {
        block[i] = (char)('A' + i % 26);
    }
    char* cross = block + 4096 - 96;
    int fd = fopen("@:/KTDIR/CROSS.TXT", "w");
    files_expect("fopen new file", fd > 0, 1);
    files_expect("fwrite across pages", fwrite(cross, 300, 1, fd), 1);
    files_expect("fwrite kernel buffer", ktest_syscall(CMD_FWRITE, k, 1, 16, fd), S_EFAULT);
    files_expect("fwrite past the block", ktest_syscall(CMD_FWRITE, (long)(block + 8192 - 50), 1, 100, fd),
                 S_EFAULT);
    files_expect("fwrite fd not owned", ktest_syscall(CMD_FWRITE, (long)block, 1, 1, 12345), S_EINVARG);
    files_expect("fclose", fclose(fd), 0);
    files_expect("fclose again", fclose(fd), S_EINVARG);
    files_expect("size after the refused writes", file_size("@:/KTDIR/CROSS.TXT"), 300);

    // Read it back into a buffer that crosses the page the other way.
    fd = fopen("@:/KTDIR/CROSS.TXT", "r");
    memset(block, 0, 8192);
    files_expect("fread back across pages", fread(block + 4096 - 150, 300, 1, fd), 1);
    int same = 1;
    for (int i = 0; i < 300; i++) {
        if (block[4096 - 150 + i] != (char)('A' + (4096 - 96 + i) % 26)) {
            same = 0;
        }
    }
    files_expect("read back equals written", same, 1);
    files_expect("fwrite to a read-only fd", fwrite(block, 1, 1, fd), S_ERDONLY);
    fclose(fd);

    fd = fopen("@:/KTDIR/CROSS.TXT", "a");
    files_expect("ftruncate", ftruncate(fd, 100), 0);
    files_expect("append", fwrite("xyz", 3, 1, fd), 1);
    fclose(fd);
    files_expect("size after truncate and append", file_size("@:/KTDIR/CROSS.TXT"), 103);

    files_expect("opendir a file", opendir("@:/HELLO.TXT"), S_ENOTDIR);
    files_expect("opendir missing", opendir("@:/NODIR"), S_ENOENT);
    int dir = opendir("@:/KTDIR");
    files_expect("opendir", dir > 0, 1);
    files_expect("readdir into kernel", ktest_syscall(CMD_READDIR, dir, k, 0, 0), S_EFAULT);
    // The 264-byte entry crosses into the second page.
    struct file_dirent* e = (struct file_dirent*)(block + 4096 - 100);
    files_expect("readdir across pages", readdir(dir, e), 1);
    files_expect("readdir name", strcmp(e->name, "CROSS.TXT") == 0, 1);
    files_expect("readdir size", e->size, 103);
    files_expect("readdir end", readdir(dir, &ent), 0);
    fd = fopen("@:/HELLO.TXT", "r");
    files_expect("readdir on a file fd", readdir(fd, &ent), S_EINVARG);
    files_expect("fread on a directory fd", fread(block, 1, 1, dir), S_EINVARG);
    fclose(fd);
    files_expect("closedir", closedir(dir), 0);

    files_expect("rmdir non-empty", rmdir("@:/KTDIR"), S_ENOTEMPTY);
    files_expect("unlink a directory", unlink("@:/KTDIR"), S_EISDIR);
    files_expect("rmdir a file", rmdir("@:/KTDIR/CROSS.TXT"), S_ENOTDIR);
    files_expect("unlink", unlink("@:/KTDIR/CROSS.TXT"), 0);
    files_expect("unlink again", unlink("@:/KTDIR/CROSS.TXT"), S_ENOENT);
    files_expect("rmdir", rmdir("@:/KTDIR"), 0);
    files_expect("directory gone", opendir("@:/KTDIR"), S_ENOENT);

    // fclose really closes: 600 opens through 512 kernel descriptors.
    int opened = 0;
    for (int i = 0; i < 600; i++) {
        fd = fopen("@:/HELLO.TXT", "r");
        if (fd <= 0) {
            break;
        }
        opened++;
        fclose(fd);
    }
    files_expect("600 open and close", opened, 600);

    // Directory descriptors left open are closed on exit.
    int pid = 0;
    for (int run = 0; run < 2; run++) {
        int status = -1;
        pid = samos_system_run("ktest.elf opendirs 300");
        files_expect("child started", pid > 0, 1);
        if (pid > 0) {
            samos_waitpid(pid, &status);
        }
        files_expect("child opened 300 directories", status, 0);
    }

    free(block);
    if (files_failures) {
        say("FILES FAIL %d\n", files_failures);
        return 1;
    }
    print("FILES OK\n");
    return 0;
}

// ---------------------------------------------------------------
// SMP probes.
// ---------------------------------------------------------------

#define CMD_CPU_STATS    51
#define CMD_MEM_STATS    52
#define CMD_CPU_CURRENT  53
#define SMP_MAX_CPUS     16

// kernel isr80h/misc.c struct cpu_stat
struct cpu_stat {
    unsigned long index, apic_id, ticks_busy, ticks_idle;
    unsigned long tasks_entered, steals, ipi_resched, ipi_tlb;
};

// kernel memory/heap/kheap.h struct kheap_stats
struct mem_stat {
    unsigned long frames_total, frames_free, frames_used, frames_reserved;
    unsigned long slab_chunks, slab_pages_in_use, slab_objects_in_use, bad_frees;
};

static int cpu_stats(struct cpu_stat* out)
{
    return (int)ktest_syscall(CMD_CPU_STATS, (long)out, SMP_MAX_CPUS, 0, 0);
}

static int burn_test(unsigned long ms)
{
    unsigned long end = samos_uptime() + ms;
    int mask = 0;
    while (samos_uptime() < end) {
        for (volatile unsigned long n = 0; n < 200000; n++) {
        }
        mask |= 1 << (ktest_syscall(CMD_CPU_CURRENT, 0, 0, 0, 0) & 7);
    }
    return mask;
}

// N CPU-bound children for ms; the parent sleeps and compares each
// CPU's busy and idle ticks across the middle half of that time.
static int cpubusy_test(unsigned long n, unsigned long ms)
{
    static struct cpu_stat a[SMP_MAX_CPUS], b[SMP_MAX_CPUS];
    int pids[32];
    char cmd[64];
    if (n > 32) {
        n = 32;
    }
    snprintf(cmd, sizeof(cmd), "ktest.elf burn %lu", ms);
    for (unsigned long i = 0; i < n; i++) {
        pids[i] = samos_system_run(cmd);
    }
    samos_sleep_ms(ms / 4);
    int cpus = cpu_stats(a);
    samos_sleep_ms(ms / 2);
    cpu_stats(b);
    int failures = 0;
    for (int i = 0; i < cpus && i < SMP_MAX_CPUS; i++) {
        unsigned long busy = b[i].ticks_busy - a[i].ticks_busy;
        unsigned long idle = b[i].ticks_idle - a[i].ticks_idle;
        unsigned long pct = busy + idle ? busy * 100 / (busy + idle) : 0;
        say("CPUBUSY cpu %lu busy %lu idle %lu pct %lu steals %lu entered %lu\n", b[i].index,
            busy, idle, pct, b[i].steals, b[i].tasks_entered);
        if (busy + idle == 0 || pct < 90) {
            failures++;
        }
    }
    int all = 0;
    for (unsigned long i = 0; i < n; i++) {
        int status = -1;
        if (pids[i] <= 0 || samos_waitpid(pids[i], &status) != pids[i] || status < 0) {
            say("CPUBUSY child %lu FAIL\n", i);
            failures++;
            continue;
        }
        all |= status;
        say("CPUBUSY child %lu ran on cpus 0x%x\n", i, status);
    }
    say("CPUBUSY %d cpus, children together ran on 0x%x\n", cpus, all);
    print(failures ? "CPUBUSY FAIL\n" : "CPUBUSY OK\n");
    return failures != 0;
}

// K create/exit cycles, each child returning a different status.
static int spawner_test(unsigned long k)
{
    char cmd[48];
    for (unsigned long i = 0; i < k; i++) {
        int code = (int)(i % 200);
        snprintf(cmd, sizeof(cmd), "ktest.elf quiet %d", code);
        int pid = samos_system_run(cmd);
        int status = -1;
        if (pid <= 0 || samos_waitpid(pid, &status) != pid || status != code) {
            say("STRESS spawner cycle %lu FAIL pid %d status %d\n", i, pid, status);
            return 1;
        }
    }
    return 0;
}

static unsigned int fnv_file(const char* path, long* size_out)
{
    static char buf[4096];
    unsigned int h = 2166136261u;
    long size = 0;
    char full[120] = "@:/";
    if (!strchr(path, ':')) {
        strncpy(full + 3, path, sizeof(full) - 4);
        path = full;
    }
    int fd = fopen(path, "r");
    if (fd <= 0) {
        *size_out = -1;
        return 0;
    }
    while (1) {
        long got = fread(buf, 1, sizeof(buf), fd);
        if (got <= 0) {
            break;
        }
        for (long i = 0; i < got; i++) {
            h = (h ^ (unsigned char)buf[i]) * 16777619u;
        }
        size += got;
    }
    fclose(fd);
    *size_out = size;
    return h;
}

// N passes over each (file, expected hash) pair.
static int reader_test(unsigned long n, int argc, char** argv)
{
    int bad = 0;
    for (unsigned long pass = 0; pass < n; pass++) {
        for (int i = 3; i + 1 < argc; i += 2) {
            long size = 0;
            unsigned int h = fnv_file(argv[i], &size);
            if (size <= 0 || h != (unsigned int)parse_hex(argv[i + 1])) {
                say("STRESS reader %s pass %lu FAIL hash %x size %ld\n", argv[i], pass, h, size);
                bad++;
            }
        }
    }
    return bad ? 1 : 0;
}

static int stress_round(unsigned long cycles, unsigned long readers, unsigned long passes,
                        const char* files)
{
    int pids[16];
    int n = 0;
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "ktest.elf spawner %lu", cycles / 4);
    for (int i = 0; i < 4; i++) {
        pids[n++] = samos_system_run(cmd);
    }
    snprintf(cmd, sizeof(cmd), "ktest.elf reader %lu%s", passes, files);
    for (unsigned long i = 0; i < readers && n < 16; i++) {
        pids[n++] = samos_system_run(cmd);
    }
    int failures = 0;
    for (int i = 0; i < n; i++) {
        int status = -1;
        if (pids[i] <= 0 || samos_waitpid(pids[i], &status) != pids[i] || status != 0) {
            say("STRESS child %d pid %d FAIL status %d\n", i, pids[i], status);
            failures++;
        }
    }
    return failures;
}

static int stress_test(int argc, char** argv)
{
    unsigned long rounds  = argc > 2 ? parse_dec(argv[2]) : 10;
    unsigned long cycles  = argc > 3 ? parse_dec(argv[3]) : 1000;
    unsigned long readers = argc > 4 ? parse_dec(argv[4]) : 3;
    char files[300] = "";
    for (int i = 5; i < argc; i++) {
        size_t len = strlen(files);
        snprintf(files + len, sizeof(files) - len, " %s", argv[i]);
    }
    unsigned long passes = 20;
    static struct mem_stat base, now;
    static struct cpu_stat cs[SMP_MAX_CPUS];
    unsigned long start = samos_uptime();

    // Warm-up: the sector cache fills and the process table grows to
    // this test's size once; the baseline is taken after it.
    int failures = stress_round(cycles / 10, readers, 1, files);
    ktest_syscall(CMD_MEM_STATS, (long)&base, 0, 0, 0);
    say("STRESS warm-up done, frames used %lu slab objects %lu\n", base.frames_used,
        base.slab_objects_in_use);
    for (unsigned long r = 1; r <= rounds && !failures; r++) {
        unsigned long t0 = samos_uptime();
        failures += stress_round(cycles, readers, passes, files);
        ktest_syscall(CMD_MEM_STATS, (long)&now, 0, 0, 0);
        long frames = (long)now.frames_used - (long)base.frames_used;
        long slab   = (long)now.slab_objects_in_use - (long)base.slab_objects_in_use;
        long bad    = (long)now.bad_frees - (long)base.bad_frees;
        int ok = !failures && frames == 0 && slab == 0 && bad == 0;
        say("STRESS round %lu %s cycles %lu reads %lu ms %lu frames %ld slab %ld bad frees %ld\n",
            r, ok ? "ok" : "FAIL", cycles, readers * passes, samos_uptime() - t0, frames, slab, bad);
        if (!ok) {
            failures++;
        }
    }
    int cpus = cpu_stats(cs);
    for (int i = 0; i < cpus && i < SMP_MAX_CPUS; i++) {
        say("STRESS cpu %lu entered %lu steals %lu resched ipis %lu tlb ipis %lu\n", cs[i].index,
            cs[i].tasks_entered, cs[i].steals, cs[i].ipi_resched, cs[i].ipi_tlb);
    }
    say(failures ? "STRESS FAIL after %lu ms\n" : "STRESS OK %lu rounds in %lu ms\n",
        failures ? samos_uptime() - start : rounds, samos_uptime() - start);
    return failures != 0;
}

// (e2e/194) - N readers at once, P passes each over the files,
// for disk traffic from every CPU ("READERS OK" or "READERS FAIL").
static int readers_test(int argc, char** argv)
{
    unsigned long n      = argc > 2 ? parse_dec(argv[2]) : 4;
    unsigned long passes = argc > 3 ? parse_dec(argv[3]) : 1;
    char cmd[400];
    int len = snprintf(cmd, sizeof(cmd), "ktest.elf reader %lu", passes);
    for (int i = 4; i < argc && len < (int)sizeof(cmd); i++) {
        len += snprintf(cmd + len, sizeof(cmd) - len, " %s", argv[i]);
    }
    unsigned long t0 = samos_uptime();
    int pids[16];
    for (unsigned long i = 0; i < n && i < 16; i++) {
        pids[i] = samos_system_run(cmd);
    }
    int failures = 0;
    for (unsigned long i = 0; i < n && i < 16; i++) {
        int status = -1;
        if (pids[i] <= 0 || samos_waitpid(pids[i], &status) != pids[i] || status != 0) {
            say("READERS child %lu pid %d FAIL status %d\n", i, pids[i], status);
            failures++;
        }
    }
    say("READERS %lu readers %lu passes in %lu ms\n", n, passes, samos_uptime() - t0);
    say(failures ? "READERS FAIL\n" : "READERS OK\n");
    return failures != 0;
}

#if defined(__aarch64__)
#define RET_BYTES 0xC0, 0x03, 0x5F, 0xD6           // ret (0xd65f03c0)
#else
#define RET_BYTES 0xC3                             // ret
#endif
static unsigned char data_code[16] = { RET_BYTES };   // in .data

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
        unsigned char code[16] = { RET_BYTES };
        print("KTEST nxstack\n");
        ((void (*)(void))code)();
    } else if (strncmp(mode, "nxheap", 7) == 0) {
        unsigned char* code = malloc(16);
        memcpy(code, data_code, 4);
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
        // aarch64 has no divide trap: the quotient is 0 and this
        // returns.
        print("KTEST de: dividing by zero\n");
        volatile int zero = 0;
        volatile int x = 10 / zero;
        (void)x;
    } else if (strncmp(mode, "gp", 3) == 0) {
#if defined(__aarch64__)
        print("KTEST gp: masking IRQs at EL0\n");
        __asm__ volatile("msr daifset, #2");
#else
        print("KTEST gp: executing hlt in ring 3\n");
        __asm__ volatile("hlt");
#endif
    } else if (strncmp(mode, "pf", 3) == 0) {
        print("KTEST pf: writing to an unmapped page\n");
        *(volatile unsigned long*)0x0000700000000000ul = 1;
    } else if (strncmp(mode, "ud", 3) == 0) {
#if defined(__aarch64__)
        print("KTEST ud: udf\n");
        __asm__ volatile("udf #0");
#else
        print("KTEST ud: ud2\n");
        __asm__ volatile("ud2");
#endif
    } else if (strncmp(mode, "kptr", 5) == 0) {
        return kptr_test();
    } else if (strncmp(mode, "longpath", 9) == 0) {
        return longpath_test();
    } else if (strncmp(mode, "badelf", 7) == 0) {
        return badelf_test(argc, argv);
    } else if (strncmp(mode, "bss", 4) == 0) {
        return bss_test();
    } else if (strncmp(mode, "files", 6) == 0) {
        return files_test();
    } else if (strncmp(mode, "opendirs", 9) == 0) {
        return opendirs_test(argc > 2 ? parse_dec(argv[2]) : 1);
    } else if (isolation_mode(mode, argc, argv)) {
        return 1;
    } else if (strncmp(mode, "cpubusy", 8) == 0) {
        return cpubusy_test(argc > 2 ? parse_dec(argv[2]) : 8, argc > 3 ? parse_dec(argv[3]) : 4000);
    } else if (strncmp(mode, "burn", 5) == 0) {
        return burn_test(argc > 2 ? parse_dec(argv[2]) : 1000);
    } else if (strncmp(mode, "stress", 7) == 0) {
        return stress_test(argc, argv);
    } else if (strncmp(mode, "quiet", 6) == 0) {
        return (int)(argc > 2 ? parse_dec(argv[2]) : 0);
    } else if (strncmp(mode, "spawner", 8) == 0) {
        return spawner_test(argc > 2 ? parse_dec(argv[2]) : 1);
    } else if (strncmp(mode, "readers", 8) == 0) {
        return readers_test(argc, argv);
    } else if (strncmp(mode, "reader", 7) == 0) {
        return reader_test(argc > 2 ? parse_dec(argv[2]) : 1, argc, argv);
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
