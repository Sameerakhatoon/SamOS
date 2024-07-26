#ifndef ISR80H_H
#define ISR80H_H

enum SystemCommands {
    SYSTEM_COMMAND0_SUM,
    SYSTEM_COMMAND1_PRINT,
    SYSTEM_COMMAND2_GETKEY,
    SYSTEM_COMMAND3_PUTCHAR,
    SYSTEM_COMMAND4_MALLOC,
    SYSTEM_COMMAND5_FREE,
    SYSTEM_COMMAND6_PROCESS_LOAD_START,
    SYSTEM_COMMAND7_INVOKE_SYSTEM_COMMAND,
    SYSTEM_COMMAND8_GET_PROGRAM_ARGUMENTS,
    SYSTEM_COMMAND9_EXIT,
    SYSTEM_COMMAND10_FOPEN,  // L105 - userland fopen
    SYSTEM_COMMAND11_FCLOSE, // L106 - userland fclose
    SYSTEM_COMMAND12_FREAD,  // L107 - userland fread
    SYSTEM_COMMAND13_FSEEK,  // L111 - userland fseek
    SYSTEM_COMMAND14_FSTAT,  // L112 - userland fstat
    SYSTEM_COMMAND15_REALLOC, // L115 - userland realloc
    SYSTEM_COMMAND16_WINDOW_CREATE, // L154 - userland window create
    SYSTEM_COMMAND17_SYSOUT_TO_WINDOW, // L158 - divert stdout to a window
    SYSTEM_COMMAND18_GET_WINDOW_EVENT, // L163 - drain a window event
    SYSTEM_COMMAND19_WINDOW_GRAPHICS_GET, // L165 - userspace graphics info
    SYSTEM_COMMAND20_GRAPHICS_PIXELS_BUFFER_GET, // L166 - userspace pixels mapping
    SYSTEM_COMMAND21_WINDOW_REDRAW, // L167 - userspace window redraw
    SYSTEM_COMMAND22_GRAPHICS_CREATE, // L171 - userspace relative graphics create
    SYSTEM_COMMAND23_WINDOW_REDRAW_REGION, // L172 - userspace body-region redraw
    SYSTEM_COMMAND24_UPDATE_WINDOW, // L173 - userspace update window field
    SYSTEM_COMMAND25_UDELAY, // L198 - userspace udelay / task sleep

    // SamOs e2e (Phase 2): userspace -> kernel marker write so
    // a user-side selftest ELF can report feature pass/fail back
    // into the boot-marker region at 0x6000. Arguments:
    //   rdi = slot  (uint32_t, must be >= 40, < BM_STAGE_MAX)
    //   rsi = value (uint32_t)
    // Returns 0 on success, -1 if slot is out of the user-write
    // window. The kernel deliberately restricts user writes to
    // slots 40..BM_STAGE_MAX-1 so a userspace bug cannot stomp
    // on kernel-side checkpoints (1..28).
    SYSTEM_COMMAND26_E2E_MARK,

    // Processes and time. Every syscall takes its
    // arguments in rdi, rsi, rdx, r10, r8, r9 (isr80h_arg).
    SYSTEM_COMMAND27_GETPID,       // int getpid(void)
    SYSTEM_COMMAND28_WAITPID,      // int waitpid(int pid, int* status)
    SYSTEM_COMMAND29_KILL,         // int kill(int pid)
    SYSTEM_COMMAND30_SLEEP_MS,     // void sleep_ms(unsigned long ms)
    SYSTEM_COMMAND31_UPTIME,       // unsigned long uptime(void), ms
    SYSTEM_COMMAND32_GETKEY_BLOCK, // int getkey_block(void)

    // Write side of the VFS and directories.
    SYSTEM_COMMAND40_FWRITE = 40,  // long fwrite(const void* buf, size_t size, size_t count, long fd)
    SYSTEM_COMMAND41_FTRUNCATE,    // int ftruncate(int fd, unsigned long size)
    SYSTEM_COMMAND42_UNLINK,       // int unlink(const char* path), files only
    SYSTEM_COMMAND43_MKDIR,        // int mkdir(const char* path)
    SYSTEM_COMMAND44_RMDIR,        // int rmdir(const char* path), empty directories only
    SYSTEM_COMMAND45_OPENDIR,      // int opendir(const char* path), close with fclose
    SYSTEM_COMMAND46_READDIR,      // int readdir(int fd, struct file_dirent* out)
    // Platform. 33-39 and 47 are unused: the file and
    // platform calls were numbered separately.
    SYSTEM_COMMAND48_TIME = 48,    // long time(void), unix seconds
    SYSTEM_COMMAND49_SHUTDOWN,     // void shutdown(void), does not return
    SYSTEM_COMMAND50_REBOOT,       // void reboot(void), does not return
    // What the CPUs and the heap are doing, for tests.
    SYSTEM_COMMAND51_CPU_STATS,    // long cpu_stats(struct cpu_stat* out, long max): CPUs online
    SYSTEM_COMMAND52_MEM_STATS,    // long mem_stats(struct kheap_stats* out)
    SYSTEM_COMMAND53_CPU_CURRENT,  // long cpu_current(void): index of the CPU running the caller
    // The network (net/net.h). 54-59 are left for other branches.
    SYSTEM_COMMAND60_NET_INFO = 60, // long net_info(int what, char* buf, long size)
    SYSTEM_COMMAND61_NET_PING,     // long net_ping(unsigned int ip, unsigned int timeout_ms)
};

void isr80h_register_commands();

// The syscall dispatch, moved here from the x86 trap code: every arch's
// syscall entry (x86: int 0x80, aarch64: svc, riscv64: ecall) calls
// isr80h_handler with the trap frame; the command number, arguments
// and result go through the arch's trap_frame accessors.
#include "arch/arch.h"

typedef void* (*ISR80H_COMMAND)(struct trap_frame* frame);

void  isr80h_register_command(int command_id, ISR80H_COMMAND command);
void  isr80h_handler(struct trap_frame* frame);

// Argument `n` (0-5) of the syscall in `frame`.
static inline void* isr80h_arg(struct trap_frame* frame, int n){
    return trap_frame_arg(frame, n);
}

#endif
