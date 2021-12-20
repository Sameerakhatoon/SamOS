// Lecture 105 - userland fopen wrapper.

#include "file.h"
#include "samos.h"

int fopen(const char* filename, const char* mode){
    return (int)samos_fopen(filename, mode);
}
