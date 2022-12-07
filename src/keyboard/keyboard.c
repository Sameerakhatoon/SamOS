#include "keyboard.h"
#include "classic.h"
#include "status.h"
#include "kernel.h"
#include "task/process.h"
#include "task/task.h"

static struct keyboard* keyboard_list_head = 0;
static struct keyboard* keyboard_list_last = 0;

static int keyboard_get_tail_index(struct process* process){
    return process->keyboard.tail % sizeof(process->keyboard.buffer);
}

void keyboard_init(){
    keyboard_insert(classic_init());
}

int keyboard_insert(struct keyboard* keyboard){
    int res = 0;
    if(keyboard->init == 0){
        res = -EINVARG;
        goto out;
    }

    if(keyboard_list_last){
        keyboard_list_last->next = keyboard;
        keyboard_list_last       = keyboard;
    } else {
        keyboard_list_head = keyboard;
        keyboard_list_last = keyboard;
    }

    res = keyboard->init();

out:
    return res;
}

void keyboard_backspace(struct process* process){
    process->keyboard.tail -= 1;
    int real_index = keyboard_get_tail_index(process);
    process->keyboard.buffer[real_index] = 0x00;
}

void keyboard_push(char c){
    struct process* process = process_current();
    if(!process){
        return;
    }

    // Reject 0 here: keyboard_pop returns 0 to signal "buffer empty", so
    // legitimately storing a 0 in the buffer would be indistinguishable
    // from emptiness and cause getkey to loop on a real-but-invisible key.
    if(c == 0){
        return;
    }

    int real_index = keyboard_get_tail_index(process);
    process->keyboard.buffer[real_index] = c;
    process->keyboard.tail++;
}

char keyboard_pop(){
    // G06: book ships `if (task_current()) return 0;` which is inverted.
    // The early-return is meant to bail when there is NO task; the body
    // below dereferences task_current()->process so the guard must be
    // !task_current().
    if(!task_current()){
        return 0;
    }
    struct process* process = task_current()->process;
    int real_index = process->keyboard.head % sizeof(process->keyboard.buffer);
    char c = process->keyboard.buffer[real_index];
    if(c == 0x00){
        return 0;
    }
    process->keyboard.buffer[real_index] = 0;
    process->keyboard.head++;
    return c;
}
