// The kernel's side of SamTcpIp (net.h, netdev.h): the Node,
// its configuration, the frames and the tick into the stack, and the
// network syscalls' work.

#include "net.h"
#include "netdev.h"
#include "samtcpip/port.h"
#include "samtcpip/log.h"
#include "samtcpip/graph.h"
#include "samtcpip/net.h"
#include "samtcpip/comm.h"
#include "samtcpip/Layer2/layer2.h"
#include "samtcpip/Layer3/layer3.h"
#include "samtcpip/Layer5/ping.h"
#include "task/task.h"
#include "boot/cmdline.h"
#include "io/serial.h"
#include "status.h"

#define NET_PINGS           16

static Graph* topo;
static Node*  node;

// One outstanding echo request per task (net_ping).
enum { PING_FREE, PING_WAITING, PING_REPLIED, PING_TIMED_OUT };
static struct net_ping {
    struct task* task;
    uint32_t     dst;
    uint16_t     seq;
    int          state;
    uint64_t     sent_us;
    uint64_t     deadline_ms;
    uint32_t     rtt_us;
} pings[NET_PINGS];
static uint16_t ping_seq;

// "A.B.C.D" (up to a space or `stop`) into out16; false if too long.
static bool net_word(const char* s, char stop, char* out16){
    int n = 0;
    while(s[n] && s[n] != ' ' && s[n] != stop){
        if(n == 15){
            return false;
        }
        out16[n] = s[n];
        n++;
    }
    out16[n] = 0;
    return true;
}

static void net_configure(Interface* eth){
    char ip[16] = "10.0.2.15", gw[16] = "10.0.2.2";
    int mask = 24;
    const char* v = boot_cmdline_value("samos.ip");
    if(v && net_word(v, '/', ip) && v[strlen(ip)] == '/'){
        mask = 0;
        for(const char* m = v + strlen(ip) + 1; *m >= '0' && *m <= '9'; m++){
            mask = mask * 10 + (*m - '0');
        }
    }
    v = boot_cmdline_value("samos.gw");
    if(v){
        net_word(v, ' ', gw);
    }
    node_set_intf_ip_address(node, eth->if_name, ip, (char)mask);
    rt_table_add_route(NODE_RT_TABLE(node), "0.0.0.0", 0, gw, eth->if_name);
    serial_write("net: ");
    serial_write(eth->if_name);
    serial_write(" ");
    serial_write(ip);
    serial_write("/");
    serial_write(itoa(mask));
    serial_write(", gateway ");
    serial_write(gw);
    serial_write("\n");
}

void net_stack_init(void){
    if(boot_cmdline_has("samos.net_debug")){
        g_log_level = LOG_LEVEL_DEBUG;
    }
    topo = create_new_graph("samos");
    node = topo ? create_graph_node(topo, "samos") : NULL;
    if(!node){
        return;
    }
    node_set_device_type(node, L3_ROUTER);
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
    if(eth){
        net_configure(eth);
        send_gratuitous_arp(node, eth);
    }
}

void net_stack_input(struct netdev* d, uint8_t* frame, uint32_t len){
    if(node && d->intf){
        pkt_receive(node, d->intf, (char*)frame, len);
    }
}

static void ping_wake(struct net_ping* p, int state){
    p->state = state;
    if(p->task->state == TASK_BLOCKED && p->task->block_reason == TASK_BLOCK_NET){
        task_wake(p->task);
    }
}

void net_stack_timer(void){
    uint64_t now = net_now_ms();
    for(int i = 0; i < NET_PINGS; i++){
        if(pings[i].state == PING_WAITING && now >= pings[i].deadline_ms){
            ping_wake(&pings[i], PING_TIMED_OUT);
        }
    }
}

// ping.c's echo-reply branch reports here as well as
// printing "Ping reply".
void net_ping_reply(uint32_t src, uint16_t seq){
    for(int i = 0; i < NET_PINGS; i++){
        struct net_ping* p = &pings[i];
        if(p->state == PING_WAITING && p->seq == seq && p->dst == src){
            p->rtt_us = (uint32_t)(arch_uptime_us() - p->sent_us);
            ping_wake(p, PING_REPLIED);
        }
    }
}

long net_ping(uint32_t dst, uint32_t timeout_ms){
    struct task* me = task_current();
    net_lock();
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
        ping_send_request(node, ip, (short)p->seq);
        net_poll();
    }
    if(p->state == PING_WAITING){
        task_block(me, TASK_BLOCK_NET);
        net_unlock();
        task_next();
    }
    long res = p->state == PING_REPLIED ? (long)p->rtt_us : -ETIMEOUT;
    p->state = PING_FREE;
    p->task  = NULL;
    net_unlock();
    return res;
}

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
    net_unlock();
}
