#ifndef NET_DHCP_H
#define NET_DHCP_H
// A DHCP client (RFC 2131, options RFC 2132) for one NIC at a
// time, at boot: samos.ip=dhcp (eth0) or samos.ip.ethN=dhcp (net.h).
//
// The exchange is DISCOVER (broadcast), OFFER, REQUEST (broadcast,
// with the offer's address and server identifier), ACK; a NAK starts
// again. It runs below SamTcpIp: the frames are whole Ethernet
// frames built here and sent with netdev_xmit, and net_stack_input
// hands a frame for UDP port 68 to dhcp_input while an exchange runs
// (the interface has no address yet, so the stack would not take it).
// The lease time is recorded and shown (ifconfig), but nothing renews
// the lease: a lease shorter than the uptime runs out silently. Only
// the subnet mask (1), router (3), DNS (6, logged, unused), lease time
// (51), message type (53) and server identifier (54) options are read;
// option overload (52) into sname/file is not.
//
// The protocol half (dhcp_build, dhcp_parse) has no kernel dependency
// and no library call: tests64/host/dhcp_test.c builds it with
// -DDHCP_HOST_TEST, which leaves the client half out.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DHCP_DISCOVER   1                   // option 53's values
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_DECLINE    4
#define DHCP_ACK        5
#define DHCP_NAK        6

#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68
#define DHCP_FRAME_MAX      590             // 14 + 20 + 8 + 548 (BOOTP 300 at least)

// What a server said. Addresses are host order, 0 when absent.
struct dhcp_lease {
    uint8_t  type;                          // DHCP_OFFER, DHCP_ACK, DHCP_NAK
    uint32_t xid;
    uint32_t yiaddr;                        // "your" address
    uint32_t server;                        // option 54, else the IP source
    uint32_t mask;                          // option 1
    uint32_t router;                        // option 3, the first
    uint32_t dns;                           // option 6, the first
    uint32_t lease_s;                       // option 51; 0xffffffff infinite
};

// A DISCOVER (`type` DHCP_DISCOVER) or a REQUEST for `req_ip` from
// `server` (DHCP_REQUEST) into `frame` (DHCP_FRAME_MAX bytes): Ethernet
// broadcast from `mac`, IP 0.0.0.0 to 255.255.255.255, UDP 68 to 67,
// with the IP and UDP checksums. The frame's length, or 0.
size_t dhcp_build(uint8_t* frame, uint8_t type, const uint8_t mac[6], uint32_t xid, uint32_t req_ip,
                  uint32_t server);

// A frame for this client: IPv4, UDP to port 68, a BOOTP reply with
// `xid` for `mac` and a message type. Fills *out. 0, or < 0 (not one).
int dhcp_parse(const uint8_t* frame, size_t len, const uint8_t mac[6], uint32_t xid, struct dhcp_lease* out);

// The frame is IPv4 UDP to port 68 (a quick look for net_stack_input).
bool dhcp_is_client_frame(const uint8_t* frame, size_t len);

#ifndef DHCP_HOST_TEST
struct netdev;
// Run the exchange on `d` (at boot, the BSP, interrupts off, before
// the stack's tick): polls the NICs for at most a few seconds. 0 and
// *out the ACK, or < 0 (no answer, NAK after retries).
int  dhcp_configure(struct netdev* d, struct dhcp_lease* out);
// net_stack_input: a frame on `d`; true when the client took it.
bool dhcp_input(struct netdev* d, const uint8_t* frame, uint32_t len);
#endif

#endif
