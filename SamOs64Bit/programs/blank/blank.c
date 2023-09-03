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
        printf("File blank.elf opened\n");
    }else{
        printf("File blank.elf opened failed\n");
    }

    while(1){
    }
    return 0;
}
