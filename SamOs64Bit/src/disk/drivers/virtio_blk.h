#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H
// virtio-blk as a struct disk driver (virtio_blk.c).

struct disk_driver;
struct disk_driver* virtio_blk_driver_init(void);

#endif
