// Sockets over SamTcpIp's UDP and TCP. See socket.h.

#include "socket.h"
#include "net.h"
#include "netdev.h"
#include "samtcpip/port.h"
#include "samtcpip/graph.h"
#include "samtcpip/Layer3/layer3.h"
#include "samtcpip/Layer4/udp.h"
#include "samtcpip/Layer4/tcp.h"
#include "status.h"

#define SOCK_EPHEMERAL_FIRST  49152

struct dgram {
    uint32_t ip;
    uint16_t port;
    uint16_t len;
    uint8_t  data[SOCK_DGRAM_MAX];
};

struct sock {
    int             type;               // 0: free
    struct process* owner;
    uint16_t        lport;
    uint32_t        rip;                // UDP: the connected peer
    uint16_t        rport;
    Tcb*            tcb;
    int             err;                // a negative status, once
    bool            eof;                // TCP: the peer's FIN
    bool            listening;
    bool            queued;             // on a listener's accept queue
    uint64_t        deadline;           // connect's
    uint8_t*        rx;                 // TCP receive ring
    uint32_t        rx_head, rx_len;
    struct sock*    accq[SOCK_BACKLOG_MAX];
    int             accq_len, backlog;
    struct dgram*   dq[SOCK_DGRAMS];
    int             dq_len;
};

static struct sock socks[NET_SOCKETS];
static uint16_t next_ephemeral = SOCK_EPHEMERAL_FIRST;

static struct sock* sock_get(struct process* p, int s, int type){
    if(s < 0 || s >= NET_SOCKETS || !socks[s].type || socks[s].owner != p){
        return NULL;
    }
    return !type || socks[s].type == type ? &socks[s] : NULL;
}

static struct sock* sock_new(struct process* p, int type){
    for(int i = 0; i < NET_SOCKETS; i++){
        if(!socks[i].type){
            struct sock* s = &socks[i];
            memset(s, 0, sizeof(*s));
            if(type == SOCK_STREAM && !(s->rx = kmalloc(SOCK_RXBUF))){
                return NULL;
            }
            s->type  = type;
            s->owner = p;
            return s;
        }
    }
    return NULL;
}

static bool port_taken(int type, uint16_t port){
    for(int i = 0; i < NET_SOCKETS; i++){
        if(socks[i].type == type && socks[i].lport == port && (type == SOCK_DGRAM || socks[i].listening)){
            return true;
        }
    }
    return false;
}

// ---- TCP: SamTcpIp's application hooks (tcp.h) -----------------------------

// ch29's on_data, on every TCB a socket owns or listens with: the bytes
// go to the receive ring (tcp.c takes no more than tcp_app_window).
static void sock_tcp_data(Tcb* t, const char* data, size_t len){
    struct sock* s = t->app;
    if(!s){
        return;
    }
    for(size_t i = 0; i < len && s->rx_len < SOCK_RXBUF; i++){
        s->rx[(s->rx_head + s->rx_len++) % SOCK_RXBUF] = (uint8_t)data[i];
    }
    net_wake(s);
}

unsigned int tcp_app_window(Tcb* t){
    struct sock* s = t->app;
    return s ? SOCK_RXBUF - s->rx_len : SOCK_RXBUF;
}

void tcp_event(Tcb* t, int ev){
    struct sock* s = t->app;
    if(ev == TCP_EV_ESTABLISHED && !s && t->listener){
        // A passive child: a socket for it, on its listener's queue.
        struct sock* l = t->listener->app;
        struct sock* c = l && l->accq_len < l->backlog ? sock_new(l->owner, SOCK_STREAM) : NULL;
        if(!c){
            tcp_close_tcb(t);
            tcp_release(t);
            return;
        }
        c->tcb   = t;
        c->lport = t->local_port;
        c->rip   = t->remote_ip;
        c->rport = t->remote_port;
        t->app   = c;
        c->queued = true;
        l->accq[l->accq_len++] = c;
        net_wake(l);
        return;
    }
    if(!s){
        return;
    }
    if(ev == TCP_EV_FIN){
        s->eof = true;
    } else if(ev == TCP_EV_CLOSED && !s->eof && !s->err){
        s->err = t->state == TCP_CLOSED && s->deadline ? -ECONNREFUSED : -ECONNRESET;
    }
    net_wake(s);
}

// ---- UDP -------------------------------------------------------------------

