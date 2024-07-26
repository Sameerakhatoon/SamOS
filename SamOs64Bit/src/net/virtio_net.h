#ifndef NET_VIRTIO_NET_H
#define NET_VIRTIO_NET_H
// virtio-net NIC driver (virtio 1.x, spec 5.1).

// Find every virtio-net device (PCI and MMIO) and register it as a
// netdev (eth0, eth1, ...). Returns how many.
int virtio_net_init(void);

#endif
