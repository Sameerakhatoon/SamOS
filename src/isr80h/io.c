#include "io.h"
#include "task/task.h"
#include "keyboard/keyboard.h"
#include "kernel.h"

// SYSTEM_COMMAND1_PRINT: pop the user's char* off the stack, copy the
// string out of user-space using the page-table portal trick, hand it
// to the kernel's print().
void* isr80h_command1_print(struct interrupt_frame* frame){
    void* user_space_msg_buffer = task_get_stack_item(task_current(), 0);

    char buf[1024];
    copy_string_from_task(task_current(), user_space_msg_buffer, buf, sizeof(buf));

    print(buf);
    return 0;
}

void* isr80h_command2_getkey(struct interrupt_frame* frame){
    char c = keyboard_pop();
    return (void*)((int)c);
}
