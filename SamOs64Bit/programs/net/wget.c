// Wget a.b.c.d port path [count]: HTTP/1.0 GET over a stream
// socket. Prints the status line, the body's length and its FNV-1a
// hash, and the body itself when it is short.
//
// With a count above 1, that many GETs one after another, each on
// a new connection, and one line at the end: how many were whole (status
// 200, no connection error, the same length and hash as the first),
// and the first's length and hash. A fetch that is not whole prints its
// own line.
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"
#include "samos.h"

#define WGET_CHUNK      2048
#define WGET_HEAD_MAX   1024
#define WGET_SHOW_MAX   256

struct fetch {
    char head[WGET_HEAD_MAX + 1];
    char show[WGET_SHOW_MAX + 1];
    long head_len, body, res;
    unsigned int hash;
    bool connected;
};

static struct fetch f;

// One GET; f.res is 0 when the response ended with the peer's FIN.
static void wget_once(unsigned int ip, unsigned port, const char* host, const char* path){
    char buf[WGET_CHUNK];
    memset(&f, 0, sizeof(f));
    f.hash = 2166136261u;
    long s = samos_socket(SAMOS_SOCK_STREAM);
    long res = s < 0 ? s : samos_connect(s, ip, port);
    if(res < 0){
        f.res = res;
        if(s >= 0){
            samos_sclose(s);
        }
        return;
    }
    f.connected = true;
    char req[256];
    int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    res = samos_send_all(s, req, n);

    long shown = 0;
    bool in_body = false;
    while(res >= 0){
        long got = samos_recv(s, buf, sizeof(buf));
        if(got <= 0){
            res = got;
            break;
        }
        for(long i = 0; i < got; i++){
            char c = buf[i];
            if(!in_body){
                if(f.head_len < WGET_HEAD_MAX){
                    f.head[f.head_len++] = c;
                    f.head[f.head_len] = 0;
                }
                in_body = f.head_len >= 4 && strncmp(f.head + f.head_len - 4, "\r\n\r\n", 4) == 0;
                continue;
            }
            f.hash = (f.hash ^ (unsigned char)c) * 16777619u;
            if(shown < WGET_SHOW_MAX){
                f.show[shown++] = c;
            }
            f.body++;
        }
    }
    samos_sclose(s);
    f.res = res;
    char* eol = strstr(f.head, "\r\n");
    if(eol){
        *eol = 0;
    }
    f.show[shown] = 0;
}

int main(int argc, char** argv){
    unsigned int ip;
    if(argc < 4 || samos_inet_pton(argv[1], &ip) < 0){
        printf("usage: wget a.b.c.d port path [count]\n");
        return 2;
    }
    unsigned port = (unsigned)atoi(argv[2]);
    int count = argc > 4 ? atoi(argv[4]) : 1;
    if(count <= 1){
        wget_once(ip, port, argv[1], argv[3]);
        if(!f.connected){
            printf("wget: connect %s:%s failed (%ld)\n", argv[1], argv[2], f.res);
            return 1;
        }
        printf("wget: %s\n", f.head_len ? f.head : "(no response)");
        printf("wget: body %ld bytes, fnv1a %x%s\n", f.body, f.hash, f.res < 0 ? ", connection error" : "");
        if(f.body <= WGET_SHOW_MAX){
            print(f.show);
        }
        return f.head_len && f.res == 0 ? 0 : 1;
    }
    long body = -1;
    unsigned int hash = 0;
    int whole = 0;
    for(int k = 0; k < count; k++){
        wget_once(ip, port, argv[1], argv[3]);
        if(body < 0 && f.res == 0){
            body = f.body;
            hash = f.hash;
        }
        bool ok = f.res == 0 && strncmp(f.head, "HTTP/1.0 200 ", 13) == 0 && f.body == body && f.hash == hash;
        if(ok){
            whole++;
        } else {
            printf("wget: fetch %d: %s, body %ld bytes, fnv1a %x (%ld)\n", k + 1,
                   f.head_len ? f.head : "(no response)", f.body, f.hash, f.res);
        }
    }
    printf("wget: %d of %d fetches whole, body %ld bytes, fnv1a %x\n", whole, count, body, hash);
    return whole == count ? 0 : 1;
}
