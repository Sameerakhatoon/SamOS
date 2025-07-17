// The kernel's side of SamTcpIp (net.h, netdev.h): the Node,
// its configuration, the frames and the tick into the stack, the
// waits of the network syscalls, ping and the tables as text.

#include "net.h"
#include "netdev.h"
#include "socket.h"
#include "samtcpip/port.h"
#include "samtcpip/log.h"
#include "samtcpip/graph.h"
#include "samtcpip/net.h"
#include "samtcpip/comm.h"
#include "samtcpip/Layer2/layer2.h"
#include "samtcpip/Layer3/layer3.h"
#include "samtcpip/Layer4/udp.h"
#include "samtcpip/Layer4/tcp.h"
#include "samtcpip/Layer5/ping.h"
#include "task/task.h"
#include "boot/cmdline.h"
#include "io/serial.h"
#include "status.h"

#define NET_PINGS           16
#define NET_WAITERS         64

static Graph* topo;
static Node*  node;

Node* net_node(void){
    return node;
}

// ---- Waits ---------------------------------------------------------------

static struct net_waiter {
    struct task* task;
    void*        obj;
    uint64_t     set;               // net_block_set's bits
    uint64_t     deadline_ms;
    bool         timed_out;
} waiters[NET_WAITERS];

// A syscall's kernel copy, freed when it blocks.
static struct net_scratch {
    struct task* task;
    void*        ptr;
} scratches[NET_WAITERS];

void net_scratch(void* p){
    net_lock();
    for(int i = 0; i < NET_WAITERS; i++){
        if(!scratches[i].task){
            scratches[i].task = task_current();
            scratches[i].ptr  = p;
            break;
        }
    }
    net_unlock();
}

void net_scratch_done(void* p){
    net_lock();
    for(int i = 0; i < NET_WAITERS; i++){
        if(scratches[i].task == task_current() && scratches[i].ptr == p){
            scratches[i].task = NULL;
            scratches[i].ptr  = NULL;
        }
    }
    net_unlock();
}

// Under the net lock: free what `t` registered (it blocks or is gone).
static void scratch_free(struct task* t){
    for(int i = 0; i < NET_WAITERS; i++){
        if(scratches[i].task == t){
            kfree(scratches[i].ptr);
            scratches[i].task = NULL;
            scratches[i].ptr  = NULL;
        }
    }
}

static struct net_waiter* waiter_of(struct task* t){
    for(int i = 0; i < NET_WAITERS; i++){
        if(waiters[i].task == t){
            return &waiters[i];
        }
    }
    return NULL;
}

static void waiter_wake(struct net_waiter* w){
    if(w->task->state == TASK_BLOCKED && w->task->block_reason == TASK_BLOCK_NET){
        task_wake(w->task);
    }
}

__attribute__((noreturn))
static void net_block_on(void* obj, uint64_t set, uint64_t deadline_ms){
    struct task* me = task_current();
    struct net_waiter* w = waiter_of(me);
    if(!w){
        w = waiter_of(NULL);
    }
    if(w){
        w->task        = me;
        w->obj         = obj;
        w->set         = set;
        w->deadline_ms = deadline_ms;
        w->timed_out   = false;
    }
    // The syscall runs again from the start: its kernel copy goes.
    scratch_free(me);
    // Without a free slot nothing wakes it but the tick: it polls.
    task_block(me, TASK_BLOCK_NET);
    net_unlock();
    task_next();
    __builtin_unreachable();
}

void net_block(void* obj, uint64_t deadline_ms){
    net_block_on(obj, 0, deadline_ms);
}

// The waiter itself is the object, which nobody else wakes.
void net_block_set(uint64_t set, uint64_t deadline_ms){
    struct net_waiter* w = waiter_of(task_current());
    if(!w){
        w = waiter_of(NULL);
    }
    net_block_on(w, set, deadline_ms);
}

bool net_resume_ex(uint64_t* deadline_ms){
    struct net_waiter* w = waiter_of(task_current());
    bool timed_out = w && w->timed_out;
    if(w){
        if(deadline_ms){
            *deadline_ms = w->deadline_ms;
        }
        w->task = NULL;
        w->obj  = NULL;
        w->set  = 0;
    }
    return timed_out;
}

bool net_resume(void){
    return net_resume_ex(NULL);
}

void net_wake(void* obj){
    for(int i = 0; i < NET_WAITERS; i++){
        if(waiters[i].task && waiters[i].obj == obj){
            waiter_wake(&waiters[i]);
        }
    }
}

