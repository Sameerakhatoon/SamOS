#include "string.h"

int strlen(const char* ptr){
    int i = 0;
    while(*ptr != 0){
        i++;
        ptr += 1;
    }
    return i;
}

int strnlen(const char* ptr, int max){
    int i = 0;
    for(i = 0; i < max; i++){
        if(ptr[i] == 0){
            break;
        }
    }
    return i;
}

bool isdigit(char c){
    return c >= 48 && c <= 57;
}

int tonumericdigit(char c){
    return c - 48;
}

char* strcpy(char* dest, const char* src){
    char* res = dest;
    while(*src != 0){
        *dest = *src;
        src  += 1;
        dest += 1;
    }
    *dest = 0x00;
    return res;
}

char* strncpy(char* dest, const char* src, int count){
    int i = 0;
    for(i = 0; i < count - 1; i++){
        if(src[i] == 0x00){
            break;
        }
        dest[i] = src[i];
    }
    dest[i] = 0x00;
    return dest;
}

char tolower(char s1){
    if(s1 >= 65 && s1 <= 90){
        s1 += 32;
    }
    return s1;
}

int strncmp(const char* str1, const char* str2, int n){
    unsigned char u1, u2;
    while(n-- > 0){
        u1 = (unsigned char)*str1++;
        u2 = (unsigned char)*str2++;
        if(u1 != u2){
            return u1 - u2;
        }
        if(u1 == '\0'){
            return 0;
        }
    }
    return 0;
}

int istrncmp(const char* s1, const char* s2, int n){
    unsigned char u1, u2;
    while(n-- > 0){
        u1 = (unsigned char)*s1++;
        u2 = (unsigned char)*s2++;
        if(u1 != u2 && tolower(u1) != tolower(u2)){
            return u1 - u2;
        }
        if(u1 == '\0'){
            return 0;
        }
    }
    return 0;
}

int strnlen_terminator(const char* str, int max, char terminator){
    int i = 0;
    for(i = 0; i < max; i++){
        if(str[i] == '\0' || str[i] == terminator){
            break;
        }
    }
    return i;
}

// Lecture 16 - int -> decimal string.
//
// The trick: flip a positive input to its negative twin and
// generate digits while looping toward zero from the negative
// side. That handles INT_MIN cleanly (its positive twin does not
// fit in a signed int; the negative one does).
//
// Digit extraction uses ('0' - (i % 10)) because i is negative
// for the entire loop body. In C, for negative i, i%10 is also
// negative (well-defined since C99), so '0' - (-d) = '0' + d.
//
// Returns a pointer into a static buffer - NOT reentrant, do not
// nest calls or capture across calls. itoa(123) followed by
// itoa(456) clobbers the first result.
char* itoa(int i){
    static char text[12];
    int loc = 11;
    text[11] = 0;
    char neg = 1;
    if(i >= 0){
        neg = 0;
        i = -i;
    }
    while(i){
        text[--loc] = '0' - (i % 10);
        i /= 10;
    }
    if(loc == 11){
        text[--loc] = '0';
    }
    if(neg){
        text[--loc] = '-';
    }
    return &text[loc];
}
