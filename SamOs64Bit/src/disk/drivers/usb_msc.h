#ifndef USB_MSC_H
#define USB_MSC_H
// USB mass storage (bulk-only, SCSI) as a struct disk driver
// (usb_msc.c).

struct disk_driver;
struct disk_driver* usb_msc_driver_init(void);

#endif
