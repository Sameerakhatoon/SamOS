#ifndef KERNEL_ISR80H_NET_H
#define KERNEL_ISR80H_NET_H
// Network syscalls (net/net.h).

struct trap_frame;
void* isr80h_command60_net_info(struct trap_frame* frame);
void* isr80h_command61_net_ping(struct trap_frame* frame);
void* isr80h_command70_net_ping_ex(struct trap_frame* frame);
// Poll, non-blocking sockets, addresses (net/socket.h).
void* isr80h_command71_poll(struct trap_frame* frame);
void* isr80h_command72_nonblock(struct trap_frame* frame);
void* isr80h_command73_getsockname(struct trap_frame* frame);
void* isr80h_command74_getpeername(struct trap_frame* frame);
// Routes at run time.
void* isr80h_command76_net_route_add(struct trap_frame* frame);
void* isr80h_command77_net_route_del(struct trap_frame* frame);
// Sockets (net/socket.h).
void* isr80h_command62_socket(struct trap_frame* frame);
void* isr80h_command63_bind(struct trap_frame* frame);
void* isr80h_command64_connect(struct trap_frame* frame);
void* isr80h_command65_listen(struct trap_frame* frame);
void* isr80h_command66_accept(struct trap_frame* frame);
void* isr80h_command67_sendto(struct trap_frame* frame);
void* isr80h_command68_recvfrom(struct trap_frame* frame);
void* isr80h_command69_sclose(struct trap_frame* frame);

#endif
