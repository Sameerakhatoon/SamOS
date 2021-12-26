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

int main(int argc, char** argv){
    (void)argc;
    (void)argv;

    int fd = fopen("@:/blank.elf", "r");
    if(fd > 0){
        // Lecture 110 - read the first 512 bytes of the ELF
        // header through the L107 fread path. buf lives on the
        // user stack; process_validate_memory_or_terminate hits
        // the L109 stack-region branch.
        char buf[512] = {0};
        printf("File blank.elf opened\n");
        fread(buf, 1, sizeof(buf), fd);
        fclose(fd);                   // L106 - close on success
    }else{
        printf("File blank.elf opened failed\n");
    }

    while(1){
    }
    return 0;
}
