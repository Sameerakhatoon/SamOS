#ifndef NET_NETDEV_H
#define NET_NETDEV_H
// Network devices and packet buffers. Not in the book.
//
// A netdev is one NIC (virtio-net) or the loopback. Its driver gives
// transmit (a whole Ethernet frame, copied out before the call
// returns) and poll (take what the device received, hand each frame to
// netdev_rx, give the buffers back). Received frames become pkbufs on
// one backlog; net_poll drains it into the stack (SamTcpIp's
// pkt_receive, net/samtcpip/comm.c) after every device was polled, so
// a frame the stack sends to itself (the loopback) is queued, never
// processed inside its own send.
//
// One lock, `net` (docs/locking.md, level 48), covers the devices, the
// backlog and the whole stack above them. It is taken by the network
// and socket syscalls, the BSP's tick hook (net_tick: polling and the
// stack's timers) and the NIC interrupt's work: the interrupt handler
// itself only sends ARCH_IPI_INPUT to the BSP, which runs net_poll
// outside any disk wait (as virtio-input).

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NETDEV_MAX          5           // Four virtio-net NICs and lo
#define NETDEV_NAME_SIZE    16          // SamTcpIp's IF_NAME_SIZE
#define NET_ETH_FRAME_MAX   1514        // without the FCS, which the NIC adds
#define NET_MTU             1500

// A packet buffer: one received (or looped back) frame. Data starts at
// `data`, `len` bytes; `buf` leaves room in front for a header pushed
// later (a VLAN tag, SamTcpIp's tag_pkt_with_vlan_id).
#define PKBUF_HEADROOM      64
#define PKBUF_SIZE          (PKBUF_HEADROOM + NET_ETH_FRAME_MAX + 6)
struct netdev;
struct pkbuf {
    struct pkbuf*  next;
    struct netdev* dev;                 // the interface it came in on
    uint8_t*       data;
    uint32_t       len;
    uint8_t        buf[PKBUF_SIZE];
};

struct pkbuf* pkbuf_alloc(void);
void          pkbuf_free(struct pkbuf* p);

struct netdev {
    char        name[NETDEV_NAME_SIZE]; // "eth0", "lo"
    uint8_t     mac[6];
    uint16_t    mtu;
    bool        loopback;
    const char* irq;                    // "MSI-X", "line", "polled", "none" for ifconfig
    // A whole Ethernet frame. 0, or < 0 when it was dropped.
    int  (*xmit)(struct netdev* d, const void* frame, uint32_t len);
    void (*poll)(struct netdev* d);
    void*       priv;                   // the driver's
    void*       intf;                   // the stack's Interface
    uint64_t    rx_packets, rx_bytes, rx_dropped;
    uint64_t    tx_packets, tx_bytes, tx_dropped;
};

// Give `d` a name (eth0, eth1, ... or lo) and add it. 0 or -ENOSPC.
int            netdev_register(struct netdev* d);
int            netdev_count(void);
struct netdev* netdev_get(int i);
struct netdev* netdev_by_name(const char* name);

// For the drivers, under the net lock: one received frame, copied.
void netdev_rx(struct netdev* d, const void* frame, uint32_t len);
// For the stack, under the net lock: send one frame on `d`.
int  netdev_xmit(struct netdev* d, const void* frame, uint32_t len);

void net_lock(void);
void net_unlock(void);
bool net_locked(void);

// Poll every device and run the backlog through the stack. Under the
// net lock.
void net_poll(void);
// A NIC's interrupt handler: hand the work to the BSP (ARCH_IPI_INPUT).
void net_irq(void);
// Milliseconds of the network's clock (the BSP's tick).
uint64_t net_now_ms(void);

// Bring up the drivers, the loopback and the stack (kernel_main, after
// PCI and before the APs start).
void net_init(void);

// The stack's side (net/net.c): a frame for it, and its timers.
void net_stack_input(struct netdev* d, uint8_t* frame, uint32_t len);
void net_stack_timer(void);
void net_stack_init(void);

#endif
