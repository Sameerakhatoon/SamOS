#include "string.h"

char tolower(char s1){
    if(s1 >= 65 && s1 <= 90){
        s1 += 32;
    }
    return s1;
}

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

int strnlen_terminator(const char* str, int max, char terminator){
    int i = 0;
    for(i = 0; i < max; i++){
        if(str[i] == '\0' || str[i] == terminator){
            break;
        }
    }
    return i;
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

bool isdigit(char c){
    return c >= 48 && c <= 57;
}

int tonumericdigit(char c){
    return c - 48;
}

static char* sp = 0;

char* strtok(char* str, const char* delimiters){
    int i = 0;
    int len = strlen(delimiters);
    if(!str && !sp){
        return 0;
    }
    // A new string always restarts tokenising. Upstream only
    // took `str` when the previous string had been fully consumed,
    // so abandoning a tokenise half way made the next call carry on
    // in the old (possibly freed) buffer.
    if(str){
        sp = str;
    }

    char* p_start = sp;
    while(1){
        for(i = 0; i < len; i++){
            if(*p_start == delimiters[i]){
                p_start++;
                break;
            }
        }
        if(i == len){
            sp = p_start;
            break;
        }
    }

    if(*sp == '\0'){
        sp = 0;
        return sp;
    }

    while(*sp != '\0'){
        for(i = 0; i < len; i++){
            if(*sp == delimiters[i]){
                *sp = '\0';
                break;
            }
        }
        sp++;
        if(i < len){
            break;
        }
    }
    return p_start;
}

// The rest of the everyday <string.h> surface.
int strcmp(const char* str1, const char* str2){
    const unsigned char* a = (const unsigned char*)str1;
    const unsigned char* b = (const unsigned char*)str2;
    while(*a && *a == *b){
        a++;
        b++;
    }
    return *a - *b;
}

char* strchr(const char* str, int c){
    for(;; str++){
        if(*str == (char)c){
            return (char*)str;
        }
        if(*str == '\0'){
            return 0;
        }
    }
}

char* strrchr(const char* str, int c){
    const char* last = 0;
    for(;; str++){
        if(*str == (char)c){
            last = str;
        }
        if(*str == '\0'){
            return (char*)last;
        }
    }
}

char* strstr(const char* haystack, const char* needle){
    if(*needle == '\0'){
        return (char*)haystack;
    }
    for(; *haystack; haystack++){
        const char* h = haystack;
        const char* n = needle;
        while(*h && *n && *h == *n){
            h++;
            n++;
        }
        if(*n == '\0'){
            return (char*)haystack;
        }
    }
    return 0;
}

char* strcat(char* dest, const char* src){
    strcpy(dest + strlen(dest), src);
    return dest;
}

// Appends at most count bytes of src and always NUL-terminates
// (C semantics: dest needs strlen(dest) + count + 1 bytes).
char* strncat(char* dest, const char* src, int count){
    char* d = dest + strlen(dest);
    while(count-- > 0 && *src){
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

bool isspace(char c){
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
