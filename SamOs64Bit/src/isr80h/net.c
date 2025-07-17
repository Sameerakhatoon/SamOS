// Network syscalls. The work is net/net.c's.

#include "isr80h/isr80h.h"
#include "net.h"
#include "net/net.h"
#include "net/socket.h"
#include "task/task.h"
#include "task/process.h"
#include "task/uaccess.h"
#include "memory/heap/kheap.h"
#include "status.h"
#include "kernel.h"

#define NET_INFO_MAX  8192

// long net_info(int what, char* buf, long size): ifconfig, arp or
// route as text (net.h NET_INFO_*), NUL-terminated; its length, or a
// negative status.
void* isr80h_command60_net_info(struct trap_frame* frame){
    int   what = (int)(intptr_t)isr80h_arg(frame, 0);
    void* ubuf = isr80h_arg(frame, 1);
    long  size = (long)(intptr_t)isr80h_arg(frame, 2);
    if(size <= 0){
        return ERROR(-EINVARG);
    }
    if(size > NET_INFO_MAX){
        size = NET_INFO_MAX;
    }
    char* buf = kmalloc((size_t)size);
    if(!buf){
        return ERROR(-ENOMEM);
    }
    long n = net_info(what, buf, (size_t)size);
    if(n >= 0){
        if(n >= size){
            n = size - 1;
        }
        if(copy_to_user(task_current()->process, ubuf, buf, (size_t)n + 1) < 0){
            n = -EFAULT;
        }
    }
    kfree(buf);
    return ERROR(n);
}

// long net_ping(unsigned int ip, unsigned int timeout_ms): one echo
// request to `ip` (host order); blocks until the reply (the round trip
// in microseconds) or the timeout (-ETIMEOUT).
void* isr80h_command61_net_ping(struct trap_frame* frame){
    uint32_t ip      = (uint32_t)(uintptr_t)isr80h_arg(frame, 0);
    uint32_t timeout = (uint32_t)(uintptr_t)isr80h_arg(frame, 1);
    if(!ip || !timeout || timeout > 60000){
        return ERROR(-EINVARG);
    }
    return ERROR(net_ping(ip, timeout));
}

// Long net_ping_ex(unsigned int ip, unsigned int timeout_ms,
// unsigned int ttl, struct net_ping_result* out): net_ping with the
// request's IP TTL (0: the default) and what answered in *out (may be
// NULL): an echo reply or a router's Time Exceeded.
void* isr80h_command70_net_ping_ex(struct trap_frame* frame){
    uint32_t ip      = (uint32_t)(uintptr_t)isr80h_arg(frame, 0);
    uint32_t timeout = (uint32_t)(uintptr_t)isr80h_arg(frame, 1);
    uint32_t ttl     = (uint32_t)(uintptr_t)isr80h_arg(frame, 2);
    void*    uout    = isr80h_arg(frame, 3);
    if(!ip || !timeout || timeout > 60000 || ttl > 255){
        return ERROR(-EINVARG);
    }
    struct net_ping_result r = {0};
    long res = net_ping_ex(ip, timeout, (uint8_t)ttl, &r);
    if(res >= 0 && uout && copy_to_user(task_current()->process, uout, &r, sizeof(r)) < 0){
        res = -EFAULT;
    }
    return ERROR(res);
}

// ---- sockets (net/socket.h) ------------------------------------------
// A socket number is an index into one system-wide table of
// NET_SOCKETS (64) sockets, each with an owner: another process's
// number gets -EBADF. Buffers go through a kernel
// copy of at most SOCK_IO_MAX bytes per call (a larger send or recv
// moves that much and returns the count).

#define SOCK_IO_MAX   8192

static struct process* sys_process(void){
    return task_current()->process;
}

static long sys_put_addr(void* uaddr, struct sock_addr* a){
    if(uaddr && copy_to_user(sys_process(), uaddr, a, sizeof(*a)) < 0){
        return -EFAULT;
    }
    return 0;
}

// long socket(int type): 1 stream (TCP), 2 datagram (UDP).
void* isr80h_command62_socket(struct trap_frame* frame){
    return ERROR(socket_open(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0)));
}

// long bind(long s, unsigned port)
void* isr80h_command63_bind(struct trap_frame* frame){
    return ERROR(socket_bind(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0),
                             (uint32_t)(uintptr_t)isr80h_arg(frame, 1)));
}

// long connect(long s, unsigned ip, unsigned port): blocks for TCP.
void* isr80h_command64_connect(struct trap_frame* frame){
    return ERROR(socket_connect(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0),
                                (uint32_t)(uintptr_t)isr80h_arg(frame, 1),
                                (uint32_t)(uintptr_t)isr80h_arg(frame, 2)));
}

// long listen(long s, int backlog)
void* isr80h_command65_listen(struct trap_frame* frame){
    return ERROR(socket_listen(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0),
                               (int)(intptr_t)isr80h_arg(frame, 1)));
}

