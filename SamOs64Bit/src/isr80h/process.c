#include "process.h"
#include "task/task.h"
#include "task/process.h"
#include "string/string.h"
#include "status.h"
#include "config.h"
#include "kernel.h"
#include "task/uaccess.h"
#include "memory/heap/kheap.h"

// A process started by syscall 6 or 7 is the caller's
// child and takes the keyboard (foreground). The caller gets the
// child's pid in rax and goes back to the run queue; the child runs
// first. The book jumped into the child and the caller later resumed
// with whatever rax held ("the parent never receives a real return
// value").
// The parent goes to this CPU's queue, where an idle CPU may
// take it at once; the child runs here.
static void* isr80h_start_child(struct process* child){
    struct task* parent_task = task_current();
    process_lock();
    child->parent = parent_task->process;
    process_switch(child);
    process_unlock();
    parent_task->registers.rax = (uint64_t)child->pid;
    task_ready(parent_task);
    task_enter(child->task);
    return 0;
}

// The book strcpy'd the user's name after "@:/" into
// a 108-byte stack buffer, a kernel stack overflow for any name over
// 104 bytes. The name is copied with a bound that
// leaves room for the prefix; anything longer is -ENAMETOOLONG.
void* isr80h_command6_process_load_start(struct trap_frame* frame){
    void* filename_user_ptr = isr80h_arg(frame, 0);
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
    res = process_load(path, &process);
    if(res < 0){
        return ERROR(res);
    }
    return isr80h_start_child(process);
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
                                    isr80h_arg(frame, 0), &arguments);
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
    res = process_load(path, &process);
    if(res >= 0){
        res = process_inject_arguments(process, arguments);
        if(res < 0){
            // The book left the half-started process queued.
            process_terminate(process);
        }
    }
    isr80h_free_arguments(arguments);
    if(res < 0){
        return ERROR(res);
    }
    return isr80h_start_child(process);
}

// The book wrote argc/argv through whatever address the user
// passed. copy_to_user checks it.
void* isr80h_command8_get_program_arguments(struct trap_frame* frame){
    struct process* process = task_current()->process;
    struct process_arguments arguments;
    process_get_arguments(process, &arguments.argc, &arguments.argv);
    return ERROR(copy_to_user(process, isr80h_arg(frame, 0),
                              &arguments, sizeof(arguments)));
}

// exit(status): rdi is the status the parent's waitpid gets.
void* isr80h_command9_exit(struct trap_frame* frame){
    struct process* process = task_current()->process;
    process_exit(process, (int)(intptr_t)isr80h_arg(frame, 0));
    task_next();
    return 0;
}

// New syscalls.

// int getpid(void)
void* isr80h_command27_getpid(struct trap_frame* frame){
    return (void*)(intptr_t)task_current()->process->pid;
}

// int waitpid(int pid, int* status): pid > 0 waits for that child,
// pid <= 0 for any child. Returns the child's pid and writes its exit
// status through `status` (may be NULL); -ECHILD if there is no such
// child, -EFAULT for a bad status pointer. Blocks while the child
// runs: the task is woken when a child exits and the syscall runs
// again (task_block).
void* isr80h_command28_waitpid(struct trap_frame* frame){
    struct process* process = task_current()->process;
    int   pid        = (int)(intptr_t)isr80h_arg(frame, 0);
    void* status_ptr = isr80h_arg(frame, 1);
    if(status_ptr && !user_range_ok(process, status_ptr, sizeof(int), true)){
        return ERROR(-EFAULT);
    }
    int status = 0;
    // The look and the block in one hold of the process lock:
    // a child exiting on another CPU takes it to add its zombie and
    // wake the parent, so the wakeup cannot fall in between.
    process_lock();
    int res = process_reap_child(process, pid, &status);
    if(res == 0){
        task_block(task_current(), TASK_BLOCK_WAIT);
    }
    process_unlock();
    if(res == 0){
        task_next();
    }
    if(res > 0 && status_ptr && copy_to_user(process, status_ptr, &status, sizeof(status)) < 0){
        return ERROR(-EFAULT);
    }
    return ERROR(res);
}

// int kill(int pid): ends the process with status 128 + 9. Killing
// yourself does not return.
// Another process is not torn down here: it may be
// running on another CPU. task_kill marks it and its own CPU ends it;
// the caller's waitpid then sees the status as before.
void* isr80h_command29_kill(struct trap_frame* frame){
    int pid = (int)(intptr_t)isr80h_arg(frame, 0);
    process_lock();
    struct process* target = process_get_by_pid(pid);
    if(!target){
        process_unlock();
        return ERROR(-ESRCH);
    }
    if(target == task_current()->process){
        process_unlock();
        process_exit(target, PROCESS_EXIT_KILLED);
        task_next();
    }
    if(target->task){
        task_kill(target->task, PROCESS_EXIT_KILLED);
    }
    process_unlock();
    return 0;
}
