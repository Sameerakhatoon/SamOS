#include "samos.h"

extern int main(int argc, char** argv);

// Main's return value is the exit status the parent's
// samos_waitpid receives.
void c_start(){
    struct process_arguments arguments;
    samos_process_get_arguments(&arguments);
    int res = main(arguments.argc, arguments.argv);
    samos_exit(res);
}
