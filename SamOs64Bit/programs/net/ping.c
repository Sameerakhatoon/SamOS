// Ping [-t ttl] a.b.c.d [count]: ICMP echo requests through
// the kernel's net_ping (SamTcpIp's ping.c sends them and reports the
// replies), one a second. Each reply's TTL as it arrived, and
// -t sets the requests' TTL: a router that drops one on it answers
// with Time Exceeded, which is printed with the router's address
// (one hop of a traceroute).
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "samos.h"

int main(int argc, char** argv){
    unsigned int ip;
    int ttl = 0, a = 1;
    if(argc > 2 && strcmp(argv[1], "-t") == 0){
        ttl = atoi(argv[2]);
        a = 3;
    }
    if(argc <= a || samos_inet_pton(argv[a], &ip) < 0 || ttl < 0 || ttl > 255){
        printf("usage: ping [-t ttl] a.b.c.d [count]\n");
        return 2;
    }
    const char* dst = argv[a];
    int count = argc > a + 1 ? atoi(argv[a + 1]) : 4;
    int received = 0;
    for(int seq = 1; seq <= count; seq++){
        struct samos_ping_result r;
        long us = samos_net_ping_ex(ip, 2000, (unsigned int)ttl, &r);
        if(us >= 0 && r.type == SAMOS_PING_ECHO_REPLY){
            printf("ping: reply from %s seq=%d time=%ld.%03ld ms ttl=%d\n", dst, seq, us / 1000, us % 1000,
                   r.ttl);
            received++;
        } else if(us >= 0){
            char from[16];
            samos_inet_ntop(r.from, from);
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
