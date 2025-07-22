// A DHCP client (RFC 2131) for boot-time configuration. See
// dhcp.h. The first half builds and parses the frames (no kernel
// dependency, host-tested); the second runs the exchange on a netdev.

#include "dhcp.h"

#define ETH_HLEN        14
#define IP_HLEN         20
#define UDP_HLEN        8
#define BOOTP_FIXED     236                 // op .. file
#define BOOTP_MIN       300                 // RFC 951's size; older servers want it
#define DHCP_COOKIE     0x63825363u

#define OPT_PAD         0
#define OPT_MASK        1
#define OPT_ROUTER      3
#define OPT_DNS         6
#define OPT_REQ_IP      50
#define OPT_LEASE       51
#define OPT_MSG_TYPE    53
#define OPT_SERVER_ID   54
#define OPT_PARAMS      55
#define OPT_CLIENT_ID   61
#define OPT_END         255

static void put16(uint8_t* p, uint16_t v){
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t* p, uint32_t v){
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get16(const uint8_t* p){
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t get32(const uint8_t* p){
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// RFC 1071 over `len` bytes, starting from `sum`; not complemented.
static uint32_t csum_add(uint32_t sum, const uint8_t* p, size_t len){
    for(size_t i = 0; i + 1 < len; i += 2){
        sum += get16(p + i);
    }
    if(len & 1){
        sum += (uint32_t)p[len - 1] << 8;
    }
    return sum;
}

static uint16_t csum_fold(uint32_t sum){
    while(sum >> 16){
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

size_t dhcp_build(uint8_t* frame, uint8_t type, const uint8_t mac[6], uint32_t xid, uint32_t req_ip,
                  uint32_t server){
    if(type != DHCP_DISCOVER && type != DHCP_REQUEST){
        return 0;
    }
    for(size_t i = 0; i < DHCP_FRAME_MAX; i++){
        frame[i] = 0;
    }
    uint8_t* eth  = frame;
    uint8_t* ip   = eth + ETH_HLEN;
    uint8_t* udp  = ip + IP_HLEN;
    uint8_t* bp   = udp + UDP_HLEN;

    // BOOTP: a request from this hardware address, replies broadcast
    // (the client has no address to receive a unicast on).
    bp[0] = 1;                              // BOOTREQUEST
    bp[1] = 1;                              // Ethernet
    bp[2] = 6;
    put32(bp + 4, xid);
    put16(bp + 10, 0x8000);                 // flags: BROADCAST
    for(int i = 0; i < 6; i++){
        bp[28 + i] = mac[i];                // chaddr
    }
    put32(bp + BOOTP_FIXED, DHCP_COOKIE);
    uint8_t* o = bp + BOOTP_FIXED + 4;
    *o++ = OPT_MSG_TYPE;
    *o++ = 1;
    *o++ = type;
    *o++ = OPT_CLIENT_ID;
    *o++ = 7;
    *o++ = 1;
    for(int i = 0; i < 6; i++){
        *o++ = mac[i];
    }
    if(type == DHCP_REQUEST){
        *o++ = OPT_REQ_IP;
        *o++ = 4;
        put32(o, req_ip);
        o += 4;
        *o++ = OPT_SERVER_ID;
        *o++ = 4;
        put32(o, server);
        o += 4;
    }
    *o++ = OPT_PARAMS;
    *o++ = 4;
    *o++ = OPT_MASK;
    *o++ = OPT_ROUTER;
    *o++ = OPT_DNS;
    *o++ = OPT_LEASE;
    *o++ = OPT_END;
    size_t bootp = (size_t)(o - bp);
    if(bootp < BOOTP_MIN){
        bootp = BOOTP_MIN;                  // padded with zeros (OPT_PAD)
    }

    // UDP 68 to 67, its checksum over the pseudo-header (0.0.0.0 to
    // 255.255.255.255, protocol 17).
    size_t ulen = UDP_HLEN + bootp;
    put16(udp, DHCP_CLIENT_PORT);
    put16(udp + 2, DHCP_SERVER_PORT);
    put16(udp + 4, (uint16_t)ulen);
    uint8_t pseudo[12] = { 0, 0, 0, 0, 255, 255, 255, 255, 0, 17, (uint8_t)(ulen >> 8), (uint8_t)ulen };
    uint16_t uc = csum_fold(csum_add(csum_add(0, pseudo, 12), udp, ulen));
    put16(udp + 6, uc ? uc : 0xffff);

    // IPv4 0.0.0.0 to 255.255.255.255, TTL 64.
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)(IP_HLEN + ulen));
    ip[8] = 64;
    ip[9] = 17;
    put32(ip + 16, 0xffffffffu);
    put16(ip + 10, csum_fold(csum_add(0, ip, IP_HLEN)));

    for(int i = 0; i < 6; i++){
        eth[i]     = 0xff;
        eth[6 + i] = mac[i];
    }
    put16(eth + 12, 0x0800);
    return ETH_HLEN + IP_HLEN + ulen;
}

bool dhcp_is_client_frame(const uint8_t* frame, size_t len){
    if(len < ETH_HLEN + IP_HLEN + UDP_HLEN || get16(frame + 12) != 0x0800){
        return false;
    }
    const uint8_t* ip = frame + ETH_HLEN;
    size_t ihl = (size_t)(ip[0] & 15) * 4;
    if((ip[0] >> 4) != 4 || ihl < IP_HLEN || ip[9] != 17 || len < ETH_HLEN + ihl + UDP_HLEN){
        return false;
    }
    return get16(ip + ihl + 2) == DHCP_CLIENT_PORT;
}

int dhcp_parse(const uint8_t* frame, size_t len, const uint8_t mac[6], uint32_t xid, struct dhcp_lease* out){
    if(!dhcp_is_client_frame(frame, len)){
        return -1;
    }
    const uint8_t* ip = frame + ETH_HLEN;
    size_t ihl = (size_t)(ip[0] & 15) * 4;
    size_t ip_len = get16(ip + 2);
    if(ip_len < ihl + UDP_HLEN || ETH_HLEN + ip_len > len){
        return -2;                          // truncated
    }
    if((get16(ip + 6) & 0x3fff) != 0){
        return -3;                          // a fragment
    }
    if(csum_fold(csum_add(0, ip, ihl)) != 0){
        return -4;                          // bad IP header checksum
    }
    const uint8_t* udp = ip + ihl;
    size_t ulen = get16(udp + 4);
    if(ulen < UDP_HLEN + BOOTP_FIXED + 4 || ulen > ip_len - ihl){
        return -5;
    }
    if(get16(udp + 6)){                     // 0: no UDP checksum (IPv4)
        uint8_t pseudo[12];
        for(int i = 0; i < 8; i++){
            pseudo[i] = ip[12 + i];
        }
        pseudo[8]  = 0;
        pseudo[9]  = 17;
        pseudo[10] = (uint8_t)(ulen >> 8);
        pseudo[11] = (uint8_t)ulen;
        if(csum_fold(csum_add(csum_add(0, pseudo, 12), udp, ulen)) != 0){
            return -6;                      // bad UDP checksum
        }
    }
    const uint8_t* bp = udp + UDP_HLEN;
    size_t blen = ulen - UDP_HLEN;
    if(bp[0] != 2 || bp[1] != 1 || bp[2] != 6 || get32(bp + 4) != xid){
        return -7;                          // not a reply to this exchange
    }
    for(int i = 0; i < 6; i++){
        if(bp[28 + i] != mac[i]){
            return -7;
        }
    }
    if(get32(bp + BOOTP_FIXED) != DHCP_COOKIE){
        return -8;
    }
    struct dhcp_lease l = { 0 };
    l.xid    = xid;
    l.yiaddr = get32(bp + 16);
    l.server = get32(ip + 12);
    bool server_opt = false;
    size_t i = BOOTP_FIXED + 4;
    while(i < blen){
        uint8_t code = bp[i++];
        if(code == OPT_PAD){
            continue;
        }
        if(code == OPT_END){
            break;
        }
        if(i >= blen || i + 1 + bp[i] > blen){
            return -9;                      // an option runs past the end
        }
        uint8_t n = bp[i++];
        const uint8_t* v = bp + i;
        switch(code){
        case OPT_MSG_TYPE:
            if(n >= 1){
                l.type = v[0];
            }
            break;
        case OPT_MASK:
            if(n >= 4){
                l.mask = get32(v);
            }
            break;
        case OPT_ROUTER:
            if(n >= 4){
                l.router = get32(v);
            }
            break;
        case OPT_DNS:
            if(n >= 4){
                l.dns = get32(v);
            }
            break;
        case OPT_LEASE:
            if(n >= 4){
                l.lease_s = get32(v);
            }
            break;
        case OPT_SERVER_ID:
            if(n >= 4){
                l.server = get32(v);
                server_opt = true;
            }
            break;
        }
        i += n;
    }
    if(l.type != DHCP_OFFER && l.type != DHCP_ACK && l.type != DHCP_NAK){
        return -10;                         // a plain BOOTP reply, or a type a client never gets
    }
    if(l.type != DHCP_NAK && (!l.yiaddr || !server_opt)){
        return -11;                         // RFC 2131 table 3: both MUST be there
    }
    *out = l;
    return 0;
}

#ifndef DHCP_HOST_TEST
// ---- The client ---------------------------------------------------------------

#include "netdev.h"
#include "arch/arch.h"
#include "io/serial.h"
#include "status.h"

#define DHCP_ATTEMPTS   4

static struct {
    struct netdev*    dev;                  // NULL: no exchange running
    uint32_t          xid;
    uint8_t           want;                 // DHCP_OFFER, or DHCP_ACK (a NAK too)
    bool              got;
    struct dhcp_lease lease;
} pending;

bool dhcp_input(struct netdev* d, const uint8_t* frame, uint32_t len){
    if(!pending.dev || d != pending.dev || !dhcp_is_client_frame(frame, len)){
        return false;
    }
    struct dhcp_lease l;
    if(!pending.got && dhcp_parse(frame, len, d->mac, pending.xid, &l) == 0){
        if(pending.want == DHCP_OFFER ? l.type == DHCP_OFFER : l.type != DHCP_OFFER){
            pending.lease = l;
            pending.got   = true;
        }
    }
    return true;                            // ours either way: the stack has no port 68
}

static int dhcp_send(struct netdev* d, uint8_t type, uint32_t req_ip, uint32_t server){
    static uint8_t frame[DHCP_FRAME_MAX];
    size_t n = dhcp_build(frame, type, d->mac, pending.xid, req_ip, server);
    return n ? netdev_xmit(d, frame, (uint32_t)n) : -EINVARG;
}

// Poll the NICs until the answer is in or `ms` have gone.
static bool dhcp_wait(uint8_t want, uint32_t ms){
    pending.want = want;
    pending.got  = false;
    uint64_t end = arch_uptime_us() + (uint64_t)ms * 1000;
    while(!pending.got && arch_uptime_us() < end){
        net_poll();
        if(!pending.got){
            udelay(500);
        }
    }
    return pending.got;
}

static void dhcp_log(struct netdev* d, const char* what){
    serial_write("net: ");
    serial_write(d->name);
    serial_write(" DHCP: ");
    serial_write(what);
    serial_write("\n");
}

int dhcp_configure(struct netdev* d, struct dhcp_lease* out){
    uint32_t seed = (uint32_t)d->mac[2] << 24 | (uint32_t)d->mac[3] << 16 | (uint32_t)d->mac[4] << 8 | d->mac[5];
    pending.dev = d;
    int res = -ETIMEOUT;
    for(int attempt = 0; attempt < DHCP_ATTEMPTS && res < 0; attempt++){
        uint32_t ms = attempt ? 2000 : 1000;
        pending.xid = seed ^ (uint32_t)arch_uptime_us() ^ ((uint32_t)attempt << 28);
        if(dhcp_send(d, DHCP_DISCOVER, 0, 0) < 0 || !dhcp_wait(DHCP_OFFER, ms)){
            dhcp_log(d, "no offer");
            continue;
        }
        struct dhcp_lease offer = pending.lease;
        if(dhcp_send(d, DHCP_REQUEST, offer.yiaddr, offer.server) < 0 || !dhcp_wait(DHCP_ACK, ms)){
            dhcp_log(d, "no answer to the request");
            continue;
        }
        if(pending.lease.type == DHCP_NAK){
            dhcp_log(d, "NAK");
            continue;
        }
        *out = pending.lease;
        res = 0;
    }
    pending.dev = NULL;
    return res;
}
#endif
