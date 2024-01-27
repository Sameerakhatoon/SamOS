#include "isr80h/isr80h.h"
#include "io.h"
#include "task/task.h"
#include "task/process.h"   // L158 - process_print / process_print_char
#include "keyboard/keyboard.h"
#include "kernel.h"
#include "task/uaccess.h"
#include "status.h"

// SYSTEM_COMMAND1_PRINT: pop the user's char* off the stack, copy the
// string out of user-space, hand it to the kernel's print().
//
// strncpy_from_user instead of the page-table portal
// trick (copy_string_from_task), which copied from whatever address
// the user named, kernel included. A bad pointer prints nothing and
// returns -EFAULT; a string longer than the buffer is cut short.
void* isr80h_command1_print(struct trap_frame* frame){
    void* user_space_msg_buffer = isr80h_arg(frame, 0);

    char buf[1024];
    long res = strncpy_from_user(task_current()->process, buf, user_space_msg_buffer, sizeof(buf));
    if(res == -EFAULT){
        return ERROR(res);
    }

    // Lecture 158 - route through the process's sysout window
    // instead of the kernel console.
    process_print(task_current()->process, buf);
    return 0;
}

void* isr80h_command2_getkey(struct trap_frame* frame){
    process_lock();
    char c = keyboard_pop();
    process_unlock();
    // Lecture 54 - cast widened to intptr_t so 'c' (a char with
    // its sign extended through int) doesn't trigger the int-to-
    // pointer-of-different-size diagnostic on x86_64.
    return (void*)((intptr_t)c);
}

// Int getkey_block(void): like getkey, but a task with no key
// waiting blocks until keyboard_push hands its process one.
// The user library polled getkey with a 1 ms udelay instead.
// Pop and block under the process lock, which keyboard_push
// holds to push and wake, so a key cannot slip in between.
void* isr80h_command32_getkey_block(struct trap_frame* frame){
    process_lock();
    char c = keyboard_pop();
    if(c == 0){
        task_block(task_current(), TASK_BLOCK_KEY);
    }
    process_unlock();
    if(c == 0){
        task_next();
    }
    return (void*)((intptr_t)c);
}

void* isr80h_command3_putchar(struct trap_frame* frame){
    // L54 - widen the intermediate cast to intptr_t too. The
    // stack item is a void*; truncating through int and then
    // back to a pointer was fine on 32-bit but loses the high
    // half on 64-bit.
    char c = (char)(intptr_t)isr80h_arg(frame, 0);
    // Lecture 158 - putchar lands in the sysout window too.
    process_print_char(task_current()->process, c);
    return 0;
}
