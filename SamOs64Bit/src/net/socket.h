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

// Poll, non-blocking sockets, the socket's addresses.
//
// poll: for each entry, the events of `events` that hold now go in
// `revents`, and SOCK_POLLERR, SOCK_POLLHUP and SOCK_POLLNVAL whether
// asked for or not; a negative `sock` is skipped (revents 0). Returns
// how many entries have revents, at once if any or if timeout_ms is
// 0, else when one of the sockets has news (any of them wakes the
// task) or after timeout_ms (-1: no limit); 0 when the time ran out.
//   POLLIN   stream: bytes to recv, the peer's FIN, or an error;
//            listener: a connection to accept; datagram: one queued
//   POLLOUT  stream: connected and room in the send buffer; datagram:
//            always
//   POLLERR  the connection failed or was reset (recv or connect
//            returns why)
//   POLLHUP  the connection is gone (reset, refused, or closed both
//            ways), or a stream socket neither connected, connecting
//            nor listening; the peer's FIN alone is POLLIN (recv 0)
//   POLLNVAL not a socket of this process
// The values are Linux's.
#define SOCK_POLLIN     0x01
#define SOCK_POLLOUT    0x04
#define SOCK_POLLERR    0x08
#define SOCK_POLLHUP    0x10
#define SOCK_POLLNVAL   0x20
#define SOCK_POLL_MAX   NET_SOCKETS
struct sock_pollfd {
    int32_t  sock;
    uint16_t events;
    uint16_t revents;
};
long socket_poll(struct process* p, struct sock_pollfd* fds, int n, int32_t timeout_ms);

// A non-blocking socket (mode 1; 0 blocking, as made; -1 only asks):
// accept, recv and send return -EAGAIN instead of waiting, connect
// -EINPROGRESS (poll for POLLOUT, or POLLERR; connect again says how
// it went: 0 or why not). Returns the mode now. An accepted socket
// starts blocking.
long socket_nonblock(struct process* p, int s, int mode);

// The local (peer 0) or remote (peer 1) address: ip 0 for a local
// address not yet fixed (unbound, or listening on every interface),
// -ENOTCONN for the peer of an unconnected socket.
long socket_name(struct process* p, int s, int peer, struct sock_addr* out);

// Under the net lock: close every socket of `p` (it is exiting).
void socket_process_gone(struct process* p);

// Under the net lock: the sockets in use, system-wide (netstat).
int socket_count(void);

#endif
