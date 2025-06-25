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

// The Node, for net/socket.c. NULL before net_init.
struct Node* net_node(void);

// Under the net lock: wait for net_wake(obj), until `deadline_ms`
// (net_now_ms; 0: no deadline). Does not return.
void net_block(void* obj, uint64_t deadline_ms) __attribute__((noreturn));
// Under the net lock, first thing in a syscall that may have blocked:
// true when the last wait of this task ended by its deadline.
bool net_resume(void);
void net_wake(void* obj);

// `t` is being freed: forget its waits and close its process's sockets.
void net_task_gone(struct task* t);

#endif
