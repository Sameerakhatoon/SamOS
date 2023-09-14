// The smallest program the shell can start: print the
// arguments and return. c_start calls samos_exit, so the shell
// gets the CPU (and its prompt) back.
#include "stdio.h"

int main(int argc, char** argv){
    printf("hello: argc=%d\n", argc);
    for(int i = 0; i < argc; i++){
        printf("hello: argv[%d]=%s\n", i, argv[i]);
    }
    return 0;
}
