#include "samos.h"
#include "stdlib.h"
#include "stdio.h"

int main(int argc, char** argv){
    printf("My age is %i\n", 98);
    print("Hello how are you!\n");

    char buf[1024];
    samos_terminal_readline(buf, sizeof(buf), true);
    print(buf);

    while(1){}
    return 0;
}
