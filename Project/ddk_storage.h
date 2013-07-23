#ifndef DDK_STORAGE_H
#define DDK_STORAGE_H

#ifdef __KERNEL__
#include <linux/usb.h>

#define DDK_SECTOR_SIZE 512

extern int ddk_storage_init(struct usb_device *dev);
extern void ddk_storage_cleanup(struct usb_device *dev);
extern int ddk_storage_write(struct usb_device *dev, sector_t sector_off, u8 *buffer, unsigned int sectors);
extern int ddk_storage_read(struct usb_device *dev, sector_t sector_off, u8 *buffer, unsigned int sectors);
#endif

#endif
