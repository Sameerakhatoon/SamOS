// Httpd [port] [count]: SamTcpIp's HTTP responder (Layer5/
// http.c, ch29) as a program on a stream socket. As upstream, any
// request gets HTTP/1.0 200 with "hello\n" and the connection closes;
// as an addition, GET /big gets HTTPD_BIG bytes of a known pattern
// (byte i is 'A' + i % 26, a newline every 64th), for the transfer
// and packet-loss tests. Port 80, for ever unless count.
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "samos.h"

#define HTTPD_PORT     80
#define HTTPD_REQ_MAX  2048
#define HTTPD_BIG      65536
#define HTTPD_CHUNK    4096

static const char hello_response[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 6\r\n"
    "Connection: close\r\n"
    "\r\n"
    "hello\n";

static long httpd_big(long c){
    char head[128];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                     HTTPD_BIG);
    long res = samos_send_all(c, head, n);
    char chunk[HTTPD_CHUNK];
    for(int off = 0; off < HTTPD_BIG && res >= 0; off += HTTPD_CHUNK){
        for(int i = 0; i < HTTPD_CHUNK; i++){
            int k = off + i;
            chunk[i] = k % 64 == 63 ? '\n' : (char)('A' + k % 26);
        }
        res = samos_send_all(c, chunk, HTTPD_CHUNK);
    }
    return res < 0 ? res : HTTPD_BIG;
}

int main(int argc, char** argv){
    int port = argc > 1 ? atoi(argv[1]) : HTTPD_PORT;
    int count = argc > 2 ? atoi(argv[2]) : 0;
    long s = samos_socket(SAMOS_SOCK_STREAM);
    long res = s < 0 ? s : samos_bind(s, (unsigned)port);
    res = res < 0 ? res : samos_listen(s, 4);
    if(res < 0){
        printf("httpd: cannot listen on port %d (%ld)\n", port, res);
        return 1;
    }
    printf("httpd: listening on port %d\n", port);
    char* req = malloc(HTTPD_REQ_MAX + 1);
    for(int served = 0; req && (!count || served < count); served++){
        struct samos_sockaddr peer;
        long c = samos_accept(s, &peer, 0);
        if(c < 0){
            printf("httpd: accept %ld\n", c);
            break;
        }
        // Up to the end of the headers (upstream answered the first
        // segment, whatever it held).
        long len = 0;
        while(len < HTTPD_REQ_MAX){
            long n = samos_recv(c, req + len, HTTPD_REQ_MAX - len);
            if(n <= 0){
                break;
            }
            len += n;
            req[len] = 0;
            if(strstr(req, "\r\n\r\n")){
                break;
            }
        }
        req[len] = 0;
        char ip[16];
        samos_inet_ntop(peer.ip, ip);
        bool big = strncmp(req, "GET /big ", 9) == 0;
        long sent = big ? httpd_big(c) : samos_send_all(c, hello_response, sizeof(hello_response) - 1);
        printf("httpd: %s from %s:%u, 200, %ld bytes\n", big ? "GET /big" : "request", ip, peer.port, sent);
        samos_sclose(c);
    }
    samos_sclose(s);
    return 0;
}
