#include "stdio.h"
#include "samos.h"

int putchar(int c){
    samos_putchar((char)c);
    return 0;
}
