// UDP echo (RFC 862), SamTcpIp's udp_echo_handler as a program
// on a datagram socket.
//
//   udpecho -s [port] [count]   echo every datagram back to its sender
//                               (port 7, for ever unless count)
//   udpecho a.b.c.d port text   send `text`, wait 2 s for it to come back
//                               (three tries)
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"
#include "samos.h"

#define UDPECHO_PORT  7
#define UDPECHO_MAX   1400
#define UDPECHO_TRIES 3
#define UDPECHO_ETIMEOUT 12     // the kernel's ETIMEOUT (src/status.h)

static int udpecho_serve(int port, int count){
    char buf[UDPECHO_MAX];
    long s = samos_socket(SAMOS_SOCK_DGRAM);
    long res = s < 0 ? s : samos_bind(s, (unsigned)port);
    if(res < 0){
        printf("udpecho: cannot listen on port %d (%ld)\n", port, res);
        return 1;
    }
    printf("udpecho: listening on port %d\n", port);
    for(int n = 0; !count || n < count; n++){
        struct samos_sockaddr from;
        long len = samos_recvfrom(s, buf, sizeof(buf), &from, 0);
        if(len < 0){
            printf("udpecho: recvfrom %ld\n", len);
            break;
        }
        char ip[16];
        samos_inet_ntop(from.ip, ip);
        samos_sendto(s, buf, len, from.ip, from.port);
        printf("udpecho: %ld bytes from %s:%u, echoed\n", len, ip, from.port);
    }
    samos_sclose(s);
    return 0;
}

int main(int argc, char** argv){
    if(argc >= 2 && strcmp(argv[1], "-s") == 0){
        return udpecho_serve(argc > 2 ? atoi(argv[2]) : UDPECHO_PORT, argc > 3 ? atoi(argv[3]) : 0);
    }
    unsigned int ip;
    if(argc < 4 || samos_inet_pton(argv[1], &ip) < 0){
        printf("usage: udpecho -s [port] [count] | udpecho a.b.c.d port text\n");
        return 2;
    }
    long s = samos_socket(SAMOS_SOCK_DGRAM);
    long len = strlen(argv[3]);
    char buf[UDPECHO_MAX];
    struct samos_sockaddr from;
    long got = s;
    // UDP may lose it: up to UDPECHO_TRIES sends, 2 s each.
    for(int try = 0; try < UDPECHO_TRIES && s >= 0; try++){
        long res = samos_sendto(s, argv[3], len, ip, (unsigned)atoi(argv[2]));
        got = res < 0 ? res : samos_recvfrom(s, buf, sizeof(buf) - 1, &from, 2000);
        if(got != -UDPECHO_ETIMEOUT){
            break;
        }
    }
    if(got == len && memcmp(buf, argv[3], len) == 0){
        buf[got] = 0;
        printf("udpecho: reply '%s' from %s ok\n", buf, argv[1]);
    } else {
        printf("udpecho: no matching reply (%ld)\n", got);
    }
    samos_sclose(s);
    return got == len ? 0 : 1;
}
