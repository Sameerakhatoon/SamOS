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
#include "isr80h/graphics.h"   // L165 - userland_graphics builder
#include "task/task.h"
#include "task/process.h"
#include "task/userlandptr.h"
#include "task/uaccess.h"

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

void* isr80h_command16_window_create(struct trap_frame* frame){
    int res = 0;
    struct process_window* win = NULL;
    void* window_title_user_ptr = isr80h_arg(frame, 0);
    char win_title[WINDOW_MAX_TITLE];
    // A title longer than the buffer is cut short.
    if(strncpy_from_user(task_current()->process, win_title, window_title_user_ptr,
                         sizeof(win_title)) == -EFAULT){
        res = -EFAULT;
        goto out;
    }

    int win_width  = (int)(uintptr_t)isr80h_arg(frame, 1);
    int win_height = (int)(uintptr_t)isr80h_arg(frame, 2);
    int flags      = (int)(uintptr_t)isr80h_arg(frame, 3);
    int id         = (int)(uintptr_t)isr80h_arg(frame, 4);

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
void* isr80h_command17_sysout_to_window(struct trap_frame* frame){
    void* user_win_ptr = isr80h_arg(frame, 0);
    if(user_win_ptr){
        struct process_window* proc_win =
            process_window_get_from_user_window(task_current()->process, user_win_ptr);
        if(proc_win){
            process_set_sysout_window(task_current()->process, proc_win);
        }
    }
    return 0;
}

// Lecture 163 - userland passes in a struct window_event_userland*
// it wants populated with the next event off its ring. We
// translate the userland address through the task's page tables
// and pop directly into it.
void* isr80h_command18_get_window_event(struct trap_frame* frame){
    int res = 0;
    struct window_event_userland* win_event_out = NULL;

    void* win_event_out_virtual_address = isr80h_arg(frame, 0);
    if(!win_event_out_virtual_address){
        res = -EINVARG;
        goto out;
    }

    // Check the buffer before popping, write it with
    // copy_to_user (the book wrote through the translated first byte).
    if(!user_range_ok(task_current()->process, win_event_out_virtual_address,
                      sizeof(*win_event_out), true)){
        res = -EFAULT;
        goto out;
    }

    struct window_event win_event_kern = {0};
    res = process_pop_window_event(task_current()->process, &win_event_kern);
    if(res < 0){
        goto out;
    }

    struct window_event_userland win_event_user = {0};
    win_event_out = &win_event_user;
    window_event_to_userland(&win_event_kern, win_event_out);
    res = copy_to_user(task_current()->process, win_event_out_virtual_address,
                       win_event_out, sizeof(*win_event_out));
out:
    // Upstream returns the int status as a void*. Preserve the
    // sign-extension behaviour by casting through intptr_t.
    return (void*)(intptr_t)res;
}

// Lecture 165 - userland passes its window handle, we hand back
// a userland_graphics it owns. The kernel graphics_info pointer
// stays hidden behind a userland_ptr sentinel.
void* isr80h_command19_window_graphics_get(struct trap_frame* frame){
    void* user_win_ptr = isr80h_arg(frame, 0);
    if(!user_win_ptr){
        return NULL;
    }

    struct window* kern_window = isr80h_window_from_process_window_virt(user_win_ptr);
    if(!kern_window){
        return NULL;
    }

    struct graphics_info* graphics = kern_window->graphics;
    struct userland_graphics* userland_graphics = NULL;
    userland_graphics = isr80h_graphics_make_userland_metadata(task_current()->process, graphics);
    if(!userland_graphics){
        return NULL;
    }

    // Userland owns this pointer and must free it.
    return (void*)userland_graphics;
}

// Lecture 167 - userspace passes its window handle; we kick the
// compositor to redraw it. Registration of this syscall lands
// at L170.
void* isr80h_command21_window_redraw(struct trap_frame* frame){
    void* user_win_ptr = isr80h_arg(frame, 0);
    if(!user_win_ptr){
        return NULL;
    }
    struct window* kern_window = isr80h_window_from_process_window_virt(user_win_ptr);
    if(!kern_window){
        return NULL;
    }
    window_redraw(kern_window);
    return NULL;
}

// Lecture 172 - userland asks the compositor to redraw a body
// region (clip rect). Translates the userland window handle and
// forwards the rect to window_redraw_body_region.
void* isr80h_command23_window_redraw_region(struct trap_frame* frame){
    long rect_x      = (long)isr80h_arg(frame, 0);
    long rect_y      = (long)isr80h_arg(frame, 1);
    long rect_width  = (long)isr80h_arg(frame, 2);
    long rect_height = (long)isr80h_arg(frame, 3);
    void* user_window_ptr = isr80h_arg(frame, 4);
    if(!user_window_ptr){
        return ERROR(-EINVARG);
    }
    struct window* kern_window = isr80h_window_from_process_window_virt(user_window_ptr);
    if(!kern_window){
        return ERROR(-EINVARG);
    }
    window_redraw_body_region(kern_window, rect_x, rect_y, rect_width, rect_height);
    return NULL;
}

// Lecture 173 / 180 - title-setter helper. L173 upstream had
// the function spelled `isr90h_command24_update_window_title`
// (90h vs 80h). L180 fixed it to `isr80h_`. SamOs follows the
// upstream fix.
void* isr80h_command24_update_window_title(struct window* window,
                                           struct trap_frame* frame){
    int res = 0;
    // Copied in; the book used the translated first byte.
    char title[WINDOW_MAX_TITLE];
    if(strncpy_from_user(task_current()->process, title,
                         isr80h_arg(frame, 2), sizeof(title)) == -EFAULT){
        res = -EFAULT;
        goto out;
    }
    window_title_set(window, title);
out:
    return (void*)(intptr_t)res;
}

// Lecture 173 - dispatch updates by subcommand. Stack: [0] is
// the update_type, [1] is the userland window handle, [2..] are
// subcommand-specific payload pointers.
void* isr80h_command24_update_window(struct trap_frame* frame){
    int res = 0;
    struct window* kern_window = NULL;
    uint64_t update_type = (uint64_t)isr80h_arg(frame, 0);
    void* user_win_ptr   = isr80h_arg(frame, 1);
    if(!user_win_ptr){
        res = -EINVARG;
        goto out;
    }
    kern_window = isr80h_window_from_process_window_virt(user_win_ptr);
    if(!kern_window){
        res = -EINVARG;
        goto out;
    };
    switch(update_type){
        case ISR80H_WINDOW_UPDATE_TITLE:
            res = (int)(uintptr_t)isr80h_command24_update_window_title(kern_window, frame);
            break;
        default:
            res = -EINVARG;
    }
out:
    return (void*)(int64_t)res;
}
