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
#include "errno.h"

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
    USER_KEY_RECEIVED   = 160, // samos_getkey eventually returns a non-zero key
    USER_LIBC_PRINTF    = 168, // Snprintf formats (bitmask of failures in the value)
    USER_LIBC_STRINGS   = 169, // number/string helpers, errno, calloc, quoting
};

// Libc checks, run inside SamOS so they use the real
// syscalls and the cross-compiled code. Each returns 1 on success.
// On failure the e2e value is 0x100 + the number of the first
// failing check, so the marker says which one broke.
static int fmt_is(const char* want, const char* got){
    return strcmp(want, got) == 0;
}

static unsigned int libc_printf_checks(void){
    char b[64];
    int n = 0;
    snprintf(b, sizeof b, "%d|%i|%u", -42, 7, 4294967295u);
    if(!fmt_is("-42|7|4294967295", b)) return 0x100 + 1;
    snprintf(b, sizeof b, "%x|%X|%p", 255, 255, (void*)0x1234);
    if(!fmt_is("ff|FF|0x1234", b)) return 0x100 + 2;
    snprintf(b, sizeof b, "%c%c|%s", 'o', 'k', "str");
    if(!fmt_is("ok|str", b)) return 0x100 + 3;
    snprintf(b, sizeof b, "%ld|%lu", -9000000000L, 18000000000UL);
    if(!fmt_is("-9000000000|18000000000", b)) return 0x100 + 4;
    snprintf(b, sizeof b, "[%5d][%-5d][%05d]", 42, 42, -42);
    if(!fmt_is("[   42][42   ][-0042]", b)) return 0x100 + 5;
    snprintf(b, sizeof b, "100%%|50%");
    if(!fmt_is("100%|50%", b)) return 0x100 + 6;
    n = snprintf(b, 4, "abcdef");
    if(n != 6 || !fmt_is("abc", b)) return 0x100 + 7;
    // printf goes out through print(); the e2e only checks it returns.
    if(printf("e2e: printf %d %s\n", 7, "ok") != 17) return 0x100 + 8;
    return 1;
}

static unsigned int libc_string_checks(void){
    char* end = 0;
    if(atoi("  -123") != -123) return 0x100 + 1;
    if(strtol("0x1f", &end, 0) != 31 || *end) return 0x100 + 2;
    errno = 0;
    strtol("999999999999999999999", &end, 10);
    if(errno != ERANGE) return 0x100 + 3;
    const char* s = "hello world";
    if(strchr(s, 'w') != s + 6 || strstr(s, "lo w") != s + 3) return 0x100 + 4;
    char cat[16] = "ab";
    strcat(cat, "cd");
    if(strcmp(cat, "abcd") != 0) return 0x100 + 5;
    errno = 0;
    if(calloc((size_t)1 << 62, 16) != 0 || errno != ENOMEM) return 0x100 + 6;
    char a[12], b[12];
    itoa_r(-5, a, sizeof a, 10);
    itoa_r(255, b, sizeof b, 16);
    if(strcmp(a, "-5") != 0 || strcmp(b, "ff") != 0) return 0x100 + 7;
    struct command_argument* c = samos_parse_command("echo \"a b\" 'c'", 64);
    int ok = c && c->next && strcmp(c->next->argument, "a b") == 0
               && c->next->next && strcmp(c->next->next->argument, "c") == 0;
    samos_free_command(c);
    if(!ok) return 0x100 + 8;
    return 1;
}

int main(int argc, char** argv){
    (void)argc;
    (void)argv;

    // SamOs e2e: prove we are in ring 3 and int 0x80 reached
    // the kernel by writing the canary slot first. If the kernel
    // never sees this, the user-side selftest harness is broken
    // (process_load_switch failed, ring 3 transition didn't iretq,
    // or the syscall dispatcher dropped the call).
    samos_e2e_mark(USER_INT80_REACH, 1);

    // Libc.
    samos_e2e_mark(USER_LIBC_PRINTF,  libc_printf_checks());
    samos_e2e_mark(USER_LIBC_STRINGS, libc_string_checks());

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
    // Close the probe's line at once. A partial line
    // goes to the UART before the return to ring 3, so with the
    // '!' left open the next line any CPU sent would follow it.
    samos_putchar('!');
    samos_putchar('\n');
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

    // SamOs e2e: every marker except the keyboard probe
    // below is written by now. The host harness waits for this
    // serial line instead of sleeping a fixed time (tests64/e2e/
    // _lib.sh, E2E_SENTINEL). It used to start with a newline to end
    // the probe's line; the probe closes its own line now.
    print("e2e: user selftest done\n");

    // L82-L88 / L122 PS/2 keyboard end to end: poll the keyboard
    // buffer for up to ~15 seconds. The host-side e2e test
    // (116-user-keyboard-input) uses QEMU 'sendkey' to inject
    // a keypress shortly after boot. The kernel's classic PS/2
    // driver receives IRQ1, pushes the char into our process's
    // keyboard buffer, and samos_getkey returns it.
    //
    // We sleep 50 ms between polls via samos_udelay so the
    // scheduler has time to run the IRQ handler + the rest
    // of the timer / mouse paths between checks. 300 iterations
    // = ~15 seconds, which is comfortably past the host-side
    // sendkey timing.
    {
        int got_key = 0;
        for (int i = 0; i < 300; i++) {
            int k = samos_getkey();
            if (k != 0) { got_key = 1; break; }
            samos_udelay(50 * 1000); // 50 ms
        }
        samos_e2e_mark(USER_KEY_RECEIVED, got_key ? 1 : 0);
    }

    // Boot to the shell. The kernel starts BLANK.ELF with no
    // arguments; then the selftest hands over to SHELL.ELF and
    // exits instead of spinning. Started from the shell (argc >= 1)
    // it just exits, so the shell gets its prompt back.
    if(argc == 0){
        samos_system_run("shell.elf");
    }
    return 0;
}
