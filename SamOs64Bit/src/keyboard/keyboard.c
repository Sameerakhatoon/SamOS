#include "keyboard.h"
#include "classic.h"
#include "virtio_input.h"
#include "status.h"
#include "kernel.h"
#include "task/process.h"
#include "task/task.h"
#include "graphics/window.h"         // window_focused
#include "memory/memory.h"           // L174 - memcpy for listener clone
#include "memory/heap/kheap.h"       // L174 - kzalloc / kfree for listeners
#include "graphics/graphics.h"       // gfx_lock

static struct keyboard* keyboard_list_head = 0;
static struct keyboard* keyboard_list_last = 0;

// Keys dropped because a process ring was full.
static unsigned int keyboard_dropped = 0;

static int keyboard_get_tail_index(struct process* process){
    return process->keyboard.tail % sizeof(process->keyboard.buffer);
}

// Bytes waiting in the ring. head and tail only ever grow (and are
// rewound together when the ring drains), so tail - head is the
// fill level.
static int keyboard_buffer_used(struct process* process){
    return process->keyboard.tail - process->keyboard.head;
}

unsigned int keyboard_dropped_count(void){
    return keyboard_dropped;
}

void keyboard_init(){
    keyboard_insert(classic_init());
    // virtio-input keyboards feed the same translator and
    // keyboard_push; PS/2 stays the default keyboard.
    virtio_input_init();
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
    if(res >= 0){
        // Lecture 174 - keyboard.c owns the listener vector. The
        // driver's init must not pre-populate it; we panic if it
        // did so we catch the bug early.
        if(keyboard->key_listeners != NULL){
            panic("Let the keyboard.c file make the key listeners vectors");
        }
        keyboard->key_listeners =
            vector_new(sizeof(struct keyboard_listener*), 4, 0);
    }

out:
    return res;
}

void keyboard_backspace(struct process* process){
    // Nothing unread to take back. Without this the tail
    // walked below head and the next push landed behind the
    // reader.
    if(keyboard_buffer_used(process) <= 0){
        return;
    }
    process->keyboard.tail -= 1;
    int real_index = keyboard_get_tail_index(process);
    process->keyboard.buffer[real_index] = 0x00;
}

// Who gets a key: the process that owns the focused
// window, else the foreground process (the one started last in the
// foreground, typically the shell or the child it waits for). The
// book pushed to process_current(), whichever process the IRQ
// happened to interrupt.
static struct process* keyboard_target(void){
    struct window* focused = window_focused();
    struct process* owner = focused ? process_get_from_kernel_window(focused) : NULL;
    return owner ? owner : process_foreground();
}

// The graphics lock (focus, listeners) and, inside it,
// the process lock (target, ring, wakeup), so the target cannot exit
// under us and a getkey_block cannot miss the wakeup.
static void keyboard_push_locked(char c);

void keyboard_push(char c){
    // Reject 0 here: keyboard_pop returns 0 to signal "buffer empty", so
    // legitimately storing a 0 in the buffer would be indistinguishable
    // from emptiness and cause getkey to loop on a real-but-invisible key.
    if(c == 0){
        return;
    }
    gfx_lock();
    keyboard_push_locked(c);
    gfx_unlock();
}

static void keyboard_push_locked(char c){
    process_lock();
    struct process* process = keyboard_target();
    if(!process){
        // Nobody to read it (for one, after the last process
        // has exited); say so, so the idle system visibly answers.
        // Quiet before the timer has ticked: the boot self-test
        // pushes a key with no process around.
        process_unlock();
        if(!task_clock_ticks()){
            return;
        }
        print("keyboard: no process for key '");
        char s[2] = { c, 0 };
        print(c >= 0x20 && c < 0x7F ? s : "?");
        print("'\n");
        return;
    }

    // Bounded ring. The old code wrapped tail straight over
    // unread keys once 1024 were queued, so the reader saw a mix
    // of new and stale bytes. Drop the new key
    // instead and count it.
    if(keyboard_buffer_used(process) >= (int)sizeof(process->keyboard.buffer)){
        keyboard_dropped++;
        process_unlock();
        return;
    }

    int real_index = keyboard_get_tail_index(process);
    process->keyboard.buffer[real_index] = c;
    process->keyboard.tail++;

    // A blocking getkey waits for exactly this.
    struct task* task = process->task;
    if(task && task->state == TASK_BLOCKED && task->block_reason == TASK_BLOCK_KEY){
        task_wake(task);
    }
    process_unlock();

    // Lecture 174 - fan the keypress out to anyone registered on
    // the default keyboard.
    struct keyboard_event keyboard_event = {0};
    keyboard_event.type = KEYBOARD_EVENT_KEY_PRESS;
    keyboard_event.data.key_press.key = c;
    keyboard_push_event_to_listeners(keyboard_default(), &keyboard_event);
}

