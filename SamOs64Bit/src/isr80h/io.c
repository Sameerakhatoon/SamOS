#include "io.h"
#include "task/task.h"
#include "task/process.h"   // L158 - process_print / process_print_char
#include "keyboard/keyboard.h"
#include "kernel.h"

// SYSTEM_COMMAND1_PRINT: pop the user's char* off the stack, copy the
// string out of user-space using the page-table portal trick, hand it
// to the kernel's print().
void* isr80h_command1_print(struct interrupt_frame* frame){
    void* user_space_msg_buffer = task_get_stack_item(task_current(), 0);

    char buf[1024];
    copy_string_from_task(task_current(), user_space_msg_buffer, buf, sizeof(buf));

    // Lecture 158 - route through the process's sysout window
    // instead of the kernel console.
    process_print(task_current()->process, buf);
    return 0;
}

void* isr80h_command2_getkey(struct interrupt_frame* frame){
    char c = keyboard_pop();
    // Lecture 54 - cast widened to intptr_t so 'c' (a char with
    // its sign extended through int) doesn't trigger the int-to-
    // pointer-of-different-size diagnostic on x86_64.
    return (void*)((intptr_t)c);
}

void* isr80h_command3_putchar(struct interrupt_frame* frame){
    // L54 - widen the intermediate cast to intptr_t too. The
    // stack item is a void*; truncating through int and then
    // back to a pointer was fine on 32-bit but loses the high
    // half on 64-bit.
    char c = (char)(intptr_t)task_get_stack_item(task_current(), 0);
    // Lecture 158 - putchar lands in the sysout window too.
    process_print_char(task_current()->process, c);
    return 0;
}
