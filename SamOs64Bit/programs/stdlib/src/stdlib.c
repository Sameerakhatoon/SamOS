#include "stdlib.h"
#include "samos.h"
#include "memory.h"   // L115 - memset for calloc
#include "errno.h"

int errno = 0;

void* malloc(size_t size){
    void* ptr = samos_malloc(size);
    if(!ptr){
        errno = ENOMEM;
    }
    return ptr;
}

void free(void* ptr){
    samos_free(ptr);
}

// Lecture 115 - userland calloc. malloc + memset(0).
void* calloc(size_t n_memb, size_t size){
    // n_memb * size used to wrap silently, so a huge request
    // returned a tiny block the caller then overran.
    if(size != 0 && n_memb > (size_t)-1 / size){
        errno = ENOMEM;
        return NULL;
    }
    size_t b_size = n_memb * size;
    void*  ptr    = malloc(b_size);
    if(!ptr){
        return NULL;
    }
    memset(ptr, 0, b_size);
    return ptr;
}

void* realloc(void* ptr, size_t new_size){
    return samos_realloc(ptr, new_size);
}

// Reentrant integer to text. Writes into the caller's
// buffer (at least 12 bytes for base 10, 34 for base 2) and
// returns it, or NULL if the buffer is too small or the base is
// outside 2..36.
char* itoa_r(int value, char* buffer, size_t size, int base){
    if(!buffer || size == 0 || base < 2 || base > 36){
        return NULL;
    }
    char tmp[34];
    int  n = 0;
    // Work on the negative magnitude so INT_MIN does not overflow.
    int  neg = value < 0;
    int  v   = neg ? value : -value;
    do {
        tmp[n++] = "0123456789abcdefghijklmnopqrstuvwxyz"[-(v % base)];
        v /= base;
    } while(v);
    size_t needed = (size_t)n + (neg ? 1 : 0) + 1;
    if(needed > size){
        buffer[0] = '\0';
        return NULL;
    }
    size_t pos = 0;
    if(neg){
        buffer[pos++] = '-';
    }
    while(n > 0){
        buffer[pos++] = tmp[--n];
    }
    buffer[pos] = '\0';
    return buffer;
}

// Upstream signature, kept for existing callers. It returns a
// static buffer, so it is NOT reentrant; new code should use
// itoa_r.
char* itoa(int i){
    static char text[12];
    return itoa_r(i, text, sizeof(text), 10);
}

static int is_space(char c){
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int digit_value(char c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'z') return c - 'a' + 10;
    if(c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

// Shared by strtol / strtoul: parse the magnitude, report the sign.
// Saturates at `limit` and sets errno = ERANGE on overflow.
static unsigned long parse_magnitude(const char* str, char** endptr, int base,
                                     int* negative, unsigned long limit_pos,
                                     unsigned long limit_neg){
    const char* s = str;
    *negative = 0;
    if(base != 0 && (base < 2 || base > 36)){
        errno = EINVAL;
        if(endptr) *endptr = (char*)str;
        return 0;
    }
    while(is_space(*s)){
        s++;
    }
    if(*s == '+' || *s == '-'){
        *negative = (*s == '-');
        s++;
    }
    if((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')
       && digit_value(s[2]) < 16){
        s += 2;
        base = 16;
    } else if(base == 0){
        base = (s[0] == '0') ? 8 : 10;
    }

    unsigned long limit  = *negative ? limit_neg : limit_pos;
    unsigned long result = 0;
    int any = 0, overflow = 0;
    for(;; s++){
        int d = digit_value(*s);
        if(d >= base){
            break;
        }
        any = 1;
        if(overflow){
            continue;
        }
        if(result > (limit - (unsigned long)d) / (unsigned long)base){
            overflow = 1;
            result = limit;
            continue;
        }
        result = result * base + d;
    }
    if(endptr){
        *endptr = (char*)(any ? s : str);
    }
    if(overflow){
        errno = ERANGE;
    }
    return result;
}

long strtol(const char* str, char** endptr, int base){
    int negative = 0;
    unsigned long max = (unsigned long)LONG_MAX;
    unsigned long magnitude = parse_magnitude(str, endptr, base, &negative, max, max + 1);
    if(negative){
        return magnitude == max + 1 ? LONG_MIN : -(long)magnitude;
    }
    return (long)magnitude;
}

unsigned long strtoul(const char* str, char** endptr, int base){
    int negative = 0;
    unsigned long magnitude = parse_magnitude(str, endptr, base, &negative,
                                              (unsigned long)-1, (unsigned long)-1);
    return negative ? (unsigned long)(-(long)magnitude) : magnitude;
}

int atoi(const char* str){
    return (int)strtol(str, NULL, 10);
}

long atol(const char* str){
    return strtol(str, NULL, 10);
}

int abs(int value){
    return value < 0 ? -value : value;
}
