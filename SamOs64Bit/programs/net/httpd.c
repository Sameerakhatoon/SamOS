// Httpd [port] [count]: SamTcpIp's HTTP responder (Layer5/
// http.c, ch29) as a program on a stream socket. As upstream, any
// request gets HTTP/1.0 200 with "hello\n" and the connection closes;
// as an addition, GET /big gets HTTPD_BIG bytes of a known pattern
// (byte i is 'A' + i % 26, a newline every 64th), for the transfer
// and packet-loss tests, and GET /big<n> n KiB of it (the longer
// transfers). Port 80, for ever unless count.
//
// One process serves up to HTTPD_CONNS connections at once: the
// listener and every connection are non-blocking, and samos_poll
// waits for whichever of them can go on (a connection to accept, a
// request to read, room to send). A slow or silent client no longer
// holds up the others. `count` is the number of connections accepted
// before the listener closes; httpd exits when the last of them is
// done. Each connection's line is printed when its response is
// queued, as before.
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"
#include "samos.h"

#define HTTPD_PORT     80
#define HTTPD_REQ_MAX  2048
#define HTTPD_BIG      65536
#define HTTPD_CHUNK    4096
#define HTTPD_BIG_MAX_KIB  16384
#define HTTPD_CONNS    16

static const char hello_response[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 6\r\n"
    "Connection: close\r\n"
    "\r\n"
    "hello\n";

// One connection: reading its request, then sending the response from
// `out` (the headers, then the body a chunk at a time).
struct conn {
    long  s;
    struct samos_sockaddr peer;
    int   sending;
    long  req_len;
    char  req[HTTPD_REQ_MAX + 1];
    long  big;                      // the body's size for /big, else 0
    long  body_off;                 // body bytes put in `out` so far
    long  sent;                     // bytes queued
    int   out_len, out_pos;
    char  out[HTTPD_CHUNK];
};

// "GET /big " is HTTPD_BIG bytes, "GET /big<n> " n KiB of the same
// pattern (up to HTTPD_BIG_MAX_KIB), for the longer transfers; else 0.
static long httpd_big_size(const char* req){
    if(strncmp(req, "GET /big", 8) != 0){
        return 0;
    }
    if(req[8] == ' '){
        return HTTPD_BIG;
    }
    long kib = 0;
    const char* p = req + 8;
    while(*p >= '0' && *p <= '9' && kib <= HTTPD_BIG_MAX_KIB){
        kib = kib * 10 + (*p++ - '0');
    }
    return *p == ' ' && kib > 0 && kib <= HTTPD_BIG_MAX_KIB ? kib * 1024 : 0;
}

// The request is in: the headers go into `out`.
static void httpd_start_response(struct conn* c){
    c->req[c->req_len] = 0;
    c->big = httpd_big_size(c->req);
    if(c->big){
        c->out_len = snprintf(c->out, HTTPD_CHUNK,
                              "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n",
                              c->big);
    } else {
        c->out_len = (int)sizeof(hello_response) - 1;
        memcpy(c->out, (void*)hello_response, c->out_len);
    }
    c->out_pos = 0;
    c->sending = 1;
}

// `out` is sent: the next chunk of the body, or 0 at its end.
static int httpd_refill(struct conn* c){
    long len = c->big - c->body_off < HTTPD_CHUNK ? c->big - c->body_off : HTTPD_CHUNK;
    for(long i = 0; i < len; i++){
        long k = c->body_off + i;
        c->out[i] = k % 64 == 63 ? '\n' : (char)('A' + k % 26);
    }
    c->body_off += len;
    c->out_len = (int)len;
    c->out_pos = 0;
    return len > 0;
}

// The line, as the one-at-a-time httpd printed it: the body's size
// for /big, the whole response's for the rest, or the error.
static void httpd_done(struct conn* c, long res){
    char ip[16];
    samos_inet_ntop(c->peer.ip, ip);
    long sent = res < 0 ? res : c->big ? c->big : c->sent;
    if(c->big == HTTPD_BIG){
        printf("httpd: GET /big from %s:%u, 200, %ld bytes\n", ip, c->peer.port, sent);
    } else if(c->big){
        printf("httpd: GET /big%ld from %s:%u, 200, %ld bytes\n", c->big / 1024, ip, c->peer.port, sent);
    } else {
        printf("httpd: request from %s:%u, 200, %ld bytes\n", ip, c->peer.port, sent);
    }
    samos_sclose(c->s);
    free(c);
}

// The connection can go on: read or send what it can without waiting.
// 1 while it has more to do, 0 once it is done (and freed).
static int httpd_step(struct conn* c){
    if(!c->sending){
        // Up to the end of the headers (upstream answered the first
        // segment, whatever it held); the peer's FIN or an error ends
        // the request too, as before.
        long n = samos_recv(c->s, c->req + c->req_len, HTTPD_REQ_MAX - c->req_len);
        if(n == -SAMOS_EAGAIN){
            return 1;
        }
        if(n > 0){
            c->req_len += n;
            c->req[c->req_len] = 0;
        }
        if(n > 0 && c->req_len < HTTPD_REQ_MAX && !strstr(c->req, "\r\n\r\n")){
            return 1;
        }
        httpd_start_response(c);
    }
    for(;;){
        if(c->out_pos == c->out_len && (!c->big || !httpd_refill(c))){
            httpd_done(c, 0);
            return 0;
        }
        long n = samos_send(c->s, c->out + c->out_pos, c->out_len - c->out_pos);
        if(n == -SAMOS_EAGAIN || n == 0){
            return 1;
        }
        if(n < 0){
            httpd_done(c, n);
            return 0;
        }
        c->out_pos += (int)n;
        c->sent    += n;
    }
}

int main(int argc, char** argv){
    int port = argc > 1 ? atoi(argv[1]) : HTTPD_PORT;
    int count = argc > 2 ? atoi(argv[2]) : 0;
    long s = samos_socket(SAMOS_SOCK_STREAM);
    long res = s < 0 ? s : samos_bind(s, (unsigned)port);
    res = res < 0 ? res : samos_listen(s, 4);
    res = res < 0 ? res : samos_nonblock(s, 1);
    if(res < 0){
        printf("httpd: cannot listen on port %d (%ld)\n", port, res);
        return 1;
    }
    printf("httpd: listening on port %d\n", port);
    struct conn* conns[HTTPD_CONNS];
    struct samos_pollfd fds[HTTPD_CONNS + 1];
    int nconns = 0, accepted = 0, listening = 1;
    while(listening || nconns){
        // The listener while more may come and there is room, then
        // every connection for what it waits for.
        fds[0].s      = listening && nconns < HTTPD_CONNS ? (int)s : -1;
        fds[0].events = SAMOS_POLLIN;
        for(int i = 0; i < nconns; i++){
            fds[i + 1].s      = (int)conns[i]->s;
            fds[i + 1].events = conns[i]->sending ? SAMOS_POLLOUT : SAMOS_POLLIN;
        }
        long n = samos_poll(fds, nconns + 1, -1);
        if(n < 0){
            printf("httpd: poll %ld\n", n);
            break;
        }
        // Backwards, so the last connection moved into a finished
        // one's place has had its turn already.
        for(int i = nconns - 1; i >= 0; i--){
            if(fds[i + 1].revents && !httpd_step(conns[i])){
                conns[i] = conns[--nconns];
            }
        }
        while(fds[0].revents && listening && nconns < HTTPD_CONNS){
            struct samos_sockaddr peer;
            long c = samos_accept(s, &peer, 0);
            if(c == -SAMOS_EAGAIN){
                break;
            }
            if(c < 0){
                printf("httpd: accept %ld\n", c);
                listening = 0;
                break;
            }
            struct conn* k = malloc(sizeof(struct conn));
            if(!k || samos_nonblock(c, 1) < 0){
                printf("httpd: no memory for a connection\n");
                samos_sclose(c);
                free(k);
                listening = 0;
                break;
            }
            memset(k, 0, sizeof(struct conn));
            k->s    = c;
            k->peer = peer;
            conns[nconns++] = k;
            if(count && ++accepted == count){
                listening = 0;
            }
            // A request may be in already.
            if(!httpd_step(k)){
                nconns--;
            }
        }
    }
    samos_sclose(s);
    return 0;
}
