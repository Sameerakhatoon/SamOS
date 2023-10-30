// Lecture 154 - userland window-create syscall.
//
// The userland caller passes title/width/height/flags/id on the
// task stack. We bounce title into a kernel-side buffer, then
// ask process_window_create to spin up a kernel window plus a
// userspace mirror. The userspace mirror's virtual address is
// the handle we hand back to userland.

#include "window.h"
#include "graphics/window.h"
#include "graphics/graphics.h"
#include "task/task.h"
#include "task/process.h"
#include "task/userlandptr.h"

#include "status.h"
#include "kernel.h"

// Lecture 154 - resolve a userspace window handle back to the
// kernel window. The handle is a struct process_userspace_window*
// in the process's virtual address space.
struct window* isr80h_window_from_process_window_virt(void* proc_win_virt_addr){
    struct process_window* proc_win =
        process_window_get_from_user_window(task_current()->process, proc_win_virt_addr);
    if(!proc_win){
        return NULL;
    }
    struct window* kern_window = proc_win->kernel_win;
    if(!kern_window){
        return NULL;
    }
    return kern_window;
}

void* isr80h_command16_window_create(struct interrupt_frame* frame){
    int res = 0;
    struct process_window* win = NULL;
    void* window_title_user_ptr = task_get_stack_item(task_current(), 0);
    char win_title[WINDOW_MAX_TITLE];
    res = copy_string_from_task(task_current(), window_title_user_ptr,
                                win_title, sizeof(win_title));
    if(res < 0){
        goto out;
    }

    int win_width  = (int)(uintptr_t)task_get_stack_item(task_current(), 1);
    int win_height = (int)(uintptr_t)task_get_stack_item(task_current(), 2);
    int flags      = (int)(uintptr_t)task_get_stack_item(task_current(), 3);
    int id         = (int)(uintptr_t)task_get_stack_item(task_current(), 4);

    win = process_window_create(task_current()->process, win_title,
                                win_width, win_height, flags, id);
    if(!win){
        res = -EINVARG;
        goto out;
    }

out:
    if(res < 0){
        if(win != NULL){
            // TODO: free the window. Upstream leaves this as a
            // TODO too; we keep the marker.
            win = NULL;
        }
        return NULL;
    }
    return win->user_win;
}

// Lecture 158 - userland called peachos_divert_stdout_to_window
// with a userland window handle. Resolve it and stash on the
// process so process_print / process_print_char land there.
void* isr80h_command17_sysout_to_window(struct interrupt_frame* frame){
    void* user_win_ptr = task_get_stack_item(task_current(), 0);
    if(user_win_ptr){
        struct process_window* proc_win =
            process_window_get_from_user_window(task_current()->process, user_win_ptr);
        if(proc_win){
            process_set_sysout_window(task_current()->process, proc_win);
        }
    }
    return 0;
}
