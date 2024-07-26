#ifndef KERNEL_ISR80H_NET_H
#define KERNEL_ISR80H_NET_H
// Network syscalls (net/net.h).

struct trap_frame;
void* isr80h_command60_net_info(struct trap_frame* frame);
void* isr80h_command61_net_ping(struct trap_frame* frame);

#endif
