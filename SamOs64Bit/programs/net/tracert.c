// tracert [-m maxhops] a.b.c.d: the routers on the way to a
// host, by echo requests with TTL 1, 2, 3, ... (net_ping_ex): each
// router where the TTL runs out answers with Time Exceeded, the host
// itself with an echo reply. Up to three tries per hop, 2 s each.
//
//   tracert: to 10.2.0.2, 8 hops max
//   tracert: 1 10.1.0.1 0.812 ms
//   tracert: 2 10.2.0.2 1.433 ms reached
//   tracert: 10.2.0.2 reached in 2 hops
//
// A hop that does not answer prints "tracert: N *"; a Destination
// Unreachable ends it ("tracert: N R unreachable code=C"). The exit
// status is 0 when the host answered.
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "samos.h"

#define TRACERT_TRIES    3
#define TRACERT_WAIT_MS  2000

int main(int argc, char** argv){
    unsigned int ip;
    int max = 8, a = 1;
    if(argc > 2 && strcmp(argv[1], "-m") == 0){
        max = atoi(argv[2]);
        a = 3;
    }
    if(argc <= a || samos_inet_pton(argv[a], &ip) < 0 || max < 1 || max > 255){
        printf("usage: tracert [-m maxhops] a.b.c.d\n");
        return 2;
    }
    const char* dst = argv[a];
    printf("tracert: to %s, %d hops max\n", dst, max);
    for(int hop = 1; hop <= max; hop++){
        struct samos_ping_result r;
        long us = -1;
        for(int t = 0; t < TRACERT_TRIES && us < 0; t++){
            us = samos_net_ping_ex(ip, TRACERT_WAIT_MS, (unsigned int)hop, &r);
        }
        if(us < 0){
            printf("tracert: %d *\n", hop);
            continue;
        }
        char from[16];
        samos_inet_ntop(r.from, from);
        if(r.type == SAMOS_PING_ECHO_REPLY){
            printf("tracert: %d %s %ld.%03ld ms reached\n", hop, from, us / 1000, us % 1000);
            printf("tracert: %s reached in %d hops\n", dst, hop);
            return 0;
        }
        if(r.type == SAMOS_PING_DEST_UNREACH){
            printf("tracert: %d %s unreachable code=%d\n", hop, from, r.code);
            return 1;
        }
        printf("tracert: %d %s %ld.%03ld ms\n", hop, from, us / 1000, us % 1000);
    }
    printf("tracert: %s not reached in %d hops\n", dst, max);
    return 1;
}
