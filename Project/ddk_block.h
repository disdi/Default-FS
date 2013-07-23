#ifndef DDK_BLOCK_H
#define DDK_BLOCK_H

#ifdef __KERNEL__
#include <linux/usb.h>

int block_register_dev(struct usb_interface *interface);
void block_deregister_dev(struct usb_interface *interface);
#endif

#endif