static void sock_udp_rx(Node* node, Interface* iif, struct IpHdr* ip, UdpHdr* udp,
                        char* payload, size_t len){
    uint16_t port = ntohs(udp->dst_port);
    for(int i = 0; i < NET_SOCKETS; i++){
        struct sock* s = &socks[i];
        if(s->type != SOCK_DGRAM || s->lport != port){
            continue;
        }
        struct dgram* d = s->dq_len < SOCK_DGRAMS && len <= SOCK_DGRAM_MAX ? kmalloc(sizeof(*d)) : NULL;
        if(d){
            d->ip   = ip->src_ip;
            d->port = ntohs(udp->src_port);
            d->len  = (uint16_t)len;
            memcpy(d->data, payload, len);
            s->dq[s->dq_len++] = d;
            net_wake(s);
        }
        return;
    }
}

static long sock_udp_bind(struct sock* s, uint16_t port){
    if(port_taken(SOCK_DGRAM, port)){
        return -EADDRINUSE;
    }
    if(!udp_bind(net_node(), port, sock_udp_rx)){
        return -ENOMEM;
    }
    s->lport = port;
    return 0;
}

static uint16_t ephemeral_port(int type){
    for(int tries = 0; tries < 16384; tries++){
        uint16_t p = next_ephemeral++;
        if(next_ephemeral < SOCK_EPHEMERAL_FIRST){
            next_ephemeral = SOCK_EPHEMERAL_FIRST;
        }
        if(!port_taken(type, p)){
            return p;
        }
    }
    return 0;
}

// ---- The calls ---------------------------------------------------------------

long socket_open(struct process* p, int type){
    if(type != SOCK_STREAM && type != SOCK_DGRAM){
        return -EINVARG;
    }
    net_lock();
    struct sock* s = net_node() ? sock_new(p, type) : NULL;
    long res = s ? (long)(s - socks) : -ENOSPC;
    net_unlock();
    return res;
}

long socket_bind(struct process* p, int sd, uint32_t port){
    net_lock();
    struct sock* s = sock_get(p, sd, 0);
    long res = -EBADF;
    if(s && (!port || port > 65535 || s->lport)){
        res = -EINVARG;
    } else if(s && s->type == SOCK_DGRAM){
        res = sock_udp_bind(s, (uint16_t)port);
    } else if(s){
        res = port_taken(SOCK_STREAM, (uint16_t)port) ? -EADDRINUSE : 0;
        if(!res){
            s->lport = (uint16_t)port;
        }
    }
    net_unlock();
    return res;
}

long socket_listen(struct process* p, int sd, int backlog){
    net_lock();
    struct sock* s = sock_get(p, sd, SOCK_STREAM);
    long res = 0;
    if(!s){
        res = -EBADF;
    } else if(!s->lport || s->tcb){
        res = -EINVARG;
    } else if(port_taken(SOCK_STREAM, s->lport)){
        res = -EADDRINUSE;
    } else if(!(s->tcb = tcp_listen_tcb(net_node(), s->lport, sock_tcp_data))){
        res = -EADDRINUSE;
    } else {
        s->tcb->app  = s;
        s->listening = true;
        s->backlog   = backlog < 1 ? 1 : backlog > SOCK_BACKLOG_MAX ? SOCK_BACKLOG_MAX : backlog;
    }
    net_unlock();
    return res;
}

long socket_connect(struct process* p, int sd, uint32_t ip, uint32_t port){
    net_lock();
    bool timed_out = net_resume();
    struct sock* s = sock_get(p, sd, 0);
    long res = 0;
    if(!s || s->listening){
        res = s ? -EINVARG : -EBADF;
    } else if(s->type == SOCK_DGRAM){
        s->rip   = ip;
        s->rport = (uint16_t)port;
    } else if(!s->tcb){
        s->tcb = tcp_open(net_node(), ip, (uint16_t)port);
        if(!s->tcb){
            res = -ENOMEM;
        } else {
            s->tcb->app     = s;
            s->tcb->on_data = sock_tcp_data;
            s->lport        = s->tcb->local_port;
            s->rip          = ip;
            s->rport        = (uint16_t)port;
            s->deadline     = net_now_ms() + SOCK_CONNECT_MS;
            net_poll();
        }
    }
    if(!res && s->type == SOCK_STREAM){
        if(s->tcb->state == TCP_ESTABLISHED || s->tcb->state == TCP_CLOSE_WAIT){
            s->deadline = 0;
        } else if(s->err){
            res = s->err;
        } else if(timed_out){
            res = -ECONNREFUSED;
        } else {
            net_block(s, s->deadline);
        }
    }
    net_unlock();
    return res;
}

long socket_accept(struct process* p, int sd, struct sock_addr* peer, uint32_t timeout_ms){
    net_lock();
    bool timed_out = net_resume();
    struct sock* s = sock_get(p, sd, SOCK_STREAM);
    long res;
    if(!s || !s->listening){
        res = s ? -EINVARG : -EBADF;
    } else if(s->accq_len){
        struct sock* c = s->accq[0];
        memmove(s->accq, s->accq + 1, sizeof(s->accq[0]) * (size_t)--s->accq_len);
        c->owner  = p;
        c->queued = false;
        peer->ip   = c->rip;
        peer->port = c->rport;
        res = c - socks;
    } else if(timed_out){
        res = -ETIMEOUT;
    } else {
        net_block(s, timeout_ms ? net_now_ms() + timeout_ms : 0);
    }
    net_unlock();
    return res;
}

