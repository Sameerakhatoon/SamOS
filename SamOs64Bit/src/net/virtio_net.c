// virtio-net NIC driver (virtio 1.x, spec 5.1).
//
// Queue 0 receives, queue 1 transmits; no control queue, no offloads,
// no mergeable buffers. With VERSION_1 every buffer starts with the
// 12-byte struct virtio_net_hdr (num_buffers included), which a
// transmit leaves zero and a receive skips. Buffers are 2 KiB, two to
// a DMA page, enough for the header and a 1514-byte frame. The receive
// queue is kept full; a transmit copies the frame into a free transmit
// buffer, which comes back when the device has used it (the transmit
// queue asks for no interrupts: VIRTQ_AVAIL_F_NO_INTERRUPT).
//
// The MAC comes from config space (VIRTIO_NET_F_MAC), else a locally
// administered one is made up.
//
// Interrupts: MSI-X where the function and the platform have it (x86,
// the aarch64 GICv3 ITS, the riscv64 IMSIC), on completion slot
// VNET_MSI_SLOT; else the virtio line (PCI INTx, or the MMIO slot's
// interrupt). Either handler only calls net_irq, which hands the work
// to the BSP (netdev.h). Without either, or with samos.net_poll, the
// BSP's tick polls (net_tick); samos.net_intx skips MSI-X.
//
// Everything here runs under the net lock except the probe, which
// runs at boot before anything else uses the device.

#include "virtio_net.h"
#include "netdev.h"
#include "virtio/virtio.h"
#include "virtio/virtio_bus.h"
#include "virtio/virtio_io.h"
#include "cpu/percpu.h"
#include "boot/cmdline.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include "io/serial.h"
#include "string/string.h"
#include "status.h"

#define VIRTIO_NET_F_MAC        5
#define VIRTIO_NET_F_STATUS     16

#define VNET_CFG_MAC            0
#define VNET_CFG_STATUS         6
#define VNET_STATUS_LINK_UP     1

#define VNET_RXQ                0
#define VNET_TXQ                1
#define VNET_BUFS               32          // per queue
#define VNET_BUF_SIZE           2048
#define VNET_HDR_SIZE           12
#define VNET_MAX_DEVICES        2
#define VNET_MSI_SLOT           14          // disk/irqwait.h: 0 AHCI, 2-3 PATA, 4-11 virtio INTx, 12-13 xHCI
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

struct vnet {
    struct virtio_dev* vdev;
    struct virtq       rxq, txq;
    struct netdev      nd;
    uint8_t*           rx[VNET_BUFS];
    uint64_t           rx_phys[VNET_BUFS];
    uint8_t*           tx[VNET_BUFS];
    uint64_t           tx_phys[VNET_BUFS];
    uint8_t            rx_of_head[VIRTQ_MAX_SIZE];     // buffer behind a descriptor
    uint8_t            tx_of_head[VIRTQ_MAX_SIZE];
    uint8_t            tx_free[VNET_BUFS];
    int                tx_free_count;
    int                rx_count;
};

static int vnet_count;

static void vnet_post_rx(struct vnet* v, int b){
    struct virtq_buf buf = { v->rx_phys[b], VNET_BUF_SIZE, true };
    virtio_dma_flush(v->rx[b], VNET_BUF_SIZE);
    int head = virtq_add(&v->rxq, &buf, 1);
    if(head >= 0){
        v->rx_of_head[head] = (uint8_t)b;
    }
}

static void vnet_reclaim_tx(struct vnet* v){
    uint32_t head;
    while(virtq_pop_used(&v->txq, &head, NULL)){
        v->tx_free[v->tx_free_count++] = v->tx_of_head[head];
    }
}

static void vnet_poll(struct netdev* nd){
    struct vnet* v = nd->priv;
    uint32_t head, len;
    bool any = false;
    while(virtq_pop_used(&v->rxq, &head, &len)){
        int b = v->rx_of_head[head];
        virtio_dma_inval(v->rx[b], VNET_BUF_SIZE);
        if(len > VNET_HDR_SIZE && len <= VNET_BUF_SIZE){
            netdev_rx(nd, v->rx[b] + VNET_HDR_SIZE, len - VNET_HDR_SIZE);
        }
        vnet_post_rx(v, b);
        any = true;
    }
    if(any){
        virtq_kick(&v->rxq);
    }
    vnet_reclaim_tx(v);
}

static int vnet_xmit(struct netdev* nd, const void* frame, uint32_t len){
    struct vnet* v = nd->priv;
    vnet_reclaim_tx(v);
    if(!v->tx_free_count || len + VNET_HDR_SIZE > VNET_BUF_SIZE){
        return -ENOSPC;
    }
    int b = v->tx_free[--v->tx_free_count];
    memset(v->tx[b], 0, VNET_HDR_SIZE);
    memcpy(v->tx[b] + VNET_HDR_SIZE, (void*)frame, (int)len);
    virtio_dma_clean(v->tx[b], VNET_HDR_SIZE + len);
    struct virtq_buf buf = { v->tx_phys[b], VNET_HDR_SIZE + len, false };
    int head = virtq_add(&v->txq, &buf, 1);
    if(head < 0){
        v->tx_free[v->tx_free_count++] = (uint8_t)b;
        return head;
    }
    v->tx_of_head[head] = (uint8_t)b;
    virtq_kick(&v->txq);
    return 0;
}