void keyboard_set_capslock(struct keyboard* keyboard, KEYBOARD_CAPS_LOCK_STATE state){
    keyboard->capslock_state = state;
}

KEYBOARD_CAPS_LOCK_STATE keyboard_get_capslock(struct keyboard* keyboard){
    return keyboard->capslock_state;
}

char keyboard_pop(){
    // Book ships `if (task_current()) return 0;` which is inverted.
    // The early-return is meant to bail when there is NO task; the body
    // below dereferences task_current()->process so the guard must be
    // !task_current().
    if(!task_current()){
        return 0;
    }
    struct process* process = task_current()->process;
    if(keyboard_buffer_used(process) <= 0){
        return 0;
    }
    int real_index = process->keyboard.head % sizeof(process->keyboard.buffer);
    char c = process->keyboard.buffer[real_index];
    process->keyboard.buffer[real_index] = 0;
    process->keyboard.head++;
    if(process->keyboard.head == process->keyboard.tail){
        // Drained: rewind both so the ints never overflow.
        process->keyboard.head = 0;
        process->keyboard.tail = 0;
    }
    return c;
}

int keyboard_get_modifiers(struct keyboard* keyboard){
    if(!keyboard){
        keyboard = keyboard_default();
    }
    return keyboard ? keyboard->modifiers : 0;
}

// Lecture 174 - head of the inserted-keyboards list. When
// listener helpers receive a NULL keyboard they fall back to
// this default.
struct keyboard* keyboard_default(){
    return keyboard_list_head;
}

// Lecture 174 - register a listener on the given keyboard
// (NULL means the default). We clone the caller's listener into
// kheap so the registration outlives the caller's stack frame.
int keyboard_register_handler(struct keyboard* keyboard,
                              struct keyboard_listener keyboard_listener){
    int res = 0;
    struct keyboard_listener* listener_clone = NULL;
    if(keyboard == NULL){
        keyboard = keyboard_default();
    }
    if(!keyboard){
        res = -EINVARG;
        goto out;
    }

    listener_clone = kzalloc(sizeof(struct keyboard_listener));
    if(!listener_clone){
        res = -ENOMEM;
        goto out;
    }
    memcpy(listener_clone, &keyboard_listener, sizeof(struct keyboard_listener));
    vector_push(keyboard->key_listeners, &listener_clone);

out:
    return res;
}

// Lecture 174 - linear-scan the keyboard's listener vector for a
// match on on_event.
struct keyboard_listener* keyboard_get_listener_ptr(struct keyboard* keyboard,
                                                    struct keyboard_listener keyboard_listener){
    struct keyboard_listener* listener_heap_ptr = NULL;
    size_t total_children = vector_count(keyboard->key_listeners);
    for(size_t i = 0; i < total_children; i++){
        struct keyboard_listener* current_listener = NULL;
        vector_at(keyboard->key_listeners, i, &current_listener, sizeof(current_listener));
        if(current_listener &&
           keyboard_listener.on_event &&
           keyboard_listener.on_event == current_listener->on_event){
            listener_heap_ptr = current_listener;
            break;
        }
    }
    return listener_heap_ptr;
}

// Lecture 174 - drain the listener vector through the recorded
// on_event callbacks.
void keyboard_push_event_to_listeners(struct keyboard* keyboard,
                                      struct keyboard_event* event){
    size_t total_children = vector_count(keyboard->key_listeners);
    for(size_t i = 0; i < total_children; i++){
        struct keyboard_listener* listener = NULL;
        vector_at(keyboard->key_listeners, i, &listener, sizeof(listener));
        if(listener){
            listener->on_event(keyboard, event);
        }
    }
}

// Lecture 174 - remove a listener and free its clone.
int keyboard_unregister_handler(struct keyboard* keyboard,
                                struct keyboard_listener keyboard_listener){
    int res = 0;
    struct keyboard_listener* keyboard_listener_ptr =
        keyboard_get_listener_ptr(keyboard, keyboard_listener);
    if(keyboard_listener_ptr){
        vector_pop_element(keyboard->key_listeners,
                           &keyboard_listener_ptr, sizeof(keyboard_listener_ptr));
    }
    kfree(keyboard_listener_ptr);
    return res;
}
