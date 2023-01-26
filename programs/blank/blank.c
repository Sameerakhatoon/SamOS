#include "samos.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char** argv){
    char words[] = "hello how are you";
    char* token = strtok(words, " ");
    while(token){
        printf("%s\n", token);
        token = strtok(0, " ");
    }
    while(1){}
    return 0;
}