void net_wake_set(int bit){
    for(int i = 0; i < NET_WAITERS; i++){
        if(waiters[i].task && bit >= 0 && bit < 64 && ((waiters[i].set >> bit) & 1)){
            waiter_wake(&waiters[i]);
        }
    }
}

// ---- Configuration and the stack ------------------------------------------

// "A.B.C.D[/N]" at `v` (ending at a space, ',' or '@') into ip16
// and *mask (left alone without "/N"); the length taken, or -1.
static int net_addr(const char* v, char* ip, int* mask){
    int n = 0;
    while(v[n] && v[n] != ' ' && v[n] != '/' && v[n] != ',' && v[n] != '@'){
        if(n == 15){
            return -1;
        }
        ip[n] = v[n];
        n++;
    }
    ip[n] = 0;
    uint32_t a;
    if(inet_pton(AF_INET, ip, &a) != 1){
        return -1;
    }
    if(v[n] == '/'){
        int m = 0, digits = 0;
        for(n++; v[n] >= '0' && v[n] <= '9' && digits < 3; n++, digits++){
            m = m * 10 + (v[n] - '0');
        }
        if(!digits || m > 32 || !mask){
            return -1;
        }
        *mask = m;
    }
    return n;
}

static void net_log_bad(const char* what, const char* v){
    char word[40];
    int n = 0;
    while(v[n] && v[n] != ' ' && n < 39){
        word[n] = v[n];
        n++;
    }
    word[n] = 0;
    serial_write("net: bad ");
    serial_write(what);
    serial_write(" '");
    serial_write(word);
    serial_write("'\n");
}

// samos.route=DEST/N@GW[,DEST/N@GW...]: each out of the interface
// whose subnet holds its gateway.
static void net_static_routes(void){
    const char* v = boot_cmdline_value("samos.route");
    while(v && *v && *v != ' '){
        char dst[16], gw[16];
        int mask = 32;
        int n = net_addr(v, dst, &mask);
        int g = n > 0 && v[n] == '@' ? net_addr(v + n + 1, gw, NULL) : -1;
        if(g < 0){
            net_log_bad("samos.route", v);
            return;
        }
        Interface* oif = node_get_matching_subnet_interface(node, gw);
        serial_write("net: route ");
        serial_write(dst);
        serial_write("/");
        serial_write(itoa(mask));
        serial_write(" via ");
        serial_write(gw);
        if(oif){
            rt_table_add_route(NODE_RT_TABLE(node), dst, (char)mask, gw, oif->if_name);
            serial_write(" on ");
            serial_write(oif->if_name);
            serial_write("\n");
        } else {
            serial_write(": no interface on the gateway's subnet\n");
        }
        v += n + 1 + g;
        if(*v == ','){
            v++;
        }
    }
}

// eth0's address and the default route. Every NIC's
// address (samos.ip.ethN), the default route out of the interface on
// the gateway's subnet, samos.gw=none, the static routes and
// samos.ip_forward (net.h).
static void net_configure(Interface* eth0){
    Interface* ifs[MAX_INTF_PER_NODE];
    int nifs = 0;
    for(int i = 0; i < MAX_INTF_PER_NODE && node->intf[i]; i++){
        Interface* intf = node->intf[i];
        if(intf->dev->loopback){
            continue;
        }
        char key[32] = "samos.ip.";
        strcpy(key + 9, intf->if_name);
        const char* v = boot_cmdline_value(key);
        if(!v && intf == eth0){
            v = boot_cmdline_value("samos.ip");
        }
        if(!v && intf != eth0){
            continue;               // only eth0 has a default address
        }
        char ip[16] = "10.0.2.15";
        int mask = 24;
        if(v && net_addr(v, ip, &mask) < 0){
            net_log_bad(key, v);
            continue;
        }
        node_set_intf_ip_address(node, intf->if_name, ip, (char)mask);
        ifs[nifs++] = intf;
    }

    char gw[16] = "10.0.2.2";
    Interface* gw_if = NULL;
    const char* v = boot_cmdline_value("samos.gw");
    if(v && strncmp(v, "none", 4) == 0 && (v[4] == ' ' || !v[4])){
        gw[0] = 0;
    } else if(v && net_addr(v, gw, NULL) < 0){
        net_log_bad("samos.gw", v);
        gw[0] = 0;
    }
    if(gw[0] && nifs){
        gw_if = node_get_matching_subnet_interface(node, gw);
        if(!gw_if){
            gw_if = ifs[0];
        }
        rt_table_add_route(NODE_RT_TABLE(node), "0.0.0.0", 0, gw, gw_if->if_name);
    }

    for(int i = 0; i < nifs; i++){
        serial_write("net: ");
        serial_write(ifs[i]->if_name);
        serial_write(" ");
        serial_write(IF_IP(ifs[i]));
        serial_write("/");
        serial_write(itoa(ifs[i]->intf_nw_props.mask));
        if(ifs[i] == gw_if){
            serial_write(", gateway ");
            serial_write(gw);
        }
        serial_write("\n");
    }
    net_static_routes();
    node->node_nw_prop.ip_forward = boot_cmdline_has("samos.ip_forward");
    if(node->node_nw_prop.ip_forward){
        serial_write("net: IP forwarding on\n");
    }
    for(int i = 0; i < nifs; i++){
        send_gratuitous_arp(node, ifs[i]);
    }
}

