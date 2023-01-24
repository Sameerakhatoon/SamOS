#include "shell.h"
#include "stdio.h"
#include "stdlib.h"
#include "samos.h"

int main(int argc, char** argv){
    print("SamOs v1.0.0\n");
    while(1){
        print("> ");
        char buf[1024];
        samos_terminal_readline(buf, sizeof(buf), true);
        samos_process_load_start(buf);
        print("\n");
    }
    return 0;
}
