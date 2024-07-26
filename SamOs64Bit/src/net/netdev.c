// Network devices, packet buffers, the loopback and the net
// lock. See netdev.h.

#include "netdev.h"
#include "virtio_net.h"
#include "arch/arch.h"
#include "cpu/percpu.h"
#include "cpu/smp.h"
#include "task/task.h"
#include "lib/spinlock/spinlock.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "string/string.h"
#include "io/serial.h"
#include "status.h"

#define NET_BACKLOG_MAX     256

static struct spinlock net_spinlock = SPINLOCK_INIT("net", LOCK_LEVEL_NET, 0);
static struct netdev* netdevs[NETDEV_MAX];
static int netdev_total;
static int eth_total;
static struct pkbuf* backlog_head;
static struct pkbuf* backlog_tail;
static int backlog_len;
static struct cpu* net_bsp;

void net_lock(void){
    spin_lock(&net_spinlock);
}

void net_unlock(void){
    spin_unlock(&net_spinlock);
}

bool net_locked(void){
    return spin_held(&net_spinlock);
}

uint64_t net_now_ms(void){
    return arch_uptime_us() / 1000;
}

struct pkbuf* pkbuf_alloc(void){
    struct pkbuf* p = kmalloc(sizeof(*p));
    if(p){
        p->next = NULL;
        p->dev  = NULL;
        p->data = p->buf + PKBUF_HEADROOM;
        p->len  = 0;
    }
    return p;
}

void pkbuf_free(struct pkbuf* p){
    kfree(p);
}

int netdev_register(struct netdev* d){
    if(netdev_total == NETDEV_MAX){
        return -ENOSPC;
    }
    if(d->loopback){
        strcpy(d->name, "lo");
    } else {
        strcpy(d->name, "eth");
        strcpy(d->name + 3, itoa(eth_total++));
    }
    if(!d->mtu){
        d->mtu = NET_MTU;
    }
    netdevs[netdev_total++] = d;
    return 0;
}

int netdev_count(void){
    return netdev_total;
}

struct netdev* netdev_get(int i){
    return i >= 0 && i < netdev_total ? netdevs[i] : NULL;
}

struct netdev* netdev_by_name(const char* name){
    for(int i = 0; i < netdev_total; i++){
        if(strncmp(netdevs[i]->name, name, NETDEV_NAME_SIZE) == 0){
            return netdevs[i];
        }
    }
    return NULL;
}

void netdev_rx(struct netdev* d, const void* frame, uint32_t len){
    struct pkbuf* p = len <= NET_ETH_FRAME_MAX && backlog_len < NET_BACKLOG_MAX ? pkbuf_alloc() : NULL;
    if(!p){
        d->rx_dropped++;
        return;
    }
    memcpy(p->data, (void*)frame, (int)len);
    p->len = len;
    p->dev = d;
    if(backlog_tail){
        backlog_tail->next = p;
    } else {
        backlog_head = p;
    }
    backlog_tail = p;
    backlog_len++;
    d->rx_packets++;
    d->rx_bytes += len;
}

int netdev_xmit(struct netdev* d, const void* frame, uint32_t len){
    if(len > NET_ETH_FRAME_MAX || d->xmit(d, frame, len) < 0){
        d->tx_dropped++;
        return -EIO;
    }
    d->tx_packets++;
    d->tx_bytes += len;
    return 0;
}

void net_poll(void){
    for(int i = 0; i < netdev_total; i++){
        if(netdevs[i]->poll){
            netdevs[i]->poll(netdevs[i]);
        }
    }
    // A frame the stack answers on the loopback lands at the tail and
    // is taken in the same loop.
    while(backlog_head){
        struct pkbuf* p = backlog_head;
        backlog_head = p->next;
        if(!backlog_head){
            backlog_tail = NULL;
        }
        backlog_len--;
        net_stack_input(p->dev, p->data, p->len);
        pkbuf_free(p);
    }
}

// ARCH_IPI_INPUT on the BSP, after a NIC interrupt.
static void net_input_work(void){
    net_lock();
    net_poll();
    net_unlock();
}

void net_irq(void){
    arch_send_ipi(net_bsp, ARCH_IPI_INPUT);
}

// Every BSP tick: devices without an interrupt, and the stack's timers.
static void net_tick(void){
    net_lock();
    net_poll();
    net_stack_timer();
    net_unlock();
}

// ---- Loopback --------------------------------------------------------

static int loopback_xmit(struct netdev* d, const void* frame, uint32_t len){
    netdev_rx(d, frame, len);
    return 0;
}

static struct netdev loopback_dev = {
    .loopback = true,
    .mtu      = NET_MTU,
    .irq      = "none",
    .xmit     = loopback_xmit,
};

// On the BSP before the APs start, interrupts off: nothing else runs
// the stack yet, so the probe (which maps and routes) is not under the
// net lock.
void net_init(void){
    net_bsp = this_cpu();
    virtio_net_init();
    netdev_register(&loopback_dev);
    net_stack_init();
    smp_input_register(net_input_work);
    task_clock_register(net_tick);
}
