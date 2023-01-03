#ifndef SAMOS_H
#define SAMOS_H

#include <stddef.h>

void* samos_malloc(size_t size);
void  samos_free(void* ptr);
void  print(const char* msg);
int   getkey();

#endif
