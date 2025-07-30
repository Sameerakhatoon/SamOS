#ifndef NET_NET_H
#define NET_NET_H
// The kernel's side of SamTcpIp: one Node ("samos") whose
// Interfaces are the netdevs, its static configuration, and what the
// network syscalls ask of it. Everything here takes the net lock.
//
// Configuration (kernel command line): samos.ip=A.B.C.D/N for eth0
// (default 10.0.2.15/24, QEMU's -netdev user), samos.gw=A.B.C.D the
// default route's gateway (default 10.0.2.2), samos.net_debug for
// SamTcpIp's own log lines; lo is 127.0.0.1/8.
//
// More than one NIC and routing: samos.ip.ethN=A.B.C.D/N for
// eth1, eth2, ... (no address without it; samos.ip.eth0 is the same
// as samos.ip); samos.gw=none for no default route (else it goes out
// of the interface whose subnet holds the gateway, eth0 if none);
// samos.route=DEST/N@GW[,DEST/N@GW...] static routes, each out of the
// interface whose subnet holds its gateway; samos.ip_forward routes
// datagrams for other hosts on (TTL-1, Time Exceeded at TTL 1).
// Without it SamOS is a host and drops them.
//
// samos.ip=dhcp (eth0) or samos.ip.ethN=dhcp asks a DHCP server
// for that NIC's address and mask at boot (net/dhcp.h); without
// samos.gw, eth0's lease also gives the default route (its router
// option; none if it has none). ifconfig shows the server and the
// lease time. The lease is not renewed. Without a lease the NIC has
// no address.
// samos.mtu.ethN=M (samos.mtu for eth0) the largest datagram ethN
// sends whole, 68 to 1500 (default 1500): a router fragments a larger
// forwarded datagram, or answers Fragmentation Needed when it has DF;
// routes change at run time through net_route_add and net_route_del.
//
// Waiting: a syscall that has to wait for the network
// records what it waits for and until when, blocks its task and lets
// the CPU go (net_block, under the net lock, does not return); the
// syscall runs again from the start when net_wake(obj) or the
// deadline wakes it (task_block's restart), and calls net_resume
// first to learn which of the two it was.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct task;
struct process;
struct Node;

// What net_info prints.
enum {
    NET_INFO_IFCONFIG = 0,
    NET_INFO_ARP      = 1,
    NET_INFO_ROUTE    = 2,
    NET_INFO_TCP      = 3,
};

// SamTcpIp's own dump (dump_intf_props, dump_arp_table, dump_rt_table,
// tcp_dump) into `buf`, NUL-terminated. The length, or < 0.
long net_info(int what, char* buf, size_t size);

// One ICMP echo request to `dst` (host order), from the current task.
// It blocks: the round trip in microseconds, or -ETIMEOUT.
long net_ping(uint32_t dst, uint32_t timeout_ms);

// What answered an echo request. Shared with user space
// (samos.h, struct samos_ping_result). Destination Unreachable
// too, with its code and (Fragmentation Needed) the next-hop MTU; for
// an echo reply the data's length and whether it came back as sent.
#define NET_PING_ECHO_REPLY     0           // ICMP types
#define NET_PING_DEST_UNREACH   3
#define NET_PING_TIME_EXCEEDED  11
struct net_ping_result {
    uint32_t from;                          // host order
    uint32_t rtt_us;
    uint8_t  ttl;                           // of the answer, as it arrived
    uint8_t  type;                          // NET_PING_*
    uint8_t  code;                          // The ICMP code
    uint8_t  data_ok;                       // Echo reply: the data as sent
    uint16_t mtu;                           // Fragmentation Needed's next-hop MTU
    uint16_t pad;
    uint32_t bytes;                         // Echo reply: data bytes
};

// Syscall 70's third argument: the TTL in the low 8 bits, DF in
// bit 8, the data bytes after the echo header in bits 16-31 (0: 16).
#define NET_PING_TTL(opts)      ((opts) & 0xff)
#define NET_PING_DF             0x100
#define NET_PING_SIZE(opts)     ((opts) >> 16)
#define NET_PING_SIZE_MAX       65507       // 65535 - IP header - echo header

// net_ping with the request's IP TTL (0: the default, 64) and the
// answer in *out: the round trip in microseconds for an echo reply or
// a router's Time Exceeded (out->type says which), or -ETIMEOUT.
// `size` data bytes (0: 16), DF when `df`; a Destination
// Unreachable answers it too.
long net_ping_ex(uint32_t dst, uint32_t timeout_ms, uint8_t ttl, uint32_t size, bool df,
                 struct net_ping_result* out);

// Change the routing table at run time. `dest`/`bits` is the
// prefix (host order; the host bits are cleared), `gw` the gateway,
// which must be on a connected subnet (its interface is the route's).
// Adding replaces a route to the same prefix. 0, -EINVARG (bad prefix,
// gateway not on a subnet), -EEXIST (adding over a connected subnet's
// route), -ENOENT (no such route to delete, or a connected subnet's
// route, which goes only with its address).
long net_route_add(uint32_t dest, uint32_t bits, uint32_t gw);
long net_route_del(uint32_t dest, uint32_t bits);

// The Node, for net/socket.c. NULL before net_init.
struct Node* net_node(void);

// Under the net lock: wait for net_wake(obj), until `deadline_ms`
// (net_now_ms; 0: no deadline). Does not return.
void net_block(void* obj, uint64_t deadline_ms) __attribute__((noreturn));
// Under the net lock, first thing in a syscall that may have blocked:
// true when the last wait of this task ended by its deadline.
bool net_resume(void);
void net_wake(void* obj);

// Waiting on several objects (poll): wait until net_wake_set(b)
// for any bit b of `set` (socket.c: a socket's index), or the deadline.
// Does not return, as net_block.
void net_block_set(uint64_t set, uint64_t deadline_ms) __attribute__((noreturn));
void net_wake_set(int bit);
// net_resume, and when this task's last wait ends here, that wait's
// deadline in *deadline_ms (left alone otherwise): a restarted call
// keeps the deadline it set the first time.
bool net_resume_ex(uint64_t* deadline_ms);
// `p` (kmalloc'd) is a syscall's kernel copy, freed
// by net_block if the call blocks (it runs again from the start and
// allocates anew), else by the caller after net_scratch_done(p). Takes
// the net lock.
void net_scratch(void* p);
void net_scratch_done(void* p);

// `t` is being freed: forget its waits and close its process's sockets.
void net_task_gone(struct task* t);

#endif