// long accept(long s, struct sock_addr* peer, unsigned timeout_ms): a
// new socket; peer may be NULL; timeout 0 waits for ever.
void* isr80h_command66_accept(struct trap_frame* frame){
    struct sock_addr peer = {0};
    long res = socket_accept(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0), &peer,
                             (uint32_t)(uintptr_t)isr80h_arg(frame, 2));
    if(res >= 0 && sys_put_addr(isr80h_arg(frame, 1), &peer) < 0){
        socket_close(sys_process(), (int)res);
        res = -EFAULT;
    }
    return ERROR(res);
}

// long sendto(long s, const void* buf, long len, unsigned ip, unsigned
// port): ip 0 sends to the connected peer. Bytes taken.
void* isr80h_command67_sendto(struct trap_frame* frame){
    long len = (long)(intptr_t)isr80h_arg(frame, 2);
    if(len < 0){
        return ERROR(-EINVARG);
    }
    if(len > SOCK_IO_MAX){
        len = SOCK_IO_MAX;
    }
    void* buf = kmalloc((size_t)len + 1);
    if(!buf){
        return ERROR(-ENOMEM);
    }
    net_scratch(buf);                       // Freed if it blocks
    long res = copy_from_user(sys_process(), buf, isr80h_arg(frame, 1), (size_t)len) < 0 ? -EFAULT :
               socket_sendto(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0), buf, (size_t)len,
                             (uint32_t)(uintptr_t)isr80h_arg(frame, 3), (uint32_t)(uintptr_t)isr80h_arg(frame, 4));
    net_scratch_done(buf);
    kfree(buf);
    return ERROR(res);
}

// long recvfrom(long s, void* buf, long len, struct sock_addr* from,
// unsigned timeout_ms): bytes, 0 at the end of a TCP stream.
void* isr80h_command68_recvfrom(struct trap_frame* frame){
    long len = (long)(intptr_t)isr80h_arg(frame, 2);
    if(len <= 0){
        return ERROR(-EINVARG);
    }
    if(len > SOCK_IO_MAX){
        len = SOCK_IO_MAX;
    }
    void* buf = kmalloc((size_t)len);
    if(!buf){
        return ERROR(-ENOMEM);
    }
    struct sock_addr from = {0};
    net_scratch(buf);                       // Freed if it blocks
    long res = socket_recvfrom(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0), buf, (size_t)len, &from,
                               (uint32_t)(uintptr_t)isr80h_arg(frame, 4));
    if(res > 0 && copy_to_user(sys_process(), isr80h_arg(frame, 1), buf, (size_t)res) < 0){
        res = -EFAULT;
    }
    if(res >= 0 && sys_put_addr(isr80h_arg(frame, 3), &from) < 0){
        res = -EFAULT;
    }
    net_scratch_done(buf);
    kfree(buf);
    return ERROR(res);
}

// long sclose(long s)
void* isr80h_command69_sclose(struct trap_frame* frame){
    return ERROR(socket_close(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0)));
}

// ---- poll, non-blocking sockets, addresses (net/socket.h) -----------

// long poll(struct sock_pollfd* fds, long n, long timeout_ms): how many
// entries have revents (written back), 0 when timeout_ms ran out; 0
// returns at once, -1 waits for ever. The table is copied onto the
// kernel stack (at most SOCK_POLL_MAX entries), so a wait leaves
// nothing behind.
void* isr80h_command71_poll(struct trap_frame* frame){
    void* ufds    = isr80h_arg(frame, 0);
    long  n       = (long)(intptr_t)isr80h_arg(frame, 1);
    long  timeout = (long)(intptr_t)isr80h_arg(frame, 2);
    struct sock_pollfd fds[SOCK_POLL_MAX];
    if(n < 0 || n > SOCK_POLL_MAX || timeout < -1 || timeout > 0x7fffffff){
        return ERROR(-EINVARG);
    }
    size_t size = sizeof(fds[0]) * (size_t)n;
    if(n && copy_from_user(sys_process(), fds, ufds, size) < 0){
        return ERROR(-EFAULT);
    }
    long res = socket_poll(sys_process(), fds, (int)n, (int32_t)timeout);
    if(res >= 0 && n && copy_to_user(sys_process(), ufds, fds, size) < 0){
        res = -EFAULT;
    }
    return ERROR(res);
}

// long nonblock(long s, long mode): 1 non-blocking, 0 blocking, -1 only
// asks; the mode now.
void* isr80h_command72_nonblock(struct trap_frame* frame){
    return ERROR(socket_nonblock(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0),
                                 (int)(intptr_t)isr80h_arg(frame, 1)));
}

static void* sys_name(struct trap_frame* frame, int peer){
    struct sock_addr a = {0};
    long res = socket_name(sys_process(), (int)(intptr_t)isr80h_arg(frame, 0), peer, &a);
    if(res >= 0){
        res = isr80h_arg(frame, 1) ? sys_put_addr(isr80h_arg(frame, 1), &a) : -EFAULT;
    }
    return ERROR(res);
}

// long getsockname(long s, struct sock_addr* out): the local address
// (ip 0 while not fixed) and port.
void* isr80h_command73_getsockname(struct trap_frame* frame){
    return sys_name(frame, 0);
}

// long getpeername(long s, struct sock_addr* out)
void* isr80h_command74_getpeername(struct trap_frame* frame){
    return sys_name(frame, 1);
}
