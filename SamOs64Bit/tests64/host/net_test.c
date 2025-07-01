// Host-side test of the network stack's protocol code
// (src/net/samtcpip: SamTcpIp's L2 to L4) built with the host's gcc and
// libc (port.h's SAMTCPIP_HOST_TEST branch). One Node with one
// fake NIC, as net/net.c sets it up in the kernel: frames the stack
// sends land in a queue here, frames the test builds go in through
// pkt_receive. The test plays 10.0.2.2 and checks every frame with its
// own checksum code, not the stack's.
//
//   checksums     RFC 1071 vectors, and every IP, ICMP, UDP and TCP
//                 checksum the stack writes
//   ARP           gratuitous ARP, a reply to a request, no reply for
//                 another address, a request on a miss, the cache
//   ICMP          echo reply byte for byte, a bad checksum ignored
//   IP            fragment reassembly in any order, an incomplete set
//                 delivers nothing, and fragmentation of a large send
//   TCP           the state machine both ways (handshake, data, close,
//                 RST, a closed port), the send buffer with cwnd and
//                 the peer's window, retransmission on the timer with
//                 the loss event, an ACK past a rewound snd_nxt,
//                 SYN retransmission and the abort
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "net/samtcpip/port.h"
#include "net/samtcpip/graph.h"
#include "net/samtcpip/net.h"
#include "net/samtcpip/utils.h"
#include "net/samtcpip/comm.h"
#include "net/samtcpip/Layer2/layer2.h"
#include "net/samtcpip/Layer3/layer3.h"
#include "net/samtcpip/Layer3/icmp.h"
#include "net/samtcpip/Layer4/udp.h"
#include "net/samtcpip/Layer4/tcp.h"
#include "net/samtcpip/Layer5/ping.h"
#undef printf

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if(!(cond)){ failures++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

// ---- what the kernel gives the stack ----------------------------------------
static int verbose;
static uint64_t now_ms = 1000;
uint64_t net_now_ms(void){ return now_ms; }

int samtcpip_printf(const char* fmt, ...){
    if(!verbose){
        return 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
int samtcpip_logf(const char* fmt, ...){
    if(!verbose){
        return 0;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

static int ping_replies;
static struct { unsigned int dst, from; unsigned short seq; unsigned char ttl, type; } ping_last;
void net_ping_reply(unsigned int dst_ip, unsigned short seq, unsigned int from_ip, unsigned char ttl,
                    unsigned char type){
    ping_replies++;
    ping_last.dst  = dst_ip;
    ping_last.seq  = seq;
    ping_last.from = from_ip;
    ping_last.ttl  = ttl;
    ping_last.type = type;
}

#define QMAX 256
static struct { uint8_t b[1514]; uint32_t len; } q[QMAX];
static int qhead, qlen;
int netdev_xmit(struct netdev* d, const void* frame, uint32_t len){
    if(qlen == QMAX || len > 1514){
        return -1;
    }
    int i = (qhead + qlen++) % QMAX;
    memcpy(q[i].b, frame, len);
    q[i].len = len;
    return 0;
}
static uint8_t* qpop(uint32_t* len){
    if(!qlen){
        return NULL;
    }
    int i = qhead;
    qhead = (qhead + 1) % QMAX;
    qlen--;
    *len = q[i].len;
    return q[i].b;
}
static void qclear(void){ qlen = 0; }

// The application side of TCP (net/socket.c in the kernel).
static unsigned int app_window = 16384;
static int ev_count[4];
static Tcb* ev_last;
static uint8_t app_rx[65536];
static size_t app_rx_len;
unsigned int tcp_app_window(Tcb* t){ return app_window; }
void tcp_event(Tcb* t, int ev){
    ev_count[ev]++;
    ev_last = t;
}
static void app_data(Tcb* t, const char* data, size_t len){
    memcpy(app_rx + app_rx_len, data, len);
    app_rx_len += len;
}

// ---- the test's own wire code -----------------------------------------------
static const uint8_t OUR_MAC[6]  = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t PEER_MAC[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };
static const uint8_t BCAST[6]    = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
#define OUR_IP   0x0a00020fu          // 10.0.2.15
#define PEER_IP  0x0a000202u          // 10.0.2.2

static Node* node;
static Interface* eth;
static struct netdev nic = { .name = "eth0", .mac = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 }, .mtu = 1500,
                             .irq = "test" };

static uint16_t get16(const uint8_t* p){ return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t get32(const uint8_t* p){ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t* p, uint32_t v){ put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }

// RFC 1071, written again here so the stack is checked against an
// independent sum: the folded one's complement sum of 16-bit words.
static uint32_t sum16(uint32_t sum, const uint8_t* p, size_t n){
    for(; n > 1; p += 2, n -= 2){
        sum += get16(p);
    }
    if(n){
        sum += (uint32_t)p[0] << 8;
    }
    return sum;
}
static uint16_t fold(uint32_t sum){
    while(sum >> 16){
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}
static uint16_t l4sum(uint32_t src, uint32_t dst, uint8_t proto, const uint8_t* seg, size_t n){
    uint8_t ph[12];
    put32(ph, src);
    put32(ph + 4, dst);
    ph[8] = 0;
    ph[9] = proto;
    put16(ph + 10, (uint16_t)n);
    return fold(sum16(sum16(0, ph, 12), seg, n));
}

static void deliver(uint8_t* f, size_t n){
    pkt_receive(node, eth, (char*)f, n);
}

static size_t eth_hdr(uint8_t* f, const uint8_t* dst, uint16_t type){
    memcpy(f, dst, 6);
    memcpy(f + 6, PEER_MAC, 6);
    put16(f + 12, type);
    return 14;
}

// An IPv4 packet from the peer to `dst` (through our MAC) with this
// TTL: `frag` is the flags and offset field, `n` the payload bytes.
static size_t ip_pkt_to(uint8_t* f, uint8_t proto, uint16_t id, uint16_t frag, uint32_t dst, uint8_t ttl,
                        const uint8_t* payload, size_t n){
    size_t o = eth_hdr(f, OUR_MAC, 0x0800);
    uint8_t* ip = f + o;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)(20 + n));
    put16(ip + 4, id);
    put16(ip + 6, frag);
    ip[8] = ttl;
    ip[9] = proto;
    put32(ip + 12, PEER_IP);
    put32(ip + 16, dst);
    put16(ip + 10, fold(sum16(0, ip, 20)));
    memcpy(ip + 20, payload, n);
    return o + 20 + n;
}
// An IPv4 packet from the peer to us.
static size_t ip_pkt(uint8_t* f, uint8_t proto, uint16_t id, uint16_t frag, const uint8_t* payload, size_t n){
    return ip_pkt_to(f, proto, id, frag, OUR_IP, 64, payload, n);
}

// A frame the stack sent: Ethernet, then (for IPv4) the header checked.
struct pkt {
    const uint8_t* f;
    uint32_t len;
    uint16_t type;
    const uint8_t* ip;
    uint8_t  proto;
    uint16_t frag;
    uint16_t id;
    const uint8_t* l4;
    size_t   l4len;
    bool     ip_ok;
};
static bool next_pkt(struct pkt* p){
    memset(p, 0, sizeof(*p));
    p->f = qpop(&p->len);
    if(!p->f){
        return false;
    }
    p->type = get16(p->f + 12);
    if(p->type == 0x0800 && p->len >= 34){
        p->ip    = p->f + 14;
        size_t hl = (size_t)(p->ip[0] & 15) * 4;
        size_t tl = get16(p->ip + 2);
        p->proto = p->ip[9];
        p->frag  = get16(p->ip + 6);
        p->id    = get16(p->ip + 4);
        p->l4    = p->ip + hl;
        p->l4len = tl - hl;
        p->ip_ok = (p->ip[0] >> 4) == 4 && fold(sum16(0, p->ip, hl)) == 0 && 14 + tl <= p->len;
    }
    return true;
}
// The next IPv4 frame of protocol `proto`, skipping ARP.
static bool next_ip(struct pkt* p, uint8_t proto){
    while(next_pkt(p)){
        if(p->type == 0x0800 && p->proto == proto){
            return true;
        }
    }
    return false;
}

// ---- TCP from the peer -------------------------------------------------------
#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

static void tcp_in(uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack, uint8_t flags, uint16_t win,
                   const void* data, size_t n, uint16_t mss){
    uint8_t seg[1600], f[1700];
    size_t hl = mss ? 24 : 20;
    memset(seg, 0, hl);
    put16(seg, sport);
    put16(seg + 2, dport);
    put32(seg + 4, seq);
    put32(seg + 8, ack);
    seg[12] = (uint8_t)((hl / 4) << 4);
    seg[13] = flags;
    put16(seg + 14, win);
    if(mss){
        seg[20] = 2;
        seg[21] = 4;
        put16(seg + 22, mss);
    }
    if(n){
        memcpy(seg + hl, data, n);
    }
    put16(seg + 16, l4sum(PEER_IP, OUR_IP, 6, seg, hl + n));
    deliver(f, ip_pkt(f, 6, 1, 0x4000, seg, hl + n));
}

struct tseg {
    uint16_t sport, dport, win, mss;
    uint32_t seq, ack;
    uint8_t  flags;
    const uint8_t* data;
    size_t   len;
    bool     csum_ok;
};
// The next TCP segment the stack sent, checked (IP and TCP checksums).
static bool tcp_out(struct tseg* s){
    struct pkt p;
    memset(s, 0, sizeof(*s));
    if(!next_ip(&p, 6)){
        return false;
    }
    const uint8_t* t = p.l4;
    size_t hl = (size_t)(t[12] >> 4) * 4;
    s->sport = get16(t);
    s->dport = get16(t + 2);
    s->seq   = get32(t + 4);
    s->ack   = get32(t + 8);
    s->flags = t[13];
    s->win   = get16(t + 14);
    s->data  = t + hl;
    s->len   = p.l4len - hl;
    for(size_t i = 20; i + 4 <= hl; i++){
        if(t[i] == 2 && t[i + 1] == 4){
            s->mss = get16(t + i + 2);
        }
    }
    s->csum_ok = p.ip_ok && get32(p.ip + 12) == OUR_IP && get32(p.ip + 16) == PEER_IP
              && l4sum(OUR_IP, PEER_IP, 6, t, p.l4len) == 0
              && memcmp(p.f, PEER_MAC, 6) == 0 && memcmp(p.f + 6, OUR_MAC, 6) == 0;
    return true;
}

// ---- the parts ---------------------------------------------------------------

static void test_checksums(void){
    // RFC 1071 section 3's example: the sum of 0001 f203 f4f5 f6f7 is
    // ddf2, so the checksum is its complement 220d.
    static const uint8_t v[] = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7 };
    CHECK(inet_checksum(v, sizeof(v)) == 0x220d, "inet_checksum(RFC 1071 example) = %04x", inet_checksum(v, 8));
    CHECK(inet_checksum(v, sizeof(v)) == fold(sum16(0, v, sizeof(v))), "inet_checksum agrees with the test's sum");
    // Odd length: the last byte is the high half of a zero-padded word.
    static const uint8_t odd[] = { 0x01, 0x02, 0x03 };
    CHECK(inet_checksum(odd, 3) == (uint16_t)~(0x0102 + 0x0300), "odd length = %04x", inet_checksum(odd, 3));
    // A carry folds back in: ffff + 0001 = 1 0000 -> 0001.
    static const uint8_t carry[] = { 0xff, 0xff, 0x00, 0x01 };
    CHECK(inet_checksum(carry, 4) == 0xfffe, "end-around carry = %04x", inet_checksum(carry, 4));
    // A buffer with its own checksum in it sums to zero.
    uint8_t hdr[20] = { 0x45, 0, 0, 0x54, 0x12, 0x34, 0x40, 0, 64, 1, 0, 0, 10, 0, 2, 15, 10, 0, 2, 2 };
    put16(hdr + 10, inet_checksum(hdr, 20));
    CHECK(inet_checksum(hdr, 20) == 0, "a header with its checksum sums to 0");
    CHECK(icmp_checksum(v, sizeof(v)) == 0x220d, "icmp_checksum = %04x", icmp_checksum(v, 8));
}

static void test_arp(void){
    struct pkt p;
    // net/net.c's bring-up sends a gratuitous ARP (ch20).
    send_gratuitous_arp(node, eth);
    CHECK(next_pkt(&p) && p.type == 0x0806, "gratuitous ARP sent");
    if(p.f && p.type == 0x0806){
        const uint8_t* a = p.f + 14;
        CHECK(memcmp(p.f, BCAST, 6) == 0, "gratuitous ARP is a broadcast");
        CHECK(get16(a) == 1 && get16(a + 2) == 0x0800 && a[4] == 6 && a[5] == 4, "gratuitous ARP: Ethernet/IPv4");
        CHECK(get32(a + 14) == OUR_IP && get32(a + 24) == OUR_IP, "gratuitous ARP: sender = target = 10.0.2.15");
        CHECK(memcmp(a + 8, OUR_MAC, 6) == 0, "gratuitous ARP: our MAC");
    }
    // A request for us from the peer: a reply to the peer's MAC.
    uint8_t f[64] = {0};
    size_t o = eth_hdr(f, BCAST, 0x0806);
    uint8_t* a = f + o;
    put16(a, 1); put16(a + 2, 0x0800); a[4] = 6; a[5] = 4; put16(a + 6, 1);
    memcpy(a + 8, PEER_MAC, 6); put32(a + 14, PEER_IP); put32(a + 24, OUR_IP);
    deliver(f, o + 28);
    CHECK(next_pkt(&p) && p.type == 0x0806, "ARP request answered");
    if(p.f && p.type == 0x0806){
        const uint8_t* r = p.f + 14;
        CHECK(memcmp(p.f, PEER_MAC, 6) == 0 && memcmp(p.f + 6, OUR_MAC, 6) == 0, "ARP reply unicast to the asker");
        CHECK(get16(r + 6) == 2, "ARP reply opcode 2, got %u", get16(r + 6));
        CHECK(memcmp(r + 8, OUR_MAC, 6) == 0 && get32(r + 14) == OUR_IP, "ARP reply: 10.0.2.15 is-at our MAC");
        CHECK(memcmp(r + 18, PEER_MAC, 6) == 0 && get32(r + 24) == PEER_IP, "ARP reply: target is the asker");
    }
    ArpEntry* e = arp_table_lookup(NODE_ARP_TABLE(node), "10.0.2.2");
    CHECK(e && memcmp(e->mac_addr.mac, PEER_MAC, 6) == 0, "the asker is in the ARP cache");
    // A request for another address: no answer.
    put32(a + 24, 0x0a000263);
    deliver(f, o + 28);
    CHECK(qlen == 0, "no ARP reply for 10.0.2.99 (%d frames)", qlen);
    qclear();
    // A miss: sending to 10.0.2.77 asks for it.
    ping_send_request(node, "10.0.2.77", 1);
    bool asked = false;
    while(next_pkt(&p)){
        if(p.type == 0x0806 && memcmp(p.f, BCAST, 6) == 0 && get16(p.f + 20) == 1 && get32(p.f + 38) == 0x0a00024d){
            asked = true;
        }
    }
    CHECK(asked, "ARP request broadcast for 10.0.2.77 on a miss");
    // Its reply fills the cache.
    static const uint8_t other[6] = { 0x02, 0, 0, 0, 0, 0x77 };
    o = eth_hdr(f, OUR_MAC, 0x0806);
    memcpy(f + 6, other, 6);
    put16(a, 1); put16(a + 2, 0x0800); a[4] = 6; a[5] = 4; put16(a + 6, 2);
    memcpy(a + 8, other, 6); put32(a + 14, 0x0a00024d); memcpy(a + 18, OUR_MAC, 6); put32(a + 24, OUR_IP);
    deliver(f, o + 28);
    e = arp_table_lookup(NODE_ARP_TABLE(node), "10.0.2.77");
    CHECK(e && memcmp(e->mac_addr.mac, other, 6) == 0, "ARP reply cached 10.0.2.77");
    qclear();
}

static void test_icmp(void){
    uint8_t icmp[64], f[200];
    struct pkt p;
    memset(icmp, 0, sizeof(icmp));
    icmp[0] = 8;
    put16(icmp + 4, 0x1234);
    put16(icmp + 6, 7);
    for(int i = 8; i < 64; i++){
        icmp[i] = (uint8_t)i;
    }
    put16(icmp + 2, fold(sum16(0, icmp, 64)));
    deliver(f, ip_pkt(f, 1, 42, 0x4000, icmp, 64));
    CHECK(next_ip(&p, 1), "echo request answered");
    if(p.ip){
        CHECK(p.ip_ok, "echo reply: IP header checksum");
        CHECK(get32(p.ip + 12) == OUR_IP && get32(p.ip + 16) == PEER_IP, "echo reply: addresses swapped");
        CHECK(p.l4len == 64 && p.l4[0] == 0 && p.l4[1] == 0, "echo reply: type 0, 64 bytes (%zu)", p.l4len);
        CHECK(fold(sum16(0, p.l4, p.l4len)) == 0, "echo reply: ICMP checksum");
        CHECK(memcmp(p.l4 + 4, icmp + 4, 60) == 0, "echo reply: id, seq and data echoed");
    }
    qclear();
    icmp[2] ^= 0x55;
    deliver(f, ip_pkt(f, 1, 43, 0x4000, icmp, 64));
    CHECK(qlen == 0, "bad ICMP checksum: no reply (%d frames)", qlen);
    // Our echo request, answered: ping.c reports the reply.
    ping_send_request(node, "10.0.2.2", 9);
    CHECK(next_ip(&p, 1) && p.l4[0] == 8 && fold(sum16(0, p.l4, p.l4len)) == 0, "echo request sent, checksum ok");
    if(p.l4){
        uint8_t rep[128];
        memcpy(rep, p.l4, p.l4len);
        rep[0] = 0;
        put16(rep + 2, 0);
        put16(rep + 2, fold(sum16(0, rep, p.l4len)));
        int before = ping_replies;
        deliver(f, ip_pkt(f, 1, 44, 0x4000, rep, p.l4len));
        CHECK(ping_replies == before + 1, "echo reply reported (net_ping_reply)");
        CHECK(ping_last.type == 0 && ping_last.ttl == 64 && ping_last.from == PEER_IP,
              "echo reply: type 0, its TTL as it arrived (%u), from the peer", ping_last.ttl);
    }
    qclear();
}

// Forwarding. A second NIC, eth1 on 10.9.0.1/24 with a host
// 10.9.0.5 behind it; datagrams from the peer on eth0 to that host.
#define FAR_IP   0x0a090005u          // 10.9.0.5
static const uint8_t FAR_MAC[6] = { 0x52, 0x54, 0x00, 0x09, 0x00, 0x05 };
static struct netdev nic1 = { .name = "eth1", .mac = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 }, .mtu = 1500,
                              .irq = "test" };

static void test_forward(void){
    uint8_t data[32], f[200], wire[60];
    struct pkt p;
    NodeNwProp* np = &node->node_nw_prop;
    Interface* eth1 = node_attach_netdev(node, &nic1);
    node_set_intf_ip_address(node, "eth1", "10.9.0.1", 24);
    ArpEntry* e = calloc(1, sizeof(ArpEntry));
    strcpy((char*)e->ip_addr.ip_addr, "10.9.0.5");
    memcpy(e->mac_addr.mac, FAR_MAC, 6);
    strcpy(e->oif_name, "eth1");
    arp_table_entry_add(NODE_ARP_TABLE(node), e);
    for(int i = 0; i < 32; i++){
        data[i] = (uint8_t)(0xa0 + i);
    }
    qclear();

    // Forwarding off (the kernel's default): dropped, counted.
    np->ip_forward = false;
    unsigned long refused = np->fwd_refused;
    deliver(f, ip_pkt_to(f, 17, 70, 0x4000, FAR_IP, 64, data, 32));
    CHECK(qlen == 0, "forwarding off: nothing sent (%d frames)", qlen);
    CHECK(np->fwd_refused == refused + 1, "forwarding off: counted as refused");

    // On: out of eth1 to the host's MAC, TTL 63, a good checksum, the
    // rest of the datagram as it came.
    np->ip_forward = true;
    unsigned long fwd = np->fwd_packets;
    size_t n = ip_pkt_to(f, 17, 71, 0x4000, FAR_IP, 64, data, 32);
    memcpy(wire, f + 14, 52);
    deliver(f, n);
    CHECK(next_ip(&p, 17), "forwarded datagram sent");
    if(p.ip){
        CHECK(memcmp(p.f, FAR_MAC, 6) == 0 && memcmp(p.f + 6, nic1.mac, 6) == 0,
              "forwarded: to the host's MAC from eth1's");
        CHECK(p.ip[8] == 63, "forwarded: TTL 64 -> %u", p.ip[8]);
        CHECK(p.ip_ok, "forwarded: IP header checksum recomputed");
        CHECK(memcmp(p.ip + 12, wire + 12, 8) == 0 && memcmp(p.l4, wire + 20, 32) == 0 && p.l4len == 32,
              "forwarded: addresses and payload unchanged");
    }
    CHECK(np->fwd_packets == fwd + 1, "forwarded: counted");
    qclear();

    // TTL 1: dropped, Time Exceeded to the peer quoting the header as
    // it arrived and 8 bytes of the payload.
    unsigned long expired = np->fwd_ttl_expired;
    n = ip_pkt_to(f, 17, 72, 0x4000, FAR_IP, 1, data, 32);
    memcpy(wire, f + 14, 52);
    deliver(f, n);
    CHECK(next_ip(&p, 1), "TTL 1: an ICMP message back");
    if(p.ip){
        CHECK(memcmp(p.f + 6, OUR_MAC, 6) == 0 && get32(p.ip + 16) == PEER_IP, "Time Exceeded: to the peer on eth0");
        CHECK(get32(p.ip + 12) == OUR_IP, "Time Exceeded: from eth0's address");
        CHECK(p.ip_ok, "Time Exceeded: IP header checksum");
        CHECK(p.l4len == 8 + 20 + 8 && p.l4[0] == 11 && p.l4[1] == 0, "Time Exceeded: type 11 code 0, 36 bytes (%zu)",
              p.l4len);
        CHECK(fold(sum16(0, p.l4, p.l4len)) == 0, "Time Exceeded: ICMP checksum");
        CHECK(p.l4len >= 36 && memcmp(p.l4 + 8, wire, 28) == 0, "Time Exceeded: the header and 8 bytes quoted");
    }
    CHECK(qlen == 0 || !next_ip(&p, 17), "TTL 1: the datagram itself not sent");
    CHECK(np->fwd_ttl_expired == expired + 1, "TTL 1: counted");
    qclear();

    // No Time Exceeded about an ICMP error (RFC 1812 4.3.2.7).
    uint8_t err[36];
    memset(err, 0, sizeof(err));
    err[0] = 3;
    put16(err + 2, fold(sum16(0, err, 36)));
    deliver(f, ip_pkt_to(f, 1, 73, 0x4000, FAR_IP, 1, err, 36));
    CHECK(qlen == 0, "TTL 1 on an ICMP error: no answer (%d frames)", qlen);

    // Our echo request with TTL 1, and a router's Time Exceeded about
    // it: reported to net_ping_reply with the router and type 11.
    ping_send_request_ttl(node, "10.9.0.5", 12, 1);
    CHECK(next_ip(&p, 1) && p.ip[8] == 1 && p.l4[0] == 8, "ping -t 1: echo request with TTL 1");
    if(p.ip){
        uint8_t te[8 + 28];
        memset(te, 0, 8);
        te[0] = 11;
        memcpy(te + 8, p.ip, 28);
        put16(te + 2, fold(sum16(0, te, sizeof(te))));
        int before = ping_replies;
        size_t m = ip_pkt_to(f, 1, 74, 0x4000, OUR_IP, 64, te, sizeof(te));
        memcpy(f, nic1.mac, 6);
        memcpy(f + 6, FAR_MAC, 6);
        put32(f + 14 + 12, 0x0a090001u + 1);   // 10.9.0.2, a router on eth1
        put16(f + 14 + 10, 0);
        put16(f + 14 + 10, fold(sum16(0, f + 14, 20)));
        pkt_receive(node, eth1, (char*)f, m);
        CHECK(ping_replies == before + 1 && ping_last.type == 11 && ping_last.seq == 12
              && ping_last.dst == FAR_IP && ping_last.from == 0x0a090002u,
              "Time Exceeded reported: type %u seq %u from %08x", ping_last.type, ping_last.seq, ping_last.from);
    }
    qclear();
    np->ip_forward = false;
}

// Longest prefix match on the whole prefix.
static void test_lpm(void){
    RtTable* rt;
    init_rt_table(&rt);
    rt_table_add_route(rt, "0.0.0.0", 0, "10.0.2.2", "eth0");
    rt_table_add_route(rt, "10.1.0.10", 32, "10.0.2.3", "eth0");
    rt_table_add_route(rt, "10.1.0.0", 24, "10.0.2.4", "eth0");
    L3Route* r = l3rib_lookup_lpm(rt, 0x0a01000au);
    CHECK(r && strcmp(r->gw_ip, "10.0.2.3") == 0, "LPM: 10.1.0.10 takes its /32");
    r = l3rib_lookup_lpm(rt, 0x0a010001u);
    CHECK(r && strcmp(r->gw_ip, "10.0.2.4") == 0, "LPM: 10.1.0.1 takes the /24, not 10.1.0.10/32 (%s)",
          r ? r->gw_ip : "none");
    r = l3rib_lookup_lpm(rt, 0x0a020001u);
    CHECK(r && r->mask == 0, "LPM: 10.2.0.1 takes the default route");
    clear_rt_table(rt);
    free(rt);
}

static uint8_t udp_got[2048];
static size_t udp_got_len;
static int udp_calls;
static void udp_handler(Node* n, Interface* iif, struct IpHdr* ip, UdpHdr* udp, char* payload, size_t len){
    udp_calls++;
    udp_got_len = len;
    memcpy(udp_got, payload, len);
}

// A UDP datagram of `n` bytes from the peer, in fragments of `fsz`
// (a multiple of 8) delivered in the order `order` (-1 ends it).
static void udp_fragments(uint16_t id, size_t n, size_t fsz, const int* order){
    uint8_t dgram[1600], f[1600];
    size_t total = 8 + n;
    put16(dgram, 5555);
    put16(dgram + 2, 7);
    put16(dgram + 4, (uint16_t)total);
    put16(dgram + 6, 0);
    for(size_t i = 0; i < n; i++){
        dgram[8 + i] = (uint8_t)(i * 7 + 3);
    }
    put16(dgram + 6, l4sum(PEER_IP, OUR_IP, 17, dgram, total));
    for(int k = 0; order[k] >= 0; k++){
        size_t off = (size_t)order[k] * fsz;
        size_t len = total - off < fsz ? total - off : fsz;
        bool more = off + len < total;
        deliver(f, ip_pkt(f, 17, id, (uint16_t)((more ? 0x2000 : 0) | (off / 8)), dgram + off, len));
    }
}

static void test_ip_fragments(void){
    struct pkt p;
    CHECK(udp_bind(node, 7, udp_handler), "udp_bind 7");
    // Unfragmented first.
    static const int one[] = { 0, -1 };
    udp_fragments(100, 100, 1600, one);
    CHECK(udp_calls == 1 && udp_got_len == 100 && udp_got[0] == 3, "a whole datagram delivered");
    // 1200 bytes in three fragments of 408, last first.
    static const int order[] = { 2, 0, 1, -1 };
    udp_fragments(101, 1200, 408, order);
    CHECK(udp_calls == 2, "reassembled datagram delivered once (%d calls)", udp_calls);
    bool same = udp_got_len == 1200;
    for(size_t i = 0; same && i < 1200; i++){
        same = udp_got[i] == (uint8_t)(i * 7 + 3);
    }
    CHECK(same, "reassembled datagram: 1200 bytes, in order (%zu)", udp_got_len);
    // Two of three: nothing.
    static const int part[] = { 0, 2, -1 };
    udp_fragments(102, 1200, 408, part);
    CHECK(udp_calls == 2, "an incomplete set delivers nothing");
    // The same id again completes it: the middle one arrives late.
    static const int late[] = { 1, -1 };
    udp_fragments(102, 1200, 408, late);
    CHECK(udp_calls == 3 && udp_got_len == 1200, "the late fragment completes it");
    qclear();
    // Our side fragments a send larger than the link MTU (ch34, ch43's
    // IF_MTU budget). udp_send_pkt takes at most 1400 bytes, so the
    // MTU goes down to 576 for this: 1208 bytes of UDP in 3 fragments.
    uint8_t big[1200];
    for(int i = 0; i < 1200; i++){
        big[i] = (uint8_t)(i ^ 0x5a);
    }
    IF_MTU(eth) = 576;
    udp_send_pkt(node, PEER_IP, 7, 9, (char*)big, sizeof(big));
    IF_MTU(eth) = 1500;
    uint8_t whole[1600];
    size_t got = 0;
    int frags = 0;
    bool ok = true;
    uint16_t id = 0;
    while(next_ip(&p, 17)){
        size_t off = (size_t)(p.frag & 0x1fff) * 8;
        ok = ok && p.ip_ok && (frags == 0 || p.id == id) && off + p.l4len <= sizeof(whole);
        id = p.id;
        if(ok){
            memcpy(whole + off, p.l4, p.l4len);
            got += p.l4len;
        }
        frags++;
        if(!(p.frag & 0x2000)){
            break;
        }
    }
    CHECK(ok && frags == 3, "a 1208-byte datagram leaves in 3 fragments at MTU 576 (%d)", frags);
    CHECK(got == 1208 && get16(whole + 4) == 1208 && memcmp(whole + 8, big, 1200) == 0, "fragments carry the datagram");
    CHECK(l4sum(OUR_IP, PEER_IP, 17, whole, got) == 0, "UDP checksum over the whole datagram");
    udp_unbind(node, 7);
    qclear();
}

static void reset_events(void){
    memset(ev_count, 0, sizeof(ev_count));
    ev_last = NULL;
}

// Advance the clock past the retransmission timer and run it.
static void tick(unsigned ms){
    now_ms += ms;
    tcp_timer();
}

static void test_tcp_passive(void){
    struct tseg s;
    const uint32_t piss = 5000;
    reset_events();
    Tcb* l = tcp_listen_tcb(node, 80, app_data);
    CHECK(l && l->state == TCP_LISTEN, "listen on 80");
    // A SYN to a closed port: RST|ACK, ack = its seq + 1.
    tcp_in(40000, 81, 777, 0, F_SYN, 65535, NULL, 0, 1460);
    CHECK(tcp_out(&s) && (s.flags & (F_RST | F_ACK)) == (F_RST | F_ACK) && s.ack == 778 && s.csum_ok,
          "SYN to a closed port: RST|ACK ack=778 (flags %02x ack %u)", s.flags, s.ack);
    // The handshake.
    tcp_in(40001, 80, piss, 0, F_SYN, 65535, NULL, 0, 1000);
    CHECK(tcp_out(&s) && s.flags == (F_SYN | F_ACK) && s.ack == piss + 1 && s.csum_ok, "SYN-ACK, ack = ISN + 1");
    CHECK(s.mss == 1460, "SYN-ACK carries MSS 1460 (%u)", s.mss);
    CHECK(s.win == 16384, "SYN-ACK advertises the application's window (%u)", s.win);
    uint32_t iss = s.seq;
    CHECK(ev_count[TCP_EV_ESTABLISHED] == 0, "not established before the third leg");
    // The third leg carries data.
    tcp_in(40001, 80, piss + 1, iss + 1, F_ACK | F_PSH, 4000, "GET / ", 6, 0);
    CHECK(ev_count[TCP_EV_ESTABLISHED] == 1 && ev_last && ev_last->state == TCP_ESTABLISHED, "ESTABLISHED");
    Tcb* t = ev_last;
    CHECK(t && t->peer_mss == 1000 && t->cwnd == 1000 && t->peer_window == 4000,
          "peer MSS 1000, cwnd 1 MSS, peer window 4000");
    CHECK(app_rx_len == 6 && memcmp(app_rx, "GET / ", 6) == 0, "data on the third leg delivered");
    CHECK(tcp_out(&s) && s.flags == F_ACK && s.ack == piss + 7, "data ACKed (ack %u)", s.ack);
    if(!t){
        return;
    }
    // In order, then out of order: dropped, ACK for what is expected.
    tcp_in(40001, 80, piss + 7, iss + 1, F_ACK | F_PSH, 4000, "HTTP/1.0", 8, 0);
    CHECK(app_rx_len == 14 && tcp_out(&s) && s.ack == piss + 15, "in order: ACK %u", s.ack);
    tcp_in(40001, 80, piss + 100, iss + 1, F_ACK | F_PSH, 4000, "zzz", 3, 0);
    CHECK(app_rx_len == 14 && tcp_out(&s) && s.ack == piss + 15, "out of order: dropped, duplicate ACK");
    // More than the application has room for: only that much is taken.
    app_window = 4;
    tcp_in(40001, 80, piss + 15, iss + 1, F_ACK | F_PSH, 4000, "\r\n\r\nX", 5, 0);
    CHECK(app_rx_len == 18 && tcp_out(&s) && s.ack == piss + 19, "4 bytes of 5 taken with a 4-byte window");
    app_window = 16384;
    tcp_in(40001, 80, piss + 19, iss + 1, F_ACK | F_PSH, 4000, "X", 1, 0);
    qclear();

    // The send side: 6000 bytes; cwnd is one MSS of 1000, so one segment.
    static uint8_t out[6000];
    for(int i = 0; i < 6000; i++){
        out[i] = (uint8_t)(i % 251);
    }
    CHECK(tcp_queue_data(t, (const char*)out, 6000) == 6000, "6000 bytes queued");
    uint32_t una = iss + 1;
    CHECK(tcp_out(&s) && s.seq == una && s.len == 1000 && memcmp(s.data, out, 1000) == 0 && s.csum_ok,
          "slow start: one 1000-byte segment (len %zu)", s.len);
    CHECK(!tcp_out(&s), "no second segment before the ACK");
    // Its ACK: cwnd 2000, but the peer's window is 4000 - so two more.
    tcp_in(40001, 80, piss + 20, una + 1000, F_ACK, 4000, NULL, 0, 0);
    CHECK(t->cwnd == 2000, "cwnd grows by 1 MSS per ACK (%u)", t->cwnd);
    int n = 0;
    while(tcp_out(&s)){
        CHECK(s.seq == una + 1000 + 1000 * (uint32_t)n && s.len == 1000, "segment %d in sequence", n);
        n++;
    }
    CHECK(n == 2, "two segments with cwnd 2000 (%d)", n);
    // The peer's window closes to 1000: one segment in flight already
    // fills it after this ACK of the first.
    tcp_in(40001, 80, piss + 20, una + 2000, F_ACK, 1000, NULL, 0, 0);
    CHECK(t->peer_window == 1000 && !tcp_out(&s), "peer window 1000 with 1000 in flight: nothing sent");
    // Window opens: sending goes on, limited by min(cwnd, window).
    tcp_in(40001, 80, piss + 20, una + 3000, F_ACK, 8000, NULL, 0, 0);
    n = 0;
    while(tcp_out(&s)){
        n++;
    }
    CHECK(n >= 2 && t->snd_nxt - t->snd_una <= (t->cwnd < 8000 ? t->cwnd : 8000), "window open: %d more segments", n);

    // Loss: no ACK, the timer fires after the 1 s RTO.
    uint32_t flight = t->snd_max - t->snd_una;
    uint32_t una0 = t->snd_una;
    unsigned long rtos = tcp_rto_count;
    tick(999);
    CHECK(!tcp_out(&s), "nothing before the RTO");
    tick(2);
    CHECK(tcp_rto_count == rtos + 1, "a retransmission timeout");
    CHECK(t->cwnd == 1000 && t->ssthresh == (flight / 2 > 2000 ? flight / 2 : 2000),
          "loss event: cwnd 1 MSS, ssthresh max(flight/2, 2 MSS) (%u, %u)", t->cwnd, t->ssthresh);
    CHECK(tcp_out(&s) && s.seq == una0 && s.len == 1000 && memcmp(s.data, out + (una0 - una), 1000) == 0,
          "go back to snd_una: the first unacknowledged segment again");
    CHECK(!tcp_out(&s), "one segment with cwnd 1 MSS");
    // The next timeout doubles the RTO: nothing at +1 s, again at +2 s.
    tick(1001);
    CHECK(!tcp_out(&s), "RTO doubled: no resend after 1 s");
    tick(1000);
    CHECK(tcp_out(&s) && s.seq == una0, "resend after 2 s");
    // The peer kept the rest, so its ACK covers everything sent
    // before the timeout, past the rewound snd_nxt.
    uint32_t max = t->snd_max;
    tcp_in(40001, 80, piss + 20, max, F_ACK, 8000, NULL, 0, 0);
    CHECK(t->snd_una == max && t->snd_nxt >= max && t->rto_ms == TCP_RTO_INIT_MS,
          "an ACK past snd_nxt is taken (una %u max %u), RTO reset", t->snd_una, max);
    // Drain the rest with ACKs.
    for(int guard = 0; guard < 50 && t->sndbuf_len; guard++){
        while(tcp_out(&s)){
        }
        tcp_in(40001, 80, piss + 20, t->snd_nxt, F_ACK, 8000, NULL, 0, 0);
    }
    CHECK(t->sndbuf_len == 0 && t->snd_una == una + 6000, "all 6000 bytes acknowledged");
    CHECK(ev_count[TCP_EV_SENT] > 0, "the application heard of the room (TCP_EV_SENT)");
    qclear();

    // The peer closes: CLOSE_WAIT, then our close: LAST_ACK, CLOSED.
    tcp_in(40001, 80, piss + 20, t->snd_nxt, F_FIN | F_ACK, 8000, NULL, 0, 0);
    CHECK(t->state == TCP_CLOSE_WAIT && ev_count[TCP_EV_FIN] == 1, "FIN: CLOSE_WAIT");
    CHECK(tcp_out(&s) && s.flags == F_ACK && s.ack == piss + 21, "FIN ACKed");
    // Data still goes in CLOSE_WAIT, and the FIN after it.
    CHECK(tcp_queue_data(t, "bye", 3) == 3, "send in CLOSE_WAIT");
    tcp_out(&s);
    tcp_close_tcb(t);
    CHECK(t->state == TCP_LAST_ACK, "close: LAST_ACK");
    CHECK(tcp_out(&s) && (s.flags & F_FIN) && s.seq == una + 6003, "FIN after the data (seq %u)", s.seq);
    tcp_in(40001, 80, piss + 21, una + 6004, F_ACK, 8000, NULL, 0, 0);
    CHECK(t->state == TCP_CLOSED && ev_count[TCP_EV_CLOSED] == 1, "final ACK: CLOSED");
    tcp_release(t);
    qclear();

    // A second connection, reset by the peer.
    reset_events();
    tcp_in(40002, 80, 9000, 0, F_SYN, 65535, NULL, 0, 1460);
    CHECK(tcp_out(&s) && s.flags == (F_SYN | F_ACK), "second SYN-ACK");
    CHECK(s.seq != iss, "a new ISS per connection");
    tcp_in(40002, 80, 9001, s.seq + 1, F_ACK, 65535, NULL, 0, 0);
    Tcb* t2 = ev_last;
    CHECK(t2 && t2->state == TCP_ESTABLISHED, "second connection established");
    tcp_in(40002, 80, 9001, 0, F_RST, 0, NULL, 0, 0);
    CHECK(t2 && t2->state == TCP_CLOSED && ev_count[TCP_EV_CLOSED] == 1, "RST: CLOSED");
    // A segment for the dead connection gets a RST.
    tcp_in(40002, 80, 9001, s.seq + 1, F_ACK | F_PSH, 65535, "late", 4, 0);
    CHECK(tcp_out(&s) && (s.flags & F_RST), "segment for a closed connection: RST");
    if(t2){
        tcp_release(t2);
    }
    tcp_close_tcb(l);
    tcp_release(l);
    tick(1);
    qclear();
}

static void test_tcp_active(void){
    struct tseg s;
    reset_events();
    Tcb* t = tcp_open(node, PEER_IP, 8080);
    CHECK(t && t->state == TCP_SYN_SENT, "connect: SYN_SENT");
    CHECK(tcp_out(&s) && s.flags == F_SYN && s.dport == 8080 && s.mss == TCP_OUR_MSS && s.csum_ok,
          "SYN with MSS %u", s.mss);
    if(!t){
        return;
    }
    t->on_data = app_data;
    uint32_t iss = s.seq;
    // The SYN is lost: sent again with the same ISS after the RTO.
    tick(1001);
    CHECK(tcp_out(&s) && s.flags == F_SYN && s.seq == iss, "SYN retransmitted");
    tcp_in(8080, s.sport, 70000, iss + 1, F_SYN | F_ACK, 30000, NULL, 0, 1200);
    CHECK(t->state == TCP_ESTABLISHED && ev_count[TCP_EV_ESTABLISHED] == 1, "SYN-ACK: ESTABLISHED");
    CHECK(t->peer_mss == 1200 && t->peer_window == 30000, "peer MSS and window from the SYN-ACK");
    CHECK(tcp_out(&s) && s.flags == F_ACK && s.ack == 70001 && s.seq == iss + 1, "third leg ACK");
    uint16_t lport = s.sport;
    // A response, then our close: FIN_WAIT_1, FIN_WAIT_2, CLOSED.
    app_rx_len = 0;
    tcp_in(8080, lport, 70001, iss + 1, F_ACK | F_PSH, 30000, "HTTP/1.0 200 OK\r\n", 17, 0);
    CHECK(app_rx_len == 17, "response data delivered");
    tcp_out(&s);
    tcp_close_tcb(t);
    CHECK(t->state == TCP_FIN_WAIT_1 && tcp_out(&s) && (s.flags & F_FIN) && s.seq == iss + 1, "FIN_WAIT_1, FIN sent");
    tcp_in(8080, lport, 70018, iss + 2, F_ACK, 30000, NULL, 0, 0);
    CHECK(t->state == TCP_FIN_WAIT_2, "FIN ACKed: FIN_WAIT_2");
    tcp_in(8080, lport, 70018, iss + 2, F_FIN | F_ACK, 30000, NULL, 0, 0);
    CHECK(t->state == TCP_CLOSED && tcp_out(&s) && s.flags == F_ACK && s.ack == 70019, "peer FIN: ACKed, CLOSED");
    tcp_release(t);
    qclear();

    // The FIN lost: sent again on the timer, and its late ACK still
    // moves FIN_WAIT_1 on.
    reset_events();
    t = tcp_open(node, PEER_IP, 8081);
    CHECK(t && tcp_out(&s), "second connect");
    if(!t){
        return;
    }
    iss = s.seq;
    tcp_in(8081, s.sport, 1, iss + 1, F_SYN | F_ACK, 30000, NULL, 0, 1460);
    tcp_out(&s);
    lport = s.sport;
    tcp_queue_data(t, "abc", 3);
    tcp_close_tcb(t);
    CHECK(tcp_out(&s) && s.len == 3 && tcp_out(&s) && (s.flags & F_FIN) && s.seq == iss + 4, "data then FIN");
    tick(1001);
    CHECK(tcp_out(&s) && s.len == 3 && s.seq == iss + 1, "timeout: the data again");
    CHECK(tcp_out(&s) && (s.flags & F_FIN), "and the FIN again");
    tcp_in(8081, lport, 2, iss + 5, F_ACK, 30000, NULL, 0, 0);
    CHECK(t->state == TCP_FIN_WAIT_2, "ACK of data and FIN: FIN_WAIT_2");
    tcp_release(t);
    qclear();

    // Nobody answers: TCP_MAX_RETRIES timeouts, then a RST and CLOSED.
    reset_events();
    t = tcp_open(node, PEER_IP, 8082);
    CHECK(t != NULL && tcp_out(&s) && s.flags == F_SYN, "third connect");
    if(!t){
        return;
    }
    int syns = 0;
    for(int i = 0; i < 40 && t->state == TCP_SYN_SENT; i++){
        tick(TCP_RTO_MAX_MS);
        while(tcp_out(&s)){
            syns += s.flags == F_SYN;
        }
    }
    CHECK(t->state == TCP_CLOSED && ev_count[TCP_EV_CLOSED] == 1, "abort after the retries");
    CHECK(syns == TCP_MAX_RETRIES, "%d retransmitted SYNs (%d expected)", syns, TCP_MAX_RETRIES);
    tcp_release(t);
    // Released TCBs are freed on the next tick.
    tick(1);
    qclear();
}

int main(int argc, char** argv){
    verbose = getenv("NET_TEST_VERBOSE") != NULL;
    // net/net.c's net_stack_init, with the fake NIC.
    Graph* g = create_new_graph("host");
    node = create_graph_node(g, "samos");
    node_set_device_type(node, L3_ROUTER);
    eth = node_attach_netdev(node, &nic);
    node_set_intf_ip_address(node, "eth0", "10.0.2.15", 24);
    rt_table_add_route(NODE_RT_TABLE(node), "0.0.0.0", 0, "10.0.2.2", "eth0");
    ping_register();
    udp_register();
    tcp_register();

    test_checksums();
    test_arp();
    test_icmp();
    test_ip_fragments();
    test_forward();
    test_lpm();
    test_tcp_passive();
    test_tcp_active();
    printf("net host test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
