// rvtest: the riscv64 user-mode probes (tests64/arch/
// riscv64-user.sh starts them with samos.run=RVTEST.ELF <mode> ...).
// ktest's x86 parts are inline assembly, so riscv64 has its own.
//
//   regs <seed>       x1-x31, f0-f31 and fcsr survive a syscall that
//                     returns at once, one that sleeps and switches
//                     tasks, and a loop that the timer preempts
//   spin <name> <n>   CPU-bound, never yields: n lines
//   fault <kind>      load, store, illegal or csr (reads sstatus, which
//                     only S-mode may): dies with a USER FAULT
//   exit <n>          samos_exit(n); ret <n>: main returns n
//   parent            starts children, checks waitpid, kill, -EFAULT
//                     for bad pointers, and the wrong-machine ELF

#include "samos.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "file.h"

#define SLOTS        65      // x0-x31, f0-f31, fcsr
#define SLOT_A0      10
#define SLOT_A1      11
#define SLOT_A7      17
#define SLOT_T6      31
#define SLOT_FCSR    64
#define ROUNDS       10
#define SPIN_LOOPS   20000000ul

#define CMD_SUM      0
#define CMD_UDELAY   25

// kernel status.h
#define S_EFAULT     (-20)
#define S_EBADEXEC   (-22)
#define S_ECHILD     (-23)
#define S_ESRCH      (-24)

// The kernel image: at or above user_end in both paging modes.
#define KERNEL_PTR   ((void*)0xffffffff80200000ul)

void rvtest_regs_ecall(unsigned long* in, unsigned long* out);
void rvtest_regs_spin(unsigned long* in, unsigned long* out);
unsigned long rvtest_load(unsigned long addr);
void rvtest_store(unsigned long addr);
void rvtest_illegal(void);
unsigned long rvtest_csr(void);

// One line, one syscall: another task's output cannot land inside it.
static void say(const char* fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    print(line);
}

static unsigned long number(const char* s, unsigned long fallback)
{
    if (!s || *s < '0' || *s > '9') {
        return fallback;
    }
    unsigned long v = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (unsigned long)(*s - '0');
    }
    return v;
}

static const char* slot_name(int i, char* buf)
{
    static const char* const x[32] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1",
        "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
    };
    if (i < 32) {
        return x[i];
    }
    if (i == SLOT_FCSR) {
        return "fcsr";
    }
    snprintf(buf, 8, "f%d", i - 32);
    return buf;
}

static int regs_check(unsigned long seed, int round, const char* what, unsigned long* in, unsigned long* out)
{
    for (int i = 1; i < SLOTS; i++) {
        if (in[i] != out[i]) {
            char buf[8];
            say("REGS FAIL %lu round %d after %s: %s expected %p got %p\n", seed, round, what,
                slot_name(i, buf), (void*)in[i], (void*)out[i]);
            return 1;
        }
    }
    return 0;
}

static void regs_fill(unsigned long* in, unsigned long seed, int round, int salt)
{
    for (int i = 0; i < SLOTS; i++) {
        in[i] = (seed << 56) ^ ((unsigned long)round << 40) ^ ((unsigned long)salt << 32) ^
                (0x0101010101ul * (unsigned long)(i + 1)) ^ 0x5A5A0000ul;
    }
    in[SLOT_FCSR] &= 0xff;   // frm and fflags: 8 bits
}

static int regs_test(unsigned long seed)
{
    unsigned long in[SLOTS], out[SLOTS];
    for (int round = 0; round < ROUNDS; round++) {
        // A syscall that returns through the trap frame: a0 = a0 + a1.
        regs_fill(in, seed, round, 1);
        in[SLOT_A7] = CMD_SUM;
        in[SLOT_A0] = round + 1;
        in[SLOT_A1] = round + 2;
        rvtest_regs_ecall(in, out);
        in[SLOT_A0] = 2 * round + 3;
        if (regs_check(seed, round, "sum", in, out)) {
            return 1;
        }

        // One that sleeps 1 ms, runs the other tasks and comes back
        // through task_enter: a0 = 0.
        regs_fill(in, seed, round, 2);
        in[SLOT_A7] = CMD_UDELAY;
        in[SLOT_A0] = 1000;
        rvtest_regs_ecall(in, out);
        in[SLOT_A0] = 0;
        if (regs_check(seed, round, "udelay", in, out)) {
            return 1;
        }

        // No syscall: the timer preempts the loop; t6 ends at 0.
        regs_fill(in, seed, round, 3);
        in[SLOT_T6] = SPIN_LOOPS;
        rvtest_regs_spin(in, out);
        in[SLOT_T6] = 0;
        if (regs_check(seed, round, "preemption", in, out)) {
            return 1;
        }
    }
    say("REGS OK %lu\n", seed);
    return 0;
}

