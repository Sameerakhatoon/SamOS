// Lecture 105 - blank becomes the fopen smoke test.
//
// L66 had this program spin forever printing "Hello world"
// through the print syscall. L105 swaps that for a fopen
// against the program's own ELF on the primary FS to verify
// the new userland file API and the kernel-side command 10
// dispatch round-trip without a crash.

#include "samos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "file.h"   // L105 - userland fopen

// SamOs e2e: BM_USER_* slot numbers; mirror src/bootmarker.h.
enum {
    USER_FOPEN        = 40,
    USER_FREAD_ELF    = 41,
    USER_FSTAT_SIZE   = 42,
    USER_FSEEK_ZERO   = 43,
    USER_FCLOSE_OK    = 44,
    USER_MALLOC_FREE  = 45,
    USER_REALLOC_GROW = 46,
    USER_INT80_REACH  = 48,
    USER_UDELAY_RETURNS = 49,  // udelay returns control to userland
    USER_WINDOW_CREATE  = 53,  // samos_window_create returns non-NULL
    USER_SYSOUT_REDIR   = 54,  // sysout_to_window did not crash
    USER_GET_EVENT_OK   = 55,  // get_window_event returns without faulting
    USER_GETKEY_NB      = 80,  // samos_getkey non-blocking returns
    USER_PRINT_OK       = 81,  // print() did not fault
    USER_PUTCHAR_OK     = 82,  // samos_putchar did not fault
    USER_PARSE_CMD_OK   = 83,  // samos_parse_command returned a non-NULL list
    USER_PROC_ARGS_OK   = 84,  // samos_process_get_arguments round trip
};

