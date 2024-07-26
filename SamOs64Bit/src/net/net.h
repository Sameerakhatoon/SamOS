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

#include <stdint.h>
#include <stddef.h>

struct task;

// What net_info prints.
enum {
    NET_INFO_IFCONFIG = 0,
    NET_INFO_ARP      = 1,
    NET_INFO_ROUTE    = 2,
};

// SamTcpIp's own dump (dump_intf_props, dump_arp_table, dump_rt_table)
// into `buf`, NUL-terminated. The length, or < 0.
long net_info(int what, char* buf, size_t size);

// One ICMP echo request to `dst` (host order), from the current task.
// It blocks (the syscall restarts when the reply or the timeout wakes
// it, task_block): the round trip in microseconds, or -ETIMEOUT.
long net_ping(uint32_t dst, uint32_t timeout_ms);

// `t` is being freed: forget it (its ping and its sockets' waits).
void net_task_gone(struct task* t);

#endif
