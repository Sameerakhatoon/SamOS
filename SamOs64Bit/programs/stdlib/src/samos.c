#include "samos.h"
#include "string.h"

// The kernel blocks the task until a key arrives (syscall 32).
// Polled getkey with a 1 ms udelay, because with the timer
// masked a busy poll starved every other task.
int samos_getkeyblock(){
    int val = 0;
    while((val = samos_getkey_block()) == 0){
    }
    return val;
}

// Wait for any child.
int samos_wait(int* status){
    return samos_waitpid(-1, status);
}

int samos_system_run(const char* command){
    char buf[1024];
    strncpy(buf, command, sizeof(buf));
    struct command_argument* root_command_argument = samos_parse_command(buf, sizeof(buf));
    if(!root_command_argument){
        return -1;
    }
    int res = samos_system(root_command_argument);
    // The kernel copies the arguments into the child
    // (process_inject_arguments), so the list is ours to free.
    // Upstream never freed it: one heap block per token, per
    // command, for the life of the shell.
    samos_free_command(root_command_argument);
    return res;
}

void samos_free_command(struct command_argument* command){
    while(command){
        struct command_argument* next = command->next;
        samos_free(command);
        command = next;
    }
}

// Pull the next word out of *cursor into out (at most
// out_size - 1 bytes), honouring quotes:
//   "a b"   one word, backslash escapes \" and \\ inside
//   'a b'   one word, taken literally
//   a\ b    backslash escapes the next character outside quotes
// Adjacent pieces join ("a"'b'c -> abc). Returns 1 if a word was
// found, 0 at end of input, -1 on an unterminated quote.
static int samos_next_word(const char** cursor, char* out, int out_size){
    const char* s = *cursor;
    while(*s == ' ' || *s == '\t'){
        s++;
    }
    if(*s == '\0'){
        *cursor = s;
        return 0;
    }

    int n = 0;
    char quote = 0;
    for(; *s; s++){
        char c = *s;
        if(quote){
            if(c == quote){
                quote = 0;
                continue;
            }
            if(quote == '"' && c == '\\' && (s[1] == '"' || s[1] == '\\')){
                c = *++s;
            }
        } else {
            if(c == ' ' || c == '\t'){
                break;
            }
            if(c == '"' || c == '\''){
                quote = c;
                continue;
            }
            if(c == '\\' && s[1]){
                c = *++s;
            }
        }
        if(n < out_size - 1){
            out[n++] = c;
        }
    }
    out[n] = '\0';
    *cursor = s;
    return quote ? -1 : 1;
}

struct command_argument* samos_parse_command(const char* command, int max){
    struct command_argument* root_command = 0;
    struct command_argument* current      = 0;
    if(max > 1025){
        return 0;
    }

    const char* cursor = command;
    while(1){
        struct command_argument* new_command = samos_malloc(sizeof(struct command_argument));
        if(!new_command){
            break;
        }
        int res = samos_next_word(&cursor, new_command->argument, sizeof(new_command->argument));
        if(res <= 0){
            samos_free(new_command);
            if(res < 0){
                // Unterminated quote: reject the whole line.
                samos_free_command(root_command);
                root_command = 0;
            }
            break;
        }
        new_command->next = 0;
        if(current){
            current->next = new_command;
        } else {
            root_command = new_command;
        }
        current = new_command;
    }
    return root_command;
}

// Line editor. Enter (CR or LF) ends the line; backspace and
// DEL erase one character, but never past the start of the line;
// ESC sequences from the arrow / Home / End / Delete keys are
// swallowed; other control bytes are ignored.
//
// Upstream echoed every key before looking at it and decremented i
// even at column 0, so a backspace on an empty line stored 0x08 in
// the buffer and erased part of the prompt.
void samos_terminal_readline(char* out, int max, bool output_while_typing){
    int i = 0;
    if(max <= 0){
        return;
    }
    while(i < max - 1){
        char key = samos_getkeyblock();
        if(key == '\r' || key == '\n'){
            break;
        }
        if(key == 0x08 || key == 0x7F){
            if(i > 0){
                i--;
                out[i] = 0x00;
                if(output_while_typing){
                    samos_putchar(0x08);
                }
            }
            continue;
        }
        if(key == 0x1B){
            // ESC [ params final, e.g. ESC [ A or ESC [ 3 ~.
            char next = samos_getkeyblock();
            if(next == '['){
                char c;
                do {
                    c = samos_getkeyblock();
                } while(c >= '0' && c <= '9');
            }
            continue;
        }
        if((unsigned char)key < 0x20 && key != '\t'){
            continue;
        }
        if(output_while_typing){
            samos_putchar(key);
        }
        out[i++] = key;
    }
    out[i] = 0x00;
}
