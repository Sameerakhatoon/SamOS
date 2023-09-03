#ifndef USERLAND_FILE_H
#define USERLAND_FILE_H
// Lecture 105 - userland fopen. Thin wrapper over the
// samos_fopen syscall trampoline.

int fopen(const char* filename, const char* mode);

#endif
