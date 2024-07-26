#ifndef SAMTCPIP_PORT_H
#define SAMTCPIP_PORT_H
// What SamTcpIp took from libc, POSIX and the simulator, in the
// kernel. Every ported file includes this instead of
// <stdio.h>, <stdlib.h>, <string.h>, <assert.h>, <arpa/inet.h>,
// <pthread.h> and <time.h>.
//
//   malloc, calloc, free   kmalloc, kzalloc, kfree
//   printf                 samtcpip_printf: the current sink, which is
//                          the serial log (filtered by g_log_level,
//                          WARN unless samos.net_debug) or a
//                          buffer a syscall hands to user space
//                          (ifconfig, arp, route in the shell)
//   assert                 a kernel panic
//   htons ... ntohl        byte swaps: every SamOS arch is little-endian
//   inet_pton, inet_ntop   AF_INET only
//   clock_gettime          net_now_ms (the counter, in ms)
//   pthread mutexes        the net lock, held by every caller
//                          (netdev.h)

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "kernel.h"
#include "arch/arch.h"
#include "net/netdev.h"

#define malloc(n)       kmalloc(n)
#define calloc(n, s)    kzalloc((size_t)(n) * (size_t)(s))
#define free(p)         kfree(p)
#define printf          samtcpip_printf
#define fflush(f)       ((void)0)
#define assert(x)       do { if(!(x)){ panic("samtcpip: assert " #x "\n"); } } while(0)

// The kernel's take void* and int; SamTcpIp passes const pointers and
// size_t. A function-like macro does not expand itself again.
#define memcpy(d, s, n) memcpy((d), (void*)(s), (int)(n))
#define memcmp(a, b, n) memcmp((void*)(a), (void*)(b), (int)(n))

static inline void* memmove(void* dst, const void* src, size_t n){
    uint8_t* d = dst;
    const uint8_t* s = src;
    if(d < s){
        for(size_t i = 0; i < n; i++){
            d[i] = s[i];
        }
    } else {
        while(n--){
            d[n] = s[n];
        }
    }
    return dst;
}

// clock_gettime(CLOCK_MONOTONIC) for ARP and MAC aging (ch35).
#define CLOCK_MONOTONIC 1
struct timespec {
    long tv_sec;
    long tv_nsec;
};
static inline int clock_gettime(int clock, struct timespec* ts){
    uint64_t us = arch_uptime_us();
    ts->tv_sec  = (long)(us / 1000000);
    ts->tv_nsec = (long)(us % 1000000) * 1000;
    return 0;
}

#define AF_INET         2

static inline uint16_t htons(uint16_t v){ return __builtin_bswap16(v); }
static inline uint16_t ntohs(uint16_t v){ return __builtin_bswap16(v); }
static inline uint32_t htonl(uint32_t v){ return __builtin_bswap32(v); }
static inline uint32_t ntohl(uint32_t v){ return __builtin_bswap32(v); }

// 1 for a dotted quad (written in network order to dst), 0 otherwise.
int         inet_pton(int af, const char* src, void* dst);
const char* inet_ntop(int af, const void* src, void* dst, int size);

int strcmp(const char* a, const char* b);
int samtcpip_printf(const char* fmt, ...) __attribute__((format(__printf__, 1, 2)));
int samtcpip_logf(const char* fmt, ...) __attribute__((format(__printf__, 1, 2)));
int samtcpip_snprintf(char* out, size_t size, const char* fmt, ...) __attribute__((format(__printf__, 3, 4)));
int samtcpip_vsnprintf(char* out, size_t size, const char* fmt, va_list ap);

// The printf sink: NULL for the serial log, else `buf` (its size), from
// now until samtcpip_sink_end, which returns the bytes written (the
// output is cut at size - 1 and NUL-terminated).
void   samtcpip_sink_begin(char* buf, size_t size);
size_t samtcpip_sink_end(void);

#endif
