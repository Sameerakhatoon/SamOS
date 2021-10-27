// Lecture 66 - minimal blank user program.
//
// Just spins forever printing "Hello world" via the print
// syscall. The compute loop in the middle slows the rate so
// prints don't flood VGA.

#include "samos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char** argv){
    (void)argc;
    (void)argv;
    while(1){
        print("Hello world\n");
        for(int i = 0; i < 1000000; i++){
        }
    }
    return 0;
}
