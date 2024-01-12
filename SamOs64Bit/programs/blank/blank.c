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

    while(1){
    }
    return 0;
}
