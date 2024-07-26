// Network syscalls. The work is net/net.c's.

#include "isr80h/isr80h.h"
#include "net.h"
#include "net/net.h"
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
