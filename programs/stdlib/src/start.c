#include "samos.h"

extern int main(int argc, char** argv);

void c_start(){
    struct process_arguments arguments;
    samos_process_get_arguments(&arguments);
    int res = main(arguments.argc, arguments.argv);
    (void)res;
    samos_exit();
}
