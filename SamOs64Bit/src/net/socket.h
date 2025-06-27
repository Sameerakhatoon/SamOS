#ifndef NET_SOCKET_H
#define NET_SOCKET_H
// Sockets over SamTcpIp's UDP and TCP. Not in the book.
//
// A socket is an index (0 to NET_SOCKETS - 1) into one system-wide
// table; each entry has an owner process, and another process's
// number is -EBADF. Addresses are one
// IPv4 address in host order and a port: there is no struct sockaddr.
//
//   stream (TCP): connect (blocks until the handshake ends), or bind +
//     listen + accept (blocks for a connection); send queues into the
//     TCB's send buffer (blocks while it is full), recv takes from the
//     socket's receive buffer (blocks while empty; 0 after the peer's
//     FIN); close sends the FIN after what is queued and lets go of the
//     TCB, which finishes the close on its own (tcp_release).
//   datagram (UDP): bind (or an ephemeral port on the first send);
//     sendto one datagram of up to SOCK_DGRAM_MAX bytes; recvfrom one
//     datagram, its source with it.
//
// Every call takes the net lock; the blocking ones wait with
// net_block (net.h). recv and accept may take a timeout.

#include <stdint.h>
#include <stddef.h>

#define NET_SOCKETS         64
#define SOCK_STREAM         1
#define SOCK_DGRAM          2
#define SOCK_RXBUF          16384
#define SOCK_BACKLOG_MAX    8
#define SOCK_DGRAMS         16
#define SOCK_DGRAM_MAX      1400        // udp_send_pkt's limit
#define SOCK_CONNECT_MS     20000

struct process;

// The peer of a received datagram or an accepted connection.
struct sock_addr {
    uint32_t ip;
    uint32_t port;
};

long socket_open(struct process* p, int type);
long socket_bind(struct process* p, int s, uint32_t port);
long socket_connect(struct process* p, int s, uint32_t ip, uint32_t port);
long socket_listen(struct process* p, int s, int backlog);
long socket_accept(struct process* p, int s, struct sock_addr* peer, uint32_t timeout_ms);
// From/to kernel buffers; the syscalls copy to and from the user.
long socket_sendto(struct process* p, int s, const void* buf, size_t len, uint32_t ip, uint32_t port);
long socket_recvfrom(struct process* p, int s, void* buf, size_t len, struct sock_addr* from, uint32_t timeout_ms);
long socket_close(struct process* p, int s);

// Under the net lock: close every socket of `p` (it is exiting).
void socket_process_gone(struct process* p);

#endif