// CPU-bound: the only way another task runs is the timer.
static int spin_test(const char* name, unsigned long lines)
{
    for (unsigned long i = 0; i < lines; i++) {
        for (volatile unsigned long n = 0; n < SPIN_LOOPS / 8; n++) {
        }
        say("SPIN %s %lu\n", name, i);
    }
    say("SPIN %s done\n", name);
    return 0;
}

static int fault_test(const char* kind)
{
    if (strcmp(kind, "load") == 0) {
        say("RVTEST fault load from 0x10 at %p\n", (void*)rvtest_load);
        rvtest_load(0x10);
    } else if (strcmp(kind, "store") == 0) {
        say("RVTEST fault store to %p at %p\n", (void*)rvtest_store, (void*)rvtest_store);
        rvtest_store((unsigned long)rvtest_store);
    } else if (strcmp(kind, "illegal") == 0) {
        say("RVTEST fault illegal at %p\n", (void*)rvtest_illegal);
        rvtest_illegal();
    } else if (strcmp(kind, "csr") == 0) {
        say("RVTEST fault csr at %p\n", (void*)rvtest_csr);
        rvtest_csr();
    }
    print("RVTEST survived the fault (bug)\n");
    return 1;
}

static int parent_failures;

static void expect(const char* what, long got, long want)
{
    say("PARENT %s %s %ld\n", what, got == want ? "ok" : "FAIL", got);
    parent_failures += got != want;
}

// Start `cmd`, wait for it, return its status (or the error).
static int run(const char* cmd)
{
    int pid = samos_system_run(cmd);
    if (pid < 0) {
        return pid;
    }
    int status = -1;
    int r = samos_waitpid(pid, &status);
    return r == pid ? status : r;
}

static int parent_test(void)
{
    int self = samos_getpid();
    say("PARENT pid %d\n", self);

    // A bad status pointer is refused before the child is reaped.
    int child = samos_system_run("rvtest.elf exit 42");
    expect("child pid differs from parent", child != self && child > 0, 1);
    expect("waitpid into the kernel", samos_waitpid(child, (int*)KERNEL_PTR), S_EFAULT);
    expect("waitpid into an unmapped page", samos_waitpid(child, (int*)0x1000), S_EFAULT);
    int status = -1;
    expect("waitpid after the refused ones", samos_waitpid(child, &status), child);
    expect("exit(42) status", status, 42);

    expect("main returning 7", run("rvtest.elf ret 7"), 7);
    expect("illegal instruction is 128 + 6", run("rvtest.elf fault illegal"), 128 + 6);
    expect("load page fault is 128 + 14", run("rvtest.elf fault load"), 128 + 14);
    expect("store page fault is 128 + 14", run("rvtest.elf fault store"), 128 + 14);

    // Other syscalls with a pointer they may not use.
    expect("fopen of a kernel path", samos_fopen((const char*)KERNEL_PTR, "r") < 0, 1);
    struct file_stat st;
    int fd = samos_fopen("@:/hello.elf", "r");
    expect("fstat into the kernel", samos_fstat(fd, (struct file_stat*)KERNEL_PTR) < 0, 1);
    expect("fread into the kernel", samos_fread(KERNEL_PTR, 1, 16, fd) < 0, 1);
    expect("fstat still works", samos_fstat(fd, &st), 0);
    samos_fclose(fd);

    // The loader takes only EM_RISCV: wrongm.elf is this program with
    // e_machine 62 (EM_X86_64), and nothing else changed.
    expect("wrong-machine ELF refused, -EBADEXEC", samos_system_run("wrongm.elf"), S_EBADEXEC);

    // Kill a CPU-bound child; the parent runs again by preemption.
    int pid = samos_system_run("rvtest.elf spin K 1000");
    samos_sleep_ms(50);
    expect("kill spinning child", samos_kill(pid), 0);
    status = -1;
    expect("waitpid killed child", samos_waitpid(pid, &status), pid);
    expect("killed status is 128 + 9", status, 137);
    expect("kill a pid that is gone", samos_kill(pid), S_ESRCH);
    expect("no child left to wait for", samos_wait(&status), S_ECHILD);

    print(parent_failures ? "PARENT FAIL\n" : "PARENT OK\n");
    return parent_failures != 0;
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "";
    if (strcmp(mode, "regs") == 0) {
        return regs_test(number(argc > 2 ? argv[2] : 0, 1));
    } else if (strcmp(mode, "spin") == 0) {
        return spin_test(argc > 2 ? argv[2] : "S", number(argc > 3 ? argv[3] : 0, 20));
    } else if (strcmp(mode, "fault") == 0) {
        return fault_test(argc > 2 ? argv[2] : "");
    } else if (strcmp(mode, "exit") == 0) {
        samos_exit((int)number(argc > 2 ? argv[2] : 0, 0));
    } else if (strcmp(mode, "ret") == 0) {
        return (int)number(argc > 2 ? argv[2] : 0, 0);
    } else if (strcmp(mode, "parent") == 0) {
        return parent_test();
    }
    say("RVTEST hello from U-mode, pid %d\n", samos_getpid());
    return 0;
}
