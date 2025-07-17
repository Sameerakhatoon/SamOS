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
    bool            nonblock;           // socket_nonblock
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

// Whoever waits on the socket itself, and every poll that has
// it in its set (its index is the bit).
static void sock_wake(struct sock* s){
    net_wake(s);
    net_wake_set((int)(s - socks));
}

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
    sock_wake(s);
}

unsigned int tcp_app_window(Tcb* t){
    struct sock* s = t->app;
    return s ? SOCK_RXBUF - s->rx_len : SOCK_RXBUF;
}

bool tcp_app_accept_room(Tcb* listener){
    struct sock* l = listener->app;
    return !l || l->accq_len < l->backlog;
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
        sock_wake(l);
        return;
    }
    if(!s){
        // A passive TCB that closed before its handshake
        // ended (a RST, or its SYN-ACKs ran out) has no socket and never
        // will: let it go, or it stays in the TCB list for ever.
        if(ev == TCP_EV_CLOSED && !t->orphan){
            tcp_release(t);
        }
        return;
    }
    if(ev == TCP_EV_ESTABLISHED){
        s->deadline = 0;                    // A non-blocking connect is done
    } else if(ev == TCP_EV_FIN){
        s->eof = true;
    } else if(ev == TCP_EV_CLOSED && !s->eof && !s->err){
        s->err = t->state == TCP_CLOSED && s->deadline ? -ECONNREFUSED : -ECONNRESET;
    }
    sock_wake(s);
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
            sock_wake(s);
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
        } else if(s->nonblock){
            res = -EINPROGRESS;
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
    } else if(s->nonblock){
        res = -EAGAIN;
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
        if(!res && s->nonblock){
            res = -EAGAIN;
        } else if(!res){
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
        } else if(s->nonblock){
            res = -EAGAIN;
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
    } else if(s->nonblock){
        res = -EAGAIN;
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
    sock_wake(s);
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

// ---- poll, non-blocking, names ----------------------------------------

static bool sock_connected(struct sock* s){
    return s->tcb && (s->tcb->state == TCP_ESTABLISHED || s->tcb->state == TCP_CLOSE_WAIT);
}

// What holds for `s` now (socket.h, the SOCK_POLL* list).
static unsigned int sock_ready(struct sock* s){
    if(s->type == SOCK_DGRAM){
        return SOCK_POLLOUT | (s->dq_len ? SOCK_POLLIN : 0);
    }
    if(s->listening){
        return s->accq_len ? SOCK_POLLIN : 0;
    }
    unsigned int r = 0;
    if(s->rx_len || s->eof || s->err){
        r |= SOCK_POLLIN;
    }
    if(s->err){
        r |= SOCK_POLLERR | SOCK_POLLHUP;
    } else if(!s->tcb || s->tcb->state == TCP_CLOSED){
        // Gone both ways (the peer's FIN alone is POLLIN: recv says 0).
        r |= SOCK_POLLHUP;
    }
    if(!s->err && sock_connected(s) && tcp_send_room(s->tcb) > 0){
        r |= SOCK_POLLOUT;
    }
    return r;
}

long socket_poll(struct process* p, struct sock_pollfd* fds, int n, int32_t timeout_ms){
    if(n < 0 || n > SOCK_POLL_MAX){
        return -EINVARG;
    }
    net_lock();
    uint64_t deadline = timeout_ms > 0 ? net_now_ms() + (uint32_t)timeout_ms : 0;
    bool timed_out = net_resume_ex(&deadline);
    // Frames a NIC without an interrupt holds, and the loopback's.
    net_poll();
    uint64_t set = 0;
    long ready = 0;
    for(int i = 0; i < n; i++){
        fds[i].revents = 0;
        if(fds[i].sock < 0){
            continue;
        }
        struct sock* s = sock_get(p, fds[i].sock, 0);
        if(!s){
            fds[i].revents = SOCK_POLLNVAL;
        } else {
            unsigned int want = fds[i].events | SOCK_POLLERR | SOCK_POLLHUP;
            fds[i].revents = (uint16_t)(sock_ready(s) & want);
            set |= 1ull << fds[i].sock;
        }
        ready += fds[i].revents != 0;
    }
    if(deadline && net_now_ms() >= deadline){
        timed_out = true;                   // before the tick says so
    }
    if(!ready && timeout_ms && !timed_out){
        net_block_set(set, deadline);
    }
    net_unlock();
    return ready;
}

long socket_nonblock(struct process* p, int sd, int mode){
    net_lock();
    struct sock* s = sock_get(p, sd, 0);
    long res;
    if(!s){
        res = -EBADF;
    } else if(mode < -1 || mode > 1){
        res = -EINVARG;
    } else {
        if(mode >= 0){
            s->nonblock = mode == 1;
        }
        res = s->nonblock;
    }
    net_unlock();
    return res;
}

long socket_name(struct process* p, int sd, int peer, struct sock_addr* out){
    net_lock();
    struct sock* s = sock_get(p, sd, 0);
    long res = 0;
    if(!s){
        res = -EBADF;
    } else if(peer){
        if(s->listening || !s->rip || (s->type == SOCK_STREAM && !s->tcb)){
            res = -ENOTCONN;
        } else {
            out->ip   = s->rip;
            out->port = s->rport;
        }
    } else {
        out->port = s->lport;
        out->ip   = s->tcb && !s->listening ? s->tcb->local_ip
                  : s->type == SOCK_DGRAM && s->rip && net_node() ? node_pick_src_ip(net_node(), s->rip) : 0;
    }
    net_unlock();
    return res;
}

int socket_count(void){
    int n = 0;
    for(int i = 0; i < NET_SOCKETS; i++){
        n += socks[i].type != 0;
    }
    return n;
}

void socket_process_gone(struct process* p){
    for(int i = 0; i < NET_SOCKETS; i++){
        if(socks[i].type && socks[i].owner == p && !socks[i].queued){
            sock_free(&socks[i]);
        }
    }
}
