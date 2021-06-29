#ifndef SAMOS_H
#define SAMOS_H

#include <stddef.h>
#include <stdbool.h>

void* samos_malloc(size_t size);
void  samos_free(void* ptr);
void  samos_putchar(char c);
int   samos_getkey();
int   samos_getkeyblock();
void  samos_terminal_readline(char* out, int max, bool output_while_typing);
void  print(const char* msg);

#endif