// samos.tcp_time_wait_ms=N and samos.tcp_orphan_ms=N (decimal).
static void net_tcp_option(const char* key, unsigned long* out){
    const char* v = boot_cmdline_value(key);
    if(!v || *v < '0' || *v > '9'){
        return;
    }
    unsigned long n = 0;
    while(*v >= '0' && *v <= '9'){
        n = n * 10 + (unsigned long)(*v++ - '0');
    }
    *out = n;
    serial_write("net: ");
    serial_write(key);
    serial_write(" ");
    serial_write(itoa((int)n));
    serial_write("\n");
}

void net_stack_init(void){
    if(boot_cmdline_has("samos.net_debug")){
        g_log_level = LOG_LEVEL_DEBUG;
    }
    net_tcp_option("samos.tcp_time_wait_ms", &tcp_time_wait_ms);
    net_tcp_option("samos.tcp_orphan_ms", &tcp_orphan_ms);
    topo = create_new_graph("samos");
    node = topo ? create_graph_node(topo, "samos") : NULL;
    if(!node){
        return;
    }
    node_set_device_type(node, L3_ROUTER);
    node->node_nw_prop.ip_forward = false;  // A host unless net_configure says
    Interface* eth = NULL;
    for(int i = 0; i < netdev_count(); i++){
        struct netdev* d = netdev_get(i);
        Interface* intf = node_attach_netdev(node, d);
        if(intf && d->loopback){
            node_set_intf_ip_address(node, intf->if_name, "127.0.0.1", 8);
        } else if(intf && !eth){
            eth = intf;
        }
    }
    ping_register();
    udp_register();
    tcp_register();
    if(eth){
        net_configure(eth);
    }
}

void net_stack_input(struct netdev* d, uint8_t* frame, uint32_t len){
    if(node && d->intf){
        pkt_receive(node, d->intf, (char*)frame, len);
    }
}

void net_stack_timer(void){
    uint64_t now = net_now_ms();
    for(int i = 0; i < NET_WAITERS; i++){
        struct net_waiter* w = &waiters[i];
        if(w->task && w->deadline_ms && !w->timed_out && now >= w->deadline_ms){
            w->timed_out = true;
            waiter_wake(w);
        }
    }
    if(node){
        tcp_timer();
    }
}

// ---- Ping ------------------------------------------------------------------

// One outstanding echo request per task.
enum { PING_FREE, PING_WAITING, PING_REPLIED };
static struct net_ping {
    struct task* task;
    uint32_t     dst;
    uint16_t     seq;
    int          state;
    uint64_t     sent_us;
    uint64_t     deadline_ms;
    uint32_t     rtt_us;
    uint32_t     from;              // Who answered, its TTL, the ICMP type
    uint8_t      ttl;
    uint8_t      type;
} pings[NET_PINGS];
static uint16_t ping_seq;

// ping.c's echo-reply branch reports here as well as
// printing "Ping reply". And its Time Exceeded branch, with the
// request's destination in `dst` and the router in `from`.
void net_ping_reply(uint32_t dst, uint16_t seq, uint32_t from, uint8_t ttl, uint8_t type){
    for(int i = 0; i < NET_PINGS; i++){
        struct net_ping* p = &pings[i];
        if(p->state == PING_WAITING && p->seq == seq && p->dst == dst){
            p->rtt_us = (uint32_t)(arch_uptime_us() - p->sent_us);
            p->from   = from;
            p->ttl    = ttl;
            p->type   = type;
            p->state  = PING_REPLIED;
            net_wake(p);
        }
    }
}

