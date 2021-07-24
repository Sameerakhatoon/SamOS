#include "samos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

// Multitasking-demo dispatcher driven by argv[0]. The "control" tasks
// loop forever (Testing!/Abc! prove the scheduler keeps switching). The
// other branches each exercise one Ch 145-149 user-visible code path:
//
//   CRASH-* -> print marker, null-deref to trip idt_handle_exception
//   EXIT-*  -> print marker, return from main; c_start calls samos_exit
//   BS-*    -> print "BS-ABC\b\b\bXYZ" so terminal_backspace rewrites
//              the row in place; tests grep for "BS-XYZ"
int main(int argc, char** argv){
    if(argc < 1 || !argv[0]){
        print("NO-ARGV\n");
        while(1){}
        return 0;
    }
    if(strncmp(argv[0], "CRASH", 5) == 0){
        print("CRASH-RAN\n");
        volatile int* nullp = (volatile int*)0;
        *nullp = 0x42;
        return 0;
    }
    if(strncmp(argv[0], "EXIT", 4) == 0){
        print("EXIT-RAN\n");
        return 0;
    }
    if(strncmp(argv[0], "BS", 2) == 0){
        print("BS-ABC\b\b\bXYZ\n");
        return 0;
    }
    while(1){
        print(argv[0]);
    }
    return 0;
}
