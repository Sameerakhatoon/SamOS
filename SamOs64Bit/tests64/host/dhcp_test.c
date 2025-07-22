// Host-side test of the DHCP client's frames (src/net/dhcp.c,
// built with -DDHCP_HOST_TEST: the protocol half only). The test plays
// the server with its own frame builder and checksum code.
//
//   build    DISCOVER and REQUEST byte for byte: Ethernet broadcast,
//            IP 0.0.0.0 to 255.255.255.255 with a good header checksum,
//            UDP 68 to 67 with a good checksum, BOOTP request, xid,
//            broadcast flag, chaddr, the cookie, the options (53, 61,
//            55; 50 and 54 in a REQUEST only), at least 300 BOOTP bytes
//   parse    OFFER, ACK and NAK from a server as QEMU's slirp sends
//            them (mask, router, DNS, lease, server identifier), with
//            and without a UDP checksum, pad options, IP options
//   reject   another xid or chaddr, a bad IP or UDP checksum, a
//            truncated frame or option, a fragment, no message type
//            (plain BOOTP), no server identifier, our own request
//            looped back, UDP to another port, ARP
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "net/dhcp.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if(!(cond)){ failures++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static const uint8_t mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t srv_mac[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };
#define XID 0x1234abcdu
#define IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static uint16_t g16(const uint8_t* p){ return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t g32(const uint8_t* p){ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void p16(uint8_t* p, uint16_t v){ p[0] = v >> 8; p[1] = (uint8_t)v; }
static void p32(uint8_t* p, uint32_t v){ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

// The test's own RFC 1071 sum: 0xffff (as folded) means the data checks.
static uint16_t sum16(const uint8_t* p, size_t n, uint32_t s){
    for(size_t i = 0; i < n; i++){
        s += (i & 1) ? p[i] : (uint32_t)p[i] << 8;
    }
    while(s >> 16){
        s = (s & 0xffff) + (s >> 16);
    }
    return (uint16_t)s;
}

static uint16_t udp_sum(const uint8_t* ip, size_t ihl){
    const uint8_t* udp = ip + ihl;
    size_t ulen = g16(udp + 4);
    uint32_t s = sum16(ip + 12, 8, 0);
    s += 17 + (uint32_t)ulen;
    return sum16(udp, ulen, s);
}

// Find option `code` in a BOOTP message's options; its length in *n.
static const uint8_t* opt(const uint8_t* bp, size_t blen, uint8_t code, int* n){
    size_t i = 240;
    while(i < blen){
        uint8_t c = bp[i++];
        if(c == 0){
            continue;
        }
        if(c == 255){
            break;
        }
        uint8_t l = bp[i++];
        if(c == code){
            *n = l;
            return bp + i;
        }
        i += l;
    }
    return NULL;
}

static void check_request(const uint8_t* f, size_t n, uint8_t type, uint32_t req, uint32_t server){
    const char* name = type == DHCP_DISCOVER ? "DISCOVER" : "REQUEST";
    CHECK(n >= 14 + 20 + 8 + 300, "%s: %zu bytes, BOOTP shorter than 300", name, n);
    CHECK(n <= DHCP_FRAME_MAX, "%s: %zu bytes", name, n);
    CHECK(memcmp(f, "\xff\xff\xff\xff\xff\xff", 6) == 0, "%s: not to the broadcast MAC", name);
    CHECK(memcmp(f + 6, mac, 6) == 0, "%s: source MAC", name);
    CHECK(g16(f + 12) == 0x0800, "%s: ethertype %04x", name, g16(f + 12));
    const uint8_t* ip = f + 14;
    CHECK(ip[0] == 0x45, "%s: IP version/IHL %02x", name, ip[0]);
    CHECK(g16(ip + 2) == n - 14, "%s: IP total length %u for %zu", name, g16(ip + 2), n - 14);
    CHECK(ip[8] == 64 && ip[9] == 17, "%s: TTL %u protocol %u", name, ip[8], ip[9]);
    CHECK(g32(ip + 12) == 0 && g32(ip + 16) == 0xffffffffu, "%s: not 0.0.0.0 to 255.255.255.255", name);
    CHECK(sum16(ip, 20, 0) == 0xffff, "%s: IP header checksum", name);
    const uint8_t* udp = ip + 20;
    CHECK(g16(udp) == 68 && g16(udp + 2) == 67, "%s: ports %u to %u", name, g16(udp), g16(udp + 2));
    CHECK(g16(udp + 4) == n - 34, "%s: UDP length", name);
    CHECK(g16(udp + 6) != 0 && udp_sum(ip, 20) == 0xffff, "%s: UDP checksum", name);
    const uint8_t* bp = udp + 8;
    size_t blen = n - 42;
    CHECK(bp[0] == 1 && bp[1] == 1 && bp[2] == 6 && bp[3] == 0, "%s: op/htype/hlen/hops", name);
    CHECK(g32(bp + 4) == XID, "%s: xid %08x", name, g32(bp + 4));
    CHECK(g16(bp + 10) == 0x8000, "%s: flags %04x, not BROADCAST", name, g16(bp + 10));
    CHECK(g32(bp + 12) == 0 && g32(bp + 16) == 0 && g32(bp + 20) == 0 && g32(bp + 24) == 0,
          "%s: ciaddr/yiaddr/siaddr/giaddr not 0", name);
    CHECK(memcmp(bp + 28, mac, 6) == 0, "%s: chaddr", name);
    CHECK(g32(bp + 236) == 0x63825363u, "%s: magic cookie", name);
    int l = 0;
    const uint8_t* o = opt(bp, blen, 53, &l);
    CHECK(o && l == 1 && o[0] == type, "%s: option 53", name);
    o = opt(bp, blen, 61, &l);
    CHECK(o && l == 7 && o[0] == 1 && memcmp(o + 1, mac, 6) == 0, "%s: option 61 client id", name);
    o = opt(bp, blen, 55, &l);
    CHECK(o && l >= 4 && memchr(o, 1, l) && memchr(o, 3, l) && memchr(o, 51, l),
          "%s: option 55 asks for the mask, router and lease", name);
    o = opt(bp, blen, 50, &l);
    if(type == DHCP_REQUEST){
        CHECK(o && l == 4 && g32(o) == req, "%s: option 50 requested address", name);
        o = opt(bp, blen, 54, &l);
        CHECK(o && l == 4 && g32(o) == server, "%s: option 54 server id", name);
    } else {
        CHECK(!o, "%s: option 50 in a DISCOVER", name);
        CHECK(!opt(bp, blen, 54, &l), "%s: option 54 in a DISCOVER", name);
    }
}

static void test_build(void){
    uint8_t f[DHCP_FRAME_MAX];
    size_t n = dhcp_build(f, DHCP_DISCOVER, mac, XID, 0, 0);
    check_request(f, n, DHCP_DISCOVER, 0, 0);
    n = dhcp_build(f, DHCP_REQUEST, mac, XID, IP(10, 0, 2, 15), IP(10, 0, 2, 2));
    check_request(f, n, DHCP_REQUEST, IP(10, 0, 2, 15), IP(10, 0, 2, 2));
    CHECK(dhcp_build(f, DHCP_ACK, mac, XID, 0, 0) == 0, "dhcp_build made a server's message");
    CHECK(dhcp_is_client_frame(f, n) == false, "a request (to port 67) taken as a client frame");
}

// ---- the server's side ---------------------------------------------------------
struct reply {
    uint8_t  type;                  // 0: no option 53
    uint32_t xid, yiaddr, server;   // server 0: no option 54
    uint32_t mask, router, dns, lease;
    bool     udp_csum, pads, ip_opts;
    const uint8_t* chaddr;
    uint8_t  op;
};

// A reply from 10.0.2.2 to 255.255.255.255 (slirp broadcasts it).
static size_t server_frame(uint8_t* f, const struct reply* r){
    memset(f, 0, 1514);
    memset(f, 0xff, 6);
    memcpy(f + 6, srv_mac, 6);
    p16(f + 12, 0x0800);
    uint8_t* ip = f + 14;
    size_t ihl = r->ip_opts ? 24 : 20;
    uint8_t* udp = ip + ihl;
    uint8_t* bp = udp + 8;
    bp[0] = r->op ? r->op : 2;
    bp[1] = 1;
    bp[2] = 6;
    p32(bp + 4, r->xid);
    p32(bp + 16, r->yiaddr);
    p32(bp + 20, IP(10, 0, 2, 2));
    memcpy(bp + 28, r->chaddr ? r->chaddr : mac, 6);
    p32(bp + 236, 0x63825363u);
    uint8_t* o = bp + 240;
    if(r->pads){
        *o++ = 0;
        *o++ = 0;
    }
    if(r->type){
        *o++ = 53; *o++ = 1; *o++ = r->type;
    }
    if(r->server){
        *o++ = 54; *o++ = 4; p32(o, r->server); o += 4;
    }
    if(r->pads){
        *o++ = 0;
    }
    if(r->lease){
        *o++ = 51; *o++ = 4; p32(o, r->lease); o += 4;
    }
    if(r->mask){
        *o++ = 1; *o++ = 4; p32(o, r->mask); o += 4;
    }
    if(r->router){
        *o++ = 3; *o++ = 8; p32(o, r->router); p32(o + 4, IP(10, 0, 2, 254)); o += 8;
    }
    if(r->dns){
        *o++ = 6; *o++ = 4; p32(o, r->dns); o += 4;
    }
    *o++ = 255;
    size_t blen = (size_t)(o - bp) < 300 ? 300 : (size_t)(o - bp);
    size_t ulen = 8 + blen;
    p16(udp, 67);
    p16(udp + 2, 68);
    p16(udp + 4, (uint16_t)ulen);
    ip[0] = 0x40 | (uint8_t)(ihl / 4);
    p16(ip + 2, (uint16_t)(ihl + ulen));
    ip[8] = 64;
    ip[9] = 17;
    p32(ip + 12, IP(10, 0, 2, 2));
    p32(ip + 16, 0xffffffffu);
    if(r->ip_opts){
        ip[20] = 1; ip[21] = 1; ip[22] = 1; ip[23] = 0;    // NOP NOP NOP EOL
    }
    p16(ip + 10, (uint16_t)~sum16(ip, ihl, 0));
    if(r->udp_csum){
        uint16_t c = (uint16_t)~udp_sum(ip, ihl);
        p16(udp + 6, c ? c : 0xffff);
    }
    return 14 + ihl + ulen;
}

static struct reply slirp_offer(void){
    struct reply r = { DHCP_OFFER, XID, IP(10, 0, 2, 15), IP(10, 0, 2, 2), IP(255, 255, 255, 0),
                       IP(10, 0, 2, 2), IP(10, 0, 2, 3), 86400, true, false, false, NULL, 0 };
    return r;
}

static void test_parse(void){
    uint8_t f[1514];
    struct dhcp_lease l;
    struct reply r = slirp_offer();
    size_t n = server_frame(f, &r);
    CHECK(dhcp_is_client_frame(f, n), "an OFFER is not a client frame");
    int res = dhcp_parse(f, n, mac, XID, &l);
    CHECK(res == 0, "OFFER: %d", res);
    CHECK(l.type == DHCP_OFFER && l.xid == XID, "OFFER: type %u xid %08x", l.type, l.xid);
    CHECK(l.yiaddr == IP(10, 0, 2, 15), "OFFER: yiaddr %08x", l.yiaddr);
    CHECK(l.server == IP(10, 0, 2, 2), "OFFER: server %08x", l.server);
    CHECK(l.mask == IP(255, 255, 255, 0), "OFFER: mask %08x", l.mask);
    CHECK(l.router == IP(10, 0, 2, 2), "OFFER: router %08x (the first of two)", l.router);
    CHECK(l.dns == IP(10, 0, 2, 3), "OFFER: DNS %08x", l.dns);
    CHECK(l.lease_s == 86400, "OFFER: lease %u", l.lease_s);

    r.type = DHCP_ACK;
    r.udp_csum = false;
    r.pads = true;
    r.ip_opts = true;
    r.lease = 0xffffffffu;
    r.server = IP(10, 0, 2, 4);              // the identifier wins over the IP source
    n = server_frame(f, &r);
    memset(&l, 0, sizeof(l));
    res = dhcp_parse(f, n, mac, XID, &l);
    CHECK(res == 0 && l.type == DHCP_ACK, "ACK without a UDP checksum, pads, IP options: %d", res);
    CHECK(l.lease_s == 0xffffffffu && l.server == IP(10, 0, 2, 4), "ACK: lease %u server %08x", l.lease_s, l.server);

    struct reply nak = { DHCP_NAK, XID, 0, IP(10, 0, 2, 2), 0, 0, 0, 0, true, false, false, NULL, 0 };
    n = server_frame(f, &nak);
    res = dhcp_parse(f, n, mac, XID, &l);
    CHECK(res == 0 && l.type == DHCP_NAK, "NAK: %d type %u", res, l.type);

    struct reply bare = slirp_offer();       // no mask, router, DNS: the client takes /24
    bare.mask = bare.router = bare.dns = bare.lease = 0;
    n = server_frame(f, &bare);
    res = dhcp_parse(f, n, mac, XID, &l);
    CHECK(res == 0 && !l.mask && !l.router && !l.dns && !l.lease_s, "OFFER with only 53 and 54: %d", res);
}

static void test_reject(void){
    uint8_t f[1514];
    struct dhcp_lease l;
    struct reply r = slirp_offer();
    size_t n = server_frame(f, &r);
    CHECK(dhcp_parse(f, n, mac, XID + 1, &l) < 0, "another xid taken");
    static const uint8_t other[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 };
    CHECK(dhcp_parse(f, n, other, XID, &l) < 0, "another chaddr taken");
    CHECK(dhcp_parse(f, n - 200, mac, XID, &l) < 0, "a truncated frame taken");
    CHECK(dhcp_parse(f, 30, mac, XID, &l) < 0, "a runt taken");

    uint8_t g[1514];
    memcpy(g, f, n);
    g[14 + 8] ^= 1;                          // TTL: the IP checksum breaks
    CHECK(dhcp_parse(g, n, mac, XID, &l) == -4, "a bad IP checksum taken");
    memcpy(g, f, n);
    g[14 + 20 + 8 + 16] ^= 1;                // yiaddr: the UDP checksum breaks
    CHECK(dhcp_parse(g, n, mac, XID, &l) == -6, "a bad UDP checksum taken");
    memcpy(g, f, n);
    p16(g + 14 + 6, 0x2000);                 // more fragments
    p16(g + 14 + 10, 0);
    p16(g + 14 + 10, (uint16_t)~sum16(g + 14, 20, 0));
    CHECK(dhcp_parse(g, n, mac, XID, &l) == -3, "a fragment taken");
    memcpy(g, f, n);
    g[14 + 9] = 6;                           // TCP
    CHECK(!dhcp_is_client_frame(g, n) && dhcp_parse(g, n, mac, XID, &l) < 0, "TCP taken");
    memcpy(g, f, n);
    p16(g + 14 + 20 + 2, 69);
    CHECK(!dhcp_is_client_frame(g, n), "UDP to port 69 taken");
    memcpy(g, f, n);
    p16(g + 12, 0x0806);
    CHECK(!dhcp_is_client_frame(g, n), "ARP taken");

    struct reply bootp = slirp_offer();      // no option 53: plain BOOTP
    bootp.type = 0;
    n = server_frame(f, &bootp);
    CHECK(dhcp_parse(f, n, mac, XID, &l) == -10, "a BOOTP reply taken");
    struct reply noid = slirp_offer();
    noid.server = 0;
    n = server_frame(f, &noid);
    CHECK(dhcp_parse(f, n, mac, XID, &l) == -11, "an OFFER without a server identifier taken");
    struct reply req = slirp_offer();
    req.op = 1;
    n = server_frame(f, &req);
    CHECK(dhcp_parse(f, n, mac, XID, &l) == -7, "a BOOTREQUEST taken");

    // An option whose length runs past the message: the end option is
    // gone and a lease option claims 200 bytes.
    struct reply cut = slirp_offer();
    cut.udp_csum = false;
    n = server_frame(f, &cut);
    uint8_t* bp = f + 14 + 20 + 8;
    size_t blen = n - 42;
    memset(bp + 240, 0, blen - 240);
    bp[240] = 53; bp[241] = 1; bp[242] = DHCP_OFFER;
    bp[243] = 54; bp[244] = 4; p32(bp + 245, IP(10, 0, 2, 2));
    bp[249] = 51; bp[250] = 200;
    CHECK(dhcp_parse(f, n, mac, XID, &l) == -9, "an option past the end taken");

    // Our own DISCOVER looped back is a request to port 67.
    n = dhcp_build(f, DHCP_DISCOVER, mac, XID, 0, 0);
    CHECK(dhcp_parse(f, n, mac, XID, &l) < 0, "our own DISCOVER taken");
}

int main(void){
    test_build();
    test_parse();
    test_reject();
    printf("dhcp: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
