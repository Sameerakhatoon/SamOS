// Wget a.b.c.d port path: HTTP/1.0 GET over a stream socket.
// Prints the status line, the body's length and its FNV-1a hash, and
// the body itself when it is short.
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "samos.h"

#define WGET_CHUNK      2048
#define WGET_HEAD_MAX   1024
#define WGET_SHOW_MAX   256

int main(int argc, char** argv){
    unsigned int ip;
    if(argc < 4 || samos_inet_pton(argv[1], &ip) < 0){
        printf("usage: wget a.b.c.d port path\n");
        return 2;
    }
    long s = samos_socket(SAMOS_SOCK_STREAM);
    long res = s < 0 ? s : samos_connect(s, ip, (unsigned)atoi(argv[2]));
    if(res < 0){
        printf("wget: connect %s:%s failed (%ld)\n", argv[1], argv[2], res);
        return 1;
    }
    char req[256];
    int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", argv[3], argv[1]);
    res = samos_send_all(s, req, n);

    char head[WGET_HEAD_MAX + 1];
    char show[WGET_SHOW_MAX + 1];
    char buf[WGET_CHUNK];
    long head_len = 0, body = 0, shown = 0;
    bool in_body = false;
    unsigned int hash = 2166136261u;
    while(res >= 0){
        long got = samos_recv(s, buf, sizeof(buf));
        if(got <= 0){
            res = got;
            break;
        }
        for(long i = 0; i < got; i++){
            char c = buf[i];
            if(!in_body){
                if(head_len < WGET_HEAD_MAX){
                    head[head_len++] = c;
                    head[head_len] = 0;
                }
                in_body = head_len >= 4 && strncmp(head + head_len - 4, "\r\n\r\n", 4) == 0;
                continue;
            }
            hash = (hash ^ (unsigned char)c) * 16777619u;
            if(shown < WGET_SHOW_MAX){
                show[shown++] = c;
            }
            body++;
        }
    }
    samos_sclose(s);
    char* eol = strstr(head, "\r\n");
    if(eol){
        *eol = 0;
    }
    show[shown] = 0;
    printf("wget: %s\n", head_len ? head : "(no response)");
    printf("wget: body %ld bytes, fnv1a %x%s\n", body, hash, res < 0 ? ", connection error" : "");
    if(body <= WGET_SHOW_MAX){
        print(show);
    }
    return head_len && res == 0 ? 0 : 1;
}
