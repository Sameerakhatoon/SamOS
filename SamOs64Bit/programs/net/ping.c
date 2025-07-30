// Ping [-t ttl] a.b.c.d [count]: ICMP echo requests through
// the kernel's net_ping (SamTcpIp's ping.c sends them and reports the
// replies), one a second. Each reply's TTL as it arrived, and
// -t sets the requests' TTL: a router that drops one on it answers
// with Time Exceeded, which is printed with the router's address
// (one hop of a traceroute).
// Ping [-t ttl] [-s size] [-D] a.b.c.d [count]. -s: data bytes
// after the echo header (default 16, at most 65507; a larger datagram
// goes in fragments and comes back reassembled); each reply then also
// shows its size and whether the data came back as sent. -D: DF set,
// so a router whose next link is too small answers Fragmentation
// Needed ("ping: frag needed from R seq=N mtu=M"). A router with no
// route answers Destination Unreachable ("ping: unreachable from R
// seq=N code=C").
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "samos.h"

static int usage(void){
    printf("usage: ping [-t ttl] [-s size] [-D] a.b.c.d [count]\n");
    return 2;
}

int main(int argc, char** argv){
    unsigned int ip;
    int ttl = 0, size = 0, df = 0, a = 1;
    while(a < argc && argv[a][0] == '-'){
        if(strcmp(argv[a], "-t") == 0 && a + 1 < argc){
            ttl = atoi(argv[a + 1]);
            a += 2;
        } else if(strcmp(argv[a], "-s") == 0 && a + 1 < argc){
            size = atoi(argv[a + 1]);
            a += 2;
        } else if(strcmp(argv[a], "-D") == 0){
            df = 1;
            a++;
        } else {
            return usage();
        }
    }
    if(argc <= a || samos_inet_pton(argv[a], &ip) < 0 || ttl < 0 || ttl > 255 || size < 0
       || size > SAMOS_PING_SIZE_MAX){
        return usage();
    }
    const char* dst = argv[a];
    int count = argc > a + 1 ? atoi(argv[a + 1]) : 4;
    int received = 0;
    unsigned int opts = (unsigned int)ttl | SAMOS_PING_SIZE(size) | (df ? SAMOS_PING_DF : 0);
    for(int seq = 1; seq <= count; seq++){
        struct samos_ping_result r;
        // 2 s, and 1 s more per 8000 bytes: a large one goes in pieces.
        long us = samos_net_ping_ex(ip, 2000 + (unsigned int)size / 8, opts, &r);
        char from[16];
        if(us >= 0){
            samos_inet_ntop(r.from, from);
        }
        if(us >= 0 && r.type == SAMOS_PING_ECHO_REPLY){
            if(size){
                printf("ping: reply from %s seq=%d time=%ld.%03ld ms ttl=%d size=%u %s\n", dst, seq, us / 1000,
                       us % 1000, r.ttl, r.bytes, r.data_ok && r.bytes == (unsigned int)size ? "data ok" : "data BAD");
            } else {
                printf("ping: reply from %s seq=%d time=%ld.%03ld ms ttl=%d\n", dst, seq, us / 1000, us % 1000,
                       r.ttl);
            }
            received++;
        } else if(us >= 0 && r.type == SAMOS_PING_DEST_UNREACH && r.code == 4){
            printf("ping: frag needed from %s seq=%d mtu=%d\n", from, seq, r.mtu);
        } else if(us >= 0 && r.type == SAMOS_PING_DEST_UNREACH){
            printf("ping: unreachable from %s seq=%d code=%d\n", from, seq, r.code);
        } else if(us >= 0){
            printf("ping: time exceeded from %s seq=%d time=%ld.%03ld ms\n", from, seq, us / 1000, us % 1000);
        } else {
            printf("ping: no reply from %s seq=%d\n", dst, seq);
        }
        if(seq < count){
            samos_sleep_ms(1000);
        }
    }
    printf("ping: %s %d sent, %d received\n", dst, count, received);
    return received ? 0 : 1;
}