long socket_sendto(struct process* p, int sd, const void* buf, size_t len, uint32_t ip, uint32_t port){
    net_lock();
    net_resume();
    struct sock* s = sock_get(p, sd, 0);
    long res = 0;
    if(!s){
        res = -EBADF;
    } else if(s->type == SOCK_DGRAM){
        if(!ip){
            ip   = s->rip;
            port = s->rport;
        }
        uint16_t lport = s->lport ? s->lport : ephemeral_port(SOCK_DGRAM);
        if(!ip || !port || port > 65535 || len > SOCK_DGRAM_MAX){
            res = -EINVARG;
        } else if(!s->lport && (res = sock_udp_bind(s, lport)) < 0){
        } else {
            udp_send_pkt(net_node(), ip, s->lport, (uint16_t)port, buf, len);
            res = (long)len;
        }
    } else if(!s->tcb || s->listening){
        res = -ENOTCONN;
    } else if(s->err){
        res = s->err;
    } else if(s->tcb->state != TCP_ESTABLISHED && s->tcb->state != TCP_CLOSE_WAIT){
        res = -ENOTCONN;
    } else if(len){
        res = (long)tcp_queue_data(s->tcb, buf, len);
        if(!res){
            net_block(s, 0);                // the send buffer is full
        }
    }
    net_unlock();
    return res;
}

long socket_recvfrom(struct process* p, int sd, void* buf, size_t len, struct sock_addr* from, uint32_t timeout_ms){
    net_lock();
    bool timed_out = net_resume();
    struct sock* s = sock_get(p, sd, 0);
    long res = 0;
    if(!s){
        res = -EBADF;
    } else if(s->type == SOCK_DGRAM){
        if(s->dq_len){
            struct dgram* d = s->dq[0];
            memmove(s->dq, s->dq + 1, sizeof(s->dq[0]) * (size_t)--s->dq_len);
            res = (long)(d->len < len ? d->len : len);
            memcpy(buf, d->data, (size_t)res);
            from->ip   = d->ip;
            from->port = d->port;
            kfree(d);
        } else if(!s->lport){
            res = -ENOTCONN;
        } else if(timed_out){
            res = -ETIMEOUT;
        } else {
            net_block(s, timeout_ms ? net_now_ms() + timeout_ms : 0);
        }
    } else if(!s->tcb || s->listening){
        res = -ENOTCONN;
    } else if(s->rx_len){
        unsigned int before = tcp_app_window(s->tcb);
        size_t n = len < s->rx_len ? len : s->rx_len;
        for(size_t i = 0; i < n; i++){
            ((uint8_t*)buf)[i] = s->rx[(s->rx_head + i) % SOCK_RXBUF];
        }
        s->rx_head = (uint32_t)((s->rx_head + n) % SOCK_RXBUF);
        s->rx_len -= (uint32_t)n;
        from->ip   = s->rip;
        from->port = s->rport;
        tcp_window_update(s->tcb, before);
        res = (long)n;
    } else if(s->eof){
        res = 0;
    } else if(s->err){
        res = s->err;
    } else if(timed_out){
        res = -ETIMEOUT;
    } else {
        net_block(s, timeout_ms ? net_now_ms() + timeout_ms : 0);
    }
    net_unlock();
    return res;
}

static void sock_free(struct sock* s){
    if(s->type == SOCK_DGRAM){
        if(s->lport){
            udp_unbind(net_node(), s->lport);
        }
        for(int i = 0; i < s->dq_len; i++){
            kfree(s->dq[i]);
        }
    } else {
        for(int i = 0; i < s->accq_len; i++){
            sock_free(s->accq[i]);
        }
        if(s->tcb){
            tcp_close_tcb(s->tcb);
            tcp_release(s->tcb);
        }
        kfree(s->rx);
    }
    net_wake(s);
    memset(s, 0, sizeof(*s));
}

long socket_close(struct process* p, int sd){
    net_lock();
    struct sock* s = sock_get(p, sd, 0);
    if(s){
        sock_free(s);
    }
    net_unlock();
    return s ? 0 : -EBADF;
}

void socket_process_gone(struct process* p){
    for(int i = 0; i < NET_SOCKETS; i++){
        if(socks[i].type && socks[i].owner == p && !socks[i].queued){
            sock_free(&socks[i]);
        }
    }
}