static void vnet_msi(struct trap_frame* frame){
    net_irq();
}

static void vnet_line(void* ctx){
    net_irq();
}

// Two buffers per page, `n` buffers.
static int vnet_alloc_bufs(uint8_t** bufs, uint64_t* phys, int n){
    for(int i = 0; i < n; i += 2){
        uint8_t* page = virtio_dma_page();
        if(!page){
            return -ENOMEM;
        }
        bufs[i] = page;
        bufs[i + 1] = page + VNET_BUF_SIZE;
        phys[i] = virtio_dma_phys(page);
        phys[i + 1] = phys[i] + VNET_BUF_SIZE;
    }
    return 0;
}

static void vnet_free(struct vnet* v){
    for(int i = 0; i < VNET_BUFS; i += 2){
        virtio_dma_free(v->rx[i]);
        virtio_dma_free(v->tx[i]);
    }
    kfree(v);
}

static const char hexdigits[] = "0123456789abcdef";

static void vnet_log(struct vnet* v, const char* irq, int line){
    char mac[18];
    for(int i = 0; i < 6; i++){
        mac[i * 3]     = hexdigits[v->nd.mac[i] >> 4];
        mac[i * 3 + 1] = hexdigits[v->nd.mac[i] & 15];
        mac[i * 3 + 2] = i < 5 ? ':' : 0;
    }
    serial_write("virtio-net: ");
    serial_write(v->nd.name);
    serial_write(" ");
    serial_write(mac);
    serial_write(", ");
    serial_write(v->vdev->transport);
    serial_write(", ");
    serial_write(irq);
    if(line >= 0){
        serial_write(" ");
        serial_write(itoa(line));
    }
    if(virtio_has(v->vdev, VIRTIO_NET_F_STATUS)){
        bool up = v->vdev->ops->cfg_read8(v->vdev, VNET_CFG_STATUS) & VNET_STATUS_LINK_UP;
        serial_write(up ? ", link up" : ", link down");
    }
    serial_write(", queues ");
    serial_write(itoa(v->rxq.size));
    serial_write("/");
    serial_write(itoa(v->txq.size));
    serial_write("\n");
}

static int vnet_attach(struct virtio_dev* d, void* ctx){
    if(vnet_count == VNET_MAX_DEVICES){
        return -ENOSPC;
    }
    int res = virtio_negotiate(d, (1ULL << VIRTIO_NET_F_MAC) | (1ULL << VIRTIO_NET_F_STATUS));
    if(res < 0){
        return res;
    }
    struct vnet* v = kzalloc(sizeof(*v));
    if(!v){
        return -ENOMEM;
    }
    v->vdev = d;
    // MSI-X is set up between the negotiation (its reset clears the
    // vectors) and the queues (each takes vector 0 before it is enabled).
    const char* irq = "polled";
    int line = -1;
    bool poll = boot_cmdline_has("samos.net_poll");
    if(!poll && !boot_cmdline_has("samos.net_intx")){
        line = virtio_msix(d, VNET_MSI_SLOT + vnet_count, this_cpu(), vnet_msi);
        if(line >= 0){
            irq = "MSI-X vector";
        }
    }
    if(vnet_alloc_bufs(v->rx, v->rx_phys, VNET_BUFS) < 0 || vnet_alloc_bufs(v->tx, v->tx_phys, VNET_BUFS) < 0 ||
       virtq_setup(d, &v->rxq, VNET_RXQ, VNET_BUFS) < 0 || virtq_setup(d, &v->txq, VNET_TXQ, VNET_BUFS) < 0){
        virtio_reset(d);
        vnet_free(v);
        return -EIO;
    }
    v->txq.avail->flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
    v->rx_count = v->rxq.size < VNET_BUFS ? v->rxq.size : VNET_BUFS;
    for(int b = 0; b < v->rx_count; b++){
        vnet_post_rx(v, b);
    }
    int tx_count = v->txq.size < VNET_BUFS ? v->txq.size : VNET_BUFS;
    for(int b = 0; b < tx_count; b++){
        v->tx_free[v->tx_free_count++] = (uint8_t)b;
    }

    if(virtio_has(d, VIRTIO_NET_F_MAC)){
        for(int i = 0; i < 6; i++){
            v->nd.mac[i] = d->ops->cfg_read8(d, VNET_CFG_MAC + i);
        }
    } else {
        static const uint8_t made_up[6] = { 0x02, 'S', 'a', 'm', 'O', 0 };
        memcpy(v->nd.mac, (void*)made_up, 6);
        v->nd.mac[5] = (uint8_t)vnet_count;
    }
    v->nd.priv = v;
    v->nd.xmit = vnet_xmit;
    v->nd.poll = vnet_poll;
    if(netdev_register(&v->nd) < 0){
        virtio_reset(d);
        vnet_free(v);
        return -ENOSPC;
    }
    vnet_count++;
    virtio_driver_ok(d);
    virtq_kick(&v->rxq);

    if(line < 0 && !poll){
        struct virtio_line* l = virtio_irq(d, false, vnet_line, v);
        if(l){
            irq = "line, GSI";
            line = (int)l->gsi;
        }
    }
    v->nd.irq = line >= 0 ? (d->msix ? "MSI-X" : "line") : "polled";
    vnet_log(v, irq, line);
    return 0;
}

int virtio_net_init(void){
    return virtio_probe(VIRTIO_ID_NET, vnet_attach, NULL);
}
