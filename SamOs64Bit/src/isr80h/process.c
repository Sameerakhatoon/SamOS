#include "process.h"
#include "task/task.h"
#include "task/process.h"
#include "string/string.h"
#include "status.h"
#include "config.h"
#include "kernel.h"
#include "task/uaccess.h"
#include "memory/heap/kheap.h"

// The book strcpy'd the user's name after "@:/" into
// a 108-byte stack buffer, a kernel stack overflow for any name over
// 104 bytes. The name is copied with a bound that
// leaves room for the prefix; anything longer is -ENAMETOOLONG.
void* isr80h_command6_process_load_start(struct trap_frame* frame){
    void* filename_user_ptr = task_get_stack_item(task_current(), 0);
    char  filename[SAMOS_MAX_PATH - 3];
    long  res = strncpy_from_user(task_current()->process, filename, filename_user_ptr,
                                  sizeof(filename));
    if(res < 0){
        return ERROR(res);
    }

    char path[SAMOS_MAX_PATH];
    // Lecture 102 - "@" resolves to the primary FS disk through
    // the L81 path parser, so userland exec is not bound to
    // disk 0.
    strcpy(path, "@:/");
    strcpy(path + 3, filename);

    struct process* process = 0;
    res = process_load_switch(path, &process);
    if(res < 0){
        return ERROR(res);
    }

    task_enter(process->task);
    return 0;
}

// Syscall 7 hands the kernel a linked list of
// struct command_argument in user memory. The book walked it in
// place, following `next` pointers the user wrote, wherever they
// pointed. Each node is copied in instead, the walk is bounded and
// every argument is NUL-terminated.
#define ISR80H_MAX_ARGUMENTS 64

static void isr80h_free_arguments(struct command_argument* arg){
    while(arg){
        struct command_argument* next = arg->next;
        kfree(arg);
        arg = next;
    }
}

static int isr80h_copy_arguments(struct process* process, struct command_argument* uptr,
                                 struct command_argument** out){
    struct command_argument* root = NULL;
    struct command_argument* last = NULL;
    int res = 0;
    for(int count = 0; uptr; count++){
        if(count == ISR80H_MAX_ARGUMENTS){
            res = -EINVARG;
            break;
        }
        struct command_argument* arg = kzalloc(sizeof(struct command_argument));
        if(!arg){
            res = -ENOMEM;
            break;
        }
        if(copy_from_user(process, arg, uptr, sizeof(*arg)) < 0){
            kfree(arg);
            res = -EFAULT;
            break;
        }
        uptr = arg->next;
        arg->next = NULL;
        arg->argument[sizeof(arg->argument) - 1] = 0;
        if(last){
            last->next = arg;
        }else{
            root = arg;
        }
        last = arg;
    }
    if(res < 0){
        isr80h_free_arguments(root);
        root = NULL;
    }
    *out = root;
    return res;
}

void* isr80h_command7_invoke_system_command(struct trap_frame* frame){
    struct command_argument* arguments = NULL;
    int res = isr80h_copy_arguments(task_current()->process,
                                    task_get_stack_item(task_current(), 0), &arguments);
    if(res < 0){
        return ERROR(res);
    }
    if(!arguments || strlen(arguments->argument) == 0){
        isr80h_free_arguments(arguments);
        return ERROR(-EINVARG);
    }

    const char* program_name = arguments->argument;
    if(strlen(program_name) > SAMOS_MAX_PATH - 4){
        isr80h_free_arguments(arguments);
        return ERROR(-ENAMETOOLONG);
    }

    char path[SAMOS_MAX_PATH];
    // Lecture 102 - same @-prefix as command 6; the shell uses
    // this path to launch siblings.
    strcpy(path, "@:/");
    strcpy(path + 3, program_name);

    struct process* process = 0;
    res = process_load_switch(path, &process);
    if(res >= 0){
        res = process_inject_arguments(process, arguments);
    }
    isr80h_free_arguments(arguments);
    if(res < 0){
        return ERROR(res);
    }

    task_enter(process->task);
    return 0;
}

// The book wrote argc/argv through whatever address the user
// passed. copy_to_user checks it.
void* isr80h_command8_get_program_arguments(struct trap_frame* frame){
    struct process* process = task_current()->process;
    struct process_arguments arguments;
    process_get_arguments(process, &arguments.argc, &arguments.argv);
    return ERROR(copy_to_user(process, task_get_stack_item(task_current(), 0),
                              &arguments, sizeof(arguments)));
}

void* isr80h_command9_exit(struct trap_frame* frame){
    struct process* process = task_current()->process;
    process_terminate(process);
    task_next();
    return 0;
}