long net_ping(uint32_t dst, uint32_t timeout_ms){
    struct net_ping_result r;
    long res = net_ping_ex(dst, timeout_ms, 0, &r);
    return res >= 0 && r.type != NET_PING_ECHO_REPLY ? -ETIMEOUT : res;
}

long net_ping_ex(uint32_t dst, uint32_t timeout_ms, uint8_t ttl, struct net_ping_result* out){
    struct task* me = task_current();
    net_lock();
    bool timed_out = net_resume();
    struct net_ping* p = NULL;
    for(int i = 0; i < NET_PINGS && !p; i++){
        if(pings[i].state != PING_FREE && pings[i].task == me){
            p = &pings[i];
        }
    }
    if(!p){
        for(int i = 0; i < NET_PINGS && !p; i++){
            if(pings[i].state == PING_FREE){
                p = &pings[i];
            }
        }
        if(!node || !p){
            net_unlock();
            return -ENOSPC;
        }
        char ip[16];
        uint32_t net = htonl(dst);
        inet_ntop(AF_INET, &net, ip, 16);
        p->task        = me;
        p->dst         = dst;
        p->seq         = ++ping_seq;
        p->state       = PING_WAITING;
        p->sent_us     = arch_uptime_us();
        p->deadline_ms = net_now_ms() + timeout_ms;
        ping_send_request_ttl(node, ip, (short)p->seq, ttl);
        net_poll();
    }
    if(p->state == PING_WAITING && !timed_out){
        net_block(p, p->deadline_ms);
    }
    long res = p->state == PING_REPLIED ? (long)p->rtt_us : -ETIMEOUT;
    if(res >= 0 && out){
        out->from   = p->from;
        out->rtt_us = p->rtt_us;
        out->ttl    = p->ttl;
        out->type   = p->type;
        out->pad[0] = out->pad[1] = 0;
    }
    p->state = PING_FREE;
    p->task  = NULL;
    net_unlock();
    return res;
}

// ---- Tables as text ----------------------------------------------------------

long net_info(int what, char* buf, size_t size){
    if(!node){
        return -ENOENT;
    }
    net_lock();
    samtcpip_sink_begin(buf, size);
    switch(what){
    case NET_INFO_IFCONFIG:
        for(int i = 0; i < MAX_INTF_PER_NODE && node->intf[i]; i++){
            struct netdev* d = node->intf[i]->dev;
            dump_intf_props(node->intf[i]);
            printf("\t rx %lu packets %lu bytes %lu dropped, tx %lu packets %lu bytes %lu dropped\n",
                   (unsigned long)d->rx_packets, (unsigned long)d->rx_bytes, (unsigned long)d->rx_dropped,
                   (unsigned long)d->tx_packets, (unsigned long)d->tx_bytes, (unsigned long)d->tx_dropped);
        }
        break;
    case NET_INFO_ARP:
        dump_arp_table(NODE_ARP_TABLE(node));
        break;
    case NET_INFO_ROUTE:
        dump_rt_table(NODE_RT_TABLE(node));
        // The forwarding counters (samtcpip/net.h).
        printf("IP forwarding %s: %lu forwarded, %lu TTL expired, %lu refused, %lu no route\n",
               node->node_nw_prop.ip_forward ? "on" : "off", node->node_nw_prop.fwd_packets,
               node->node_nw_prop.fwd_ttl_expired, node->node_nw_prop.fwd_refused,
               node->node_nw_prop.fwd_no_route);
        break;
    case NET_INFO_TCP:
        tcp_dump(node);
        printf("TCP retransmission timeouts: %lu\n", tcp_rto_count);
        // The TCP repair's counters.
        printf("TCP fast retransmits: %lu\n", tcp_fast_rtx_count);
        printf("TCP out-of-order segments kept: %lu\n", tcp_ooo_count);
        printf("TCP orphans timed out: %lu\n", tcp_orphan_count);
        printf("sockets in use: %d of %d\n", socket_count(), NET_SOCKETS);
        break;
    }
    long n = (long)samtcpip_sink_end();
    net_unlock();
    return n;
}

void net_task_gone(struct task* t){
    net_lock();
    for(int i = 0; i < NET_PINGS; i++){
        if(pings[i].state != PING_FREE && pings[i].task == t){
            pings[i].state = PING_FREE;
            pings[i].task  = NULL;
        }
    }
    struct net_waiter* w = waiter_of(t);
    if(w){
        w->task = NULL;
        w->obj  = NULL;
        w->set  = 0;
    }
    scratch_free(t);
    socket_process_gone(t->process);
    net_unlock();
}