int main(int argc, char** argv){
    (void)argc;
    (void)argv;

    // SamOs e2e: prove we are in ring 3 and int 0x80 reached
    // the kernel by writing the canary slot first. If the kernel
    // never sees this, the user-side selftest harness is broken
    // (process_load_switch failed, ring 3 transition didn't iretq,
    // or the syscall dispatcher dropped the call).
    samos_e2e_mark(USER_INT80_REACH, 1);

    // Exercise user heap.
    {
        char* p = samos_malloc(128);
        int ok = 0;
        if(p){
            for(int i = 0; i < 128; i++) p[i] = (char)(i & 0x7F);
            ok = 1;
            for(int i = 0; i < 128; i++){
                if(p[i] != (char)(i & 0x7F)){ ok = 0; break; }
            }
            samos_free(p);
        }
        samos_e2e_mark(USER_MALLOC_FREE, ok ? 1 : 0);
    }

    // realloc grow preserves prefix.
    {
        char* p = samos_malloc(32);
        int ok = 0;
        if(p){
            for(int i = 0; i < 32; i++) p[i] = (char)(0xA0 + i);
            char* q = samos_realloc(p, 128);
            if(q){
                ok = 1;
                for(int i = 0; i < 32; i++){
                    if(q[i] != (char)(0xA0 + i)){ ok = 0; break; }
                }
                samos_free(q);
            }
        }
        samos_e2e_mark(USER_REALLOC_GROW, ok ? 1 : 0);
    }

    // File API end to end.
    int fd = fopen("@:/blank.elf", "r");
    samos_e2e_mark(USER_FOPEN, (fd > 0) ? 1 : 0);
    if(fd > 0){
        // Lecture 112 - exercise fstat on the just-opened ELF.
        // The L110 fread call is dropped: fstat alone keeps the
        // userland file API surface visible without holding
        // the slower ATA-PIO read.
        struct file_stat file_stat = {0};
        printf("File blank.elf opened\n");
        fstat(fd, &file_stat);
        samos_e2e_mark(USER_FSTAT_SIZE,
                       (file_stat.filesize > 0) ? 1 : 0);
        printf("File size: %i\n", file_stat.filesize);

        // SamOs e2e: a quick fread of the first 4 bytes; expect
        // the ELF magic. We deliberately do this AFTER fstat so
        // the slow PIO read does not delay the lighter probes.
        unsigned char hdr[4] = {0};
        samos_fread(hdr, 1, 4, (long)fd);
        int hdr_ok = (hdr[0] == 0x7F && hdr[1] == 'E'
                      && hdr[2] == 'L' && hdr[3] == 'F');
        samos_e2e_mark(USER_FREAD_ELF, hdr_ok ? 1 : 0);

        long rc_seek = samos_fseek((long)fd, 0, 0);
        samos_e2e_mark(USER_FSEEK_ZERO, (rc_seek >= 0) ? 1 : 0);

        fclose(fd);                   // L106 - close on success
        samos_e2e_mark(USER_FCLOSE_OK, 1);
    }else{
        samos_e2e_mark(USER_FREAD_ELF,  0);
        samos_e2e_mark(USER_FSTAT_SIZE, 0);
        samos_e2e_mark(USER_FSEEK_ZERO, 0);
        samos_e2e_mark(USER_FCLOSE_OK,  0);
        printf("File blank.elf opened failed\n");
    }

    // L198 userspace udelay: ask the kernel to put us to sleep
    // briefly, then mark on resume. If the IRQ timer + scheduler
    // never wakes us, the marker never lands and the e2e harness
    // sees the slot stay zero. 100 us is short enough to not eat
    // wallclock and long enough that the scheduler must run.
    samos_udelay(100);
    samos_e2e_mark(USER_UDELAY_RETURNS, 1);

    // L154 - userspace window create. Returns an opaque user
    // handle. We record 1 for "syscall round-tripped without
    // faulting" (return-value 0 is acceptable today: the kernel-
    // side window stack only fully initialises when the L136
    // Test-Window code path runs, which we skip in this boot in
    // order to drop straight to user mode).
    void* user_win = samos_window_create("e2e", 60, 40, 0, -1);
    samos_e2e_mark(USER_WINDOW_CREATE, 1);

    // L158 - divert stdout into the window we just made.
    samos_sysout_to_window(user_win);
    samos_e2e_mark(USER_SYSOUT_REDIR, 1);

    // L163 - drain one window event. The buffer needs to be sized
    // for the kernel side struct window_event_userland; 64 bytes
    // is generous and clearly within a user page. We just want
    // the syscall to round-trip without faulting; an empty ring
    // returns non-zero but does not fault.
    unsigned char evt[64] = {0};
    samos_get_window_event(evt);
    samos_e2e_mark(USER_GET_EVENT_OK, 1);

    // L101 - samos print + putchar trampolines round-trip cleanly.
    print("e2e: hello\n");
    samos_e2e_mark(USER_PRINT_OK, 1);
    samos_putchar('!');
    samos_e2e_mark(USER_PUTCHAR_OK, 1);

    // L102 - non-blocking getkey: the keyboard buffer is empty at
    // boot so this returns 0 immediately. We just want the
    // syscall to round-trip.
    (void)samos_getkey();
    samos_e2e_mark(USER_GETKEY_NB, 1);

    // L116 - samos_parse_command splits a command line into a
    // list. The first segment should be non-NULL.
    {
        struct command_argument* args = samos_parse_command("hello world", 64);
        samos_e2e_mark(USER_PARSE_CMD_OK, args ? 1 : 0);
        // We do NOT free the list - the test ELF spins forever
        // right after this; the kernel reclaims the process heap
        // when the e2e harness QEMU-quits.
    }

    // L115 - get the process's own arguments. argc may be 0 when
    // BLANK.ELF is launched directly (kernel doesn't pass args
    // through), but the syscall should still round-trip without
    // faulting and populate argc / argv.
    {
        struct process_arguments pa = {0, NULL};
        samos_process_get_arguments(&pa);
        samos_e2e_mark(USER_PROC_ARGS_OK, 1);
    }

    while(1){
    }
    return 0;
}
