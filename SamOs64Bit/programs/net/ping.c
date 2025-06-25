// Ping a.b.c.d [count]: ICMP echo requests through the
// kernel's net_ping (SamTcpIp's ping.c sends them and reports the
// replies), one a second.
#include "stdio.h"
#include "stdlib.h"
#include "samos.h"

int main(int argc, char** argv){
    unsigned int ip;
    if(argc < 2 || samos_inet_pton(argv[1], &ip) < 0){
        printf("usage: ping a.b.c.d [count]\n");
        return 2;
    }
    int count = argc > 2 ? atoi(argv[2]) : 4;
    int received = 0;
    for(int seq = 1; seq <= count; seq++){
        long us = samos_net_ping(ip, 2000);
        if(us >= 0){
            printf("ping: reply from %s seq=%d time=%ld.%03ld ms\n", argv[1], seq, us / 1000, us % 1000);
            received++;
        } else {
            printf("ping: no reply from %s seq=%d\n", argv[1], seq);
        }
        if(seq < count){
            samos_sleep_ms(1000);
        }
    }
    printf("ping: %s %d sent, %d received\n", argv[1], count, received);
    return received ? 0 : 1;
}
