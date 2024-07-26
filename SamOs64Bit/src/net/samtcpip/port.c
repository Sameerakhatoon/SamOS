// Libc for the SamTcpIp port (port.h): a small printf with a
// sink, inet_pton and inet_ntop, strcmp, the log level.

#include "port.h"
#include "log.h"
#include "io/serial.h"


static char*  sink_buf;
static size_t sink_size, sink_len;

int strcmp(const char* a, const char* b){
    while(*a && *a == *b){
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

struct fmt_out {
    char*  buf;
    size_t size, len;
};

static void fmt_putc(struct fmt_out* o, char c){
    if(o->len + 1 < o->size){
        o->buf[o->len] = c;
    }
    o->len++;
}

static void fmt_pad(struct fmt_out* o, const char* s, int n, int width, bool left, char pad){
    for(int i = n; !left && i < width; i++){
        fmt_putc(o, pad);
    }
    for(int i = 0; i < n; i++){
        fmt_putc(o, s[i]);
    }
    for(int i = n; left && i < width; i++){
        fmt_putc(o, ' ');
    }
}

// %d %i %u %x %X %p %s %c %%, flags - and 0, a width (or *), the
// length modifiers l, ll, z and h.
int samtcpip_vsnprintf(char* out, size_t size, const char* fmt, va_list ap){
    struct fmt_out o = { out, size, 0 };
    for(; *fmt; fmt++){
        if(*fmt != '%'){
            fmt_putc(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false;
        char pad = ' ';
        for(; *fmt == '-' || *fmt == '0'; fmt++){
            if(*fmt == '-'){
                left = true;
            } else {
                pad = '0';
            }
        }
        int width = 0;
        if(*fmt == '*'){
            width = va_arg(ap, int);
            fmt++;
        }
        while(*fmt >= '0' && *fmt <= '9'){
            width = width * 10 + (*fmt++ - '0');
        }
        int lng = 0;
        while(*fmt == 'l' || *fmt == 'z' || *fmt == 'h'){
            if(*fmt != 'h'){
                lng = 1;
            }
            fmt++;
        }
        char num[24];
        int n = 0;
        unsigned long long v;
        bool neg = false;
        switch(*fmt){
        case 'd': case 'i': {
            long long sv = lng ? va_arg(ap, long) : va_arg(ap, int);
            neg = sv < 0;
            v = neg ? -(unsigned long long)sv : (unsigned long long)sv;
            goto decimal;
        }
        case 'u':
            v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
        decimal:
            do {
                num[sizeof(num) - 1 - n++] = (char)('0' + v % 10);
                v /= 10;
            } while(v);
            if(neg){
                num[sizeof(num) - 1 - n++] = '-';
            }
            fmt_pad(&o, num + sizeof(num) - n, n, width, left, pad);
            break;
        case 'x': case 'X': case 'p':
            v = *fmt == 'p' ? (unsigned long long)(uintptr_t)va_arg(ap, void*)
                            : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            do {
                num[sizeof(num) - 1 - n++] = "0123456789abcdef"[v & 15] - (*fmt == 'X' && (v & 15) > 9 ? 32 : 0);
                v >>= 4;
            } while(v);
            fmt_pad(&o, num + sizeof(num) - n, n, width, left, pad);
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if(!s){
                s = "(null)";
            }
            fmt_pad(&o, s, strlen(s), width, left, ' ');
            break;
        }
        case 'c':
            num[0] = (char)va_arg(ap, int);
            fmt_pad(&o, num, 1, width, left, ' ');
            break;
        case '%':
            fmt_putc(&o, '%');
            break;
        case 0:
            fmt--;
            break;
        default:
            fmt_putc(&o, '%');
            fmt_putc(&o, *fmt);
        }
    }
    if(size){
        out[o.len < size ? o.len : size - 1] = 0;
    }
    return (int)o.len;
}

int samtcpip_snprintf(char* out, size_t size, const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    int n = samtcpip_vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return n;
}

void samtcpip_sink_begin(char* buf, size_t size){
    sink_buf  = buf;
    sink_size = size;
    sink_len  = 0;
    if(size){
        buf[0] = 0;
    }
}

size_t samtcpip_sink_end(void){
    size_t n = sink_len < sink_size ? sink_len : (sink_size ? sink_size - 1 : 0);
    sink_buf = NULL;
    return n;
}

static int samtcpip_vprint(bool gated, const char* fmt, va_list ap){
    int n = 0;
    if(sink_buf){
        size_t room = sink_len < sink_size ? sink_size - sink_len : 0;
        n = samtcpip_vsnprintf(sink_buf + (room ? sink_len : 0), room, fmt, ap);
        sink_len += n;
    } else if(!gated || g_log_level <= LOG_LEVEL_INFO){
        char line[256];
        n = samtcpip_vsnprintf(line, sizeof(line), fmt, ap);
        serial_write(line);
    }
    return n;
}

// Unlevelled printf calls (most of SamTcpIp) count as INFO, as upstream
// ch37 says.
int samtcpip_printf(const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    int n = samtcpip_vprint(true, fmt, ap);
    va_end(ap);
    return n;
}

// LOG_*: the macro has checked the level.
int samtcpip_logf(const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    int n = samtcpip_vprint(false, fmt, ap);
    va_end(ap);
    return n;
}

int inet_pton(int af, const char* src, void* dst){
    uint8_t ip[4];
    for(int i = 0; i < 4; i++){
        int v = 0, digits = 0;
        while(*src >= '0' && *src <= '9' && digits < 4){
            v = v * 10 + (*src++ - '0');
            digits++;
        }
        if(!digits || v > 255 || (i < 3 && *src++ != '.')){
            return 0;
        }
        ip[i] = (uint8_t)v;
    }
    if(af != AF_INET || *src){
        return 0;
    }
    memcpy(dst, ip, 4);
    return 1;
}

const char* inet_ntop(int af, const void* src, void* dst, int size){
    const uint8_t* ip = src;
    samtcpip_snprintf(dst, (size_t)size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return dst;
}
