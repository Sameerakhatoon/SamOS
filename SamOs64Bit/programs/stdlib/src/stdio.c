#include "stdio.h"
#include "samos.h"
#include "stdlib.h"
#include <stdarg.h>
#include <stdint.h>

int putchar(int c){
    samos_putchar((char)c);
    return 0;
}

// One formatter behind printf and the snprintf family.
// Output goes through a sink: either a bounded buffer or the
// console. Supported: %d %i %u %x %X %p %c %s %%, the l / ll / z
// length modifiers, and the '-' and '0' flags with a field width
// (a number or *). Unknown conversions are printed verbatim.
struct format_sink {
    char*  buffer;     // NULL = write to the console
    size_t size;       // capacity of buffer, including the NUL
    size_t written;    // characters produced so far (may exceed size)
    // Console mode batches output so printf costs one print syscall
    // per 127 bytes instead of one putchar syscall per byte.
    char   pending[128];
    int    pending_len;
};

static void sink_flush(struct format_sink* sink){
    if(sink->buffer || sink->pending_len == 0){
        return;
    }
    sink->pending[sink->pending_len] = '\0';
    print(sink->pending);
    sink->pending_len = 0;
}

static void sink_put(struct format_sink* sink, char c){
    if(sink->buffer){
        if(sink->size > 0 && sink->written < sink->size - 1){
            sink->buffer[sink->written] = c;
        }
    } else {
        if(sink->pending_len >= (int)sizeof(sink->pending) - 1){
            sink_flush(sink);
        }
        sink->pending[sink->pending_len++] = c;
    }
    sink->written++;
}

static void sink_pad(struct format_sink* sink, char c, int count){
    for(int i = 0; i < count; i++){
        sink_put(sink, c);
    }
}

// Emit `digits` (length len, no sign) with an optional sign/prefix,
// honouring width, left-justify and zero-pad.
static void sink_number(struct format_sink* sink, const char* prefix,
                        const char* digits, int len,
                        int width, int left, int zero){
    int prefix_len = 0;
    while(prefix[prefix_len]){
        prefix_len++;
    }
    int pad = width - prefix_len - len;
    if(pad < 0){
        pad = 0;
    }
    if(!left && !zero){
        sink_pad(sink, ' ', pad);
    }
    for(int i = 0; i < prefix_len; i++){
        sink_put(sink, prefix[i]);
    }
    if(!left && zero){
        sink_pad(sink, '0', pad);
    }
    for(int i = 0; i < len; i++){
        sink_put(sink, digits[i]);
    }
    if(left){
        sink_pad(sink, ' ', pad);
    }
}

static int format_unsigned(unsigned long long value, unsigned base, int upper, char* out){
    const char* digit_chars = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = digit_chars[value % base];
        value /= base;
    } while(value);
    for(int i = 0; i < n; i++){
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

static void format_to_sink(struct format_sink* sink, const char* fmt, va_list ap){
    for(const char* p = fmt; *p; p++){
        if(*p != '%'){
            sink_put(sink, *p);
            continue;
        }
        p++;
        // A lone '%' at the end used to step past the NUL
        // and keep formatting whatever followed in memory. Print it and stop.
        if(*p == '\0'){
            sink_put(sink, '%');
            break;
        }

        int left = 0, zero = 0;
        for(;; p++){
            if(*p == '-'){
                left = 1;
            } else if(*p == '0'){
                zero = 1;
            } else {
                break;
            }
        }
        int width = 0;
        if(*p == '*'){
            width = va_arg(ap, int);
            if(width < 0){
                left = 1;
                width = -width;
            }
            p++;
        } else {
            while(*p >= '0' && *p <= '9'){
                width = width * 10 + (*p - '0');
                p++;
            }
        }
        int longs = 0;
        while(*p == 'l'){
            longs++;
            p++;
        }
        if(*p == 'z'){
            longs = 1;
            p++;
        }
        if(*p == '\0'){
            break;
        }

        char digits[24];
        int  len = 0;
        switch(*p){
            case 'd':
            case 'i': {
                long long v = longs >= 2 ? va_arg(ap, long long)
                            : longs == 1 ? va_arg(ap, long)
                            : va_arg(ap, int);
                // Negate as unsigned so LLONG_MIN does not overflow.
                unsigned long long magnitude = v < 0 ? 0ULL - (unsigned long long)v
                                                     : (unsigned long long)v;
                len = format_unsigned(magnitude, 10, 0, digits);
                sink_number(sink, v < 0 ? "-" : "", digits, len, width, left, zero);
                break;
            }
            case 'u':
            case 'x':
            case 'X': {
                unsigned long long v = longs >= 2 ? va_arg(ap, unsigned long long)
                                     : longs == 1 ? va_arg(ap, unsigned long)
                                     : va_arg(ap, unsigned int);
                len = format_unsigned(v, *p == 'u' ? 10 : 16, *p == 'X', digits);
                sink_number(sink, "", digits, len, width, left, zero);
                break;
            }
            case 'p': {
                uintptr_t v = (uintptr_t)va_arg(ap, void*);
                len = format_unsigned(v, 16, 0, digits);
                sink_number(sink, "0x", digits, len, width, left, zero);
                break;
            }
            case 'c': {
                digits[0] = (char)va_arg(ap, int);
                sink_number(sink, "", digits, 1, width, left, 0);
                break;
            }
            case 's': {
                const char* s = va_arg(ap, const char*);
                if(!s){
                    s = "(null)";
                }
                int slen = 0;
                while(s[slen]){
                    slen++;
                }
                sink_number(sink, "", s, slen, width, left, 0);
                break;
            }
            case '%':
                sink_put(sink, '%');
                break;
            default:
                sink_put(sink, '%');
                sink_put(sink, *p);
                break;
        }
    }
}

int vsnprintf(char* buffer, size_t size, const char* fmt, va_list ap){
    struct format_sink sink = {0};
    sink.buffer = buffer;
    sink.size   = size;
    format_to_sink(&sink, fmt, ap);
    if(buffer && size > 0){
        buffer[sink.written < size ? sink.written : size - 1] = '\0';
    }
    return (int)sink.written;
}

int snprintf(char* buffer, size_t size, const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buffer, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char* buffer, const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buffer, (size_t)-1 / 2, fmt, ap);
    va_end(ap);
    return n;
}

int vprintf(const char* fmt, va_list ap){
    struct format_sink sink = {0};
    format_to_sink(&sink, fmt, ap);
    sink_flush(&sink);
    return (int)sink.written;
}

int printf(const char* fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char* s){
    print(s);
    putchar('\n');
    return 0;
}
