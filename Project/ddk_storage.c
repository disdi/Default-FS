#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/errno.h>

#include "ddk_storage.h"
#include "ddk_block.h"

#define DDK_VENDOR_ID 0x16c0
#define DDK_PRODUCT_ID 0x05dc

#define CUSTOM_RQ_SET_MEM_RD_OFFSET 3
#define CUSTOM_RQ_GET_MEM_RD_OFFSET 4
#define CUSTOM_RQ_SET_MEM_WR_OFFSET 5
#define CUSTOM_RQ_GET_MEM_WR_OFFSET 6
#define CUSTOM_RQ_GET_MEM_SIZE      7
#define CUSTOM_RQ_SET_MEM_TYPE      8
#define CUSTOM_RQ_GET_MEM_TYPE      9

#define MEM_EP_IN (USB_DIR_IN | 0x01)
#define MEM_EP_OUT 0x01

#define MAX_PKT_SIZE 8

enum
{
	eeprom,
	flash,
	total_mem_type
};

enum
{
	e_read,
	e_write
};

static int set_mem(struct usb_device *dev)
{
	/* Control OUT */
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			CUSTOM_RQ_SET_MEM_TYPE, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			flash, 0, NULL, 0, USB_CTRL_GET_TIMEOUT);
}

static int get_size(struct usb_device *dev)
{
	int retval;
	short val;

	/* Control IN */
	retval = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			CUSTOM_RQ_GET_MEM_SIZE, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, 0, &val, sizeof(val), USB_CTRL_GET_TIMEOUT);
	if (retval == 2)
	{
		return val;
	}
	else
	{
		return DDK_SECTOR_SIZE; // Just one sector
	}
}

#if 0
static int get_off(struct usb_device *dev, int dir)
{
	int retval;
	short val;

	/* Control IN */
	retval = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			(dir == e_read) ? CUSTOM_RQ_GET_MEM_RD_OFFSET : CUSTOM_RQ_GET_MEM_WR_OFFSET,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, 0, &val, sizeof(val), USB_CTRL_GET_TIMEOUT);

	if (retval == 2)
	{
		return val;
	}
	else
	{
		return (retval < 0) ? retval : -EINVAL;
	}
}
#endif
static int set_off(struct usb_device *dev, int dir, int offset)
{
	/* Control OUT */
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			(dir == e_read) ? CUSTOM_RQ_SET_MEM_RD_OFFSET : CUSTOM_RQ_SET_MEM_WR_OFFSET,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			offset, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
}

int ddk_storage_init(struct usb_device *dev)
{
	int retval;
	
	if ((retval = set_mem(dev)) < 0) // Setting memory type to flash
	{
		return retval;
	}
	else
	{
		if ((retval = get_size(dev)) < 0)
			return retval;
		else
			return retval / DDK_SECTOR_SIZE;
	}
}
void ddk_storage_cleanup(struct usb_device *dev)
{
}
int ddk_storage_write(struct usb_device *dev, sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	int offset = sector_off * DDK_SECTOR_SIZE;
	int cnt = sectors * DDK_SECTOR_SIZE;
	int wrote_cnt, wrote_size;
	int retval;

	//printk(KERN_DEBUG "ddkb: Wr:S:C:O - %Ld:%d:%d\n", sector_off, sectors, get_off(dev, e_write));
	if ((retval = set_off(dev, e_write, offset)) < 0)
	{
		printk(KERN_ERR "ddkb: Set Off Error: %d\n", retval);
		return retval;
	}
	wrote_cnt = 0;
	while (wrote_cnt < cnt)
	{
		/* Send the data out the int endpoint */
		retval = usb_interrupt_msg(dev, usb_sndintpipe(dev, MEM_EP_OUT),
			buffer + wrote_cnt, MAX_PKT_SIZE, &wrote_size, 0);
		if (retval)
		{
			printk(KERN_ERR "ddkb: Interrupt message returned %d\n", retval);
			return retval;
		}
		if (wrote_size == 0)
		{
			break;
		}
		wrote_cnt += wrote_size;
	}
	printk(KERN_INFO "ddkb: Wrote %d bytes\n", wrote_cnt);
	msleep(100);

	return wrote_cnt;
}
int ddk_storage_read(struct usb_device *dev, sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	int offset = sector_off * DDK_SECTOR_SIZE;
	int cnt = sectors * DDK_SECTOR_SIZE;
	int read_cnt, read_size;
	int retval;

	//printk(KERN_DEBUG "ddkb: Rd:S:C:O - %Ld:%d:%d\n", sector_off, sectors, get_off(dev, e_read));
	if ((retval = set_off(dev, e_read, offset)) < 0)
	{
		printk(KERN_ERR "ddkb: Set Off Error: %d\n", retval);
		return retval;
	}
	read_cnt = 0;
	while (read_cnt < cnt)
	{
		/* Read the data in the int endpoint */
		retval = usb_interrupt_msg(dev, usb_rcvintpipe(dev, MEM_EP_IN),
			buffer + read_cnt, MAX_PKT_SIZE, &read_size, 0);
		if (retval)
		{
			printk(KERN_ERR "ddkb: Interrupt message returned %d\n", retval);
			return retval;
		}
		if (read_size == 0)
		{
			break;
		}
		read_cnt += read_size;
	}
	printk(KERN_INFO "ddkb: Read %d bytes\n", read_cnt);

	return read_cnt;
}

static int ddk_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	return block_register_dev(interface);
}

static void ddk_disconnect(struct usb_interface *interface)
{
	block_deregister_dev(interface);
}

/* Table of devices that work with this driver */
static struct usb_device_id ddk_table[] =
{
	{
		USB_DEVICE(DDK_VENDOR_ID, DDK_PRODUCT_ID)
	},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, ddk_table);

static struct usb_driver ddk_driver =
{
	.name = "ddk_block",
	.probe = ddk_probe,
	.disconnect = ddk_disconnect,
	.id_table = ddk_table,
};

static int __init ddk_init(void)
{
	int result;

	/* Register this driver with the USB subsystem */
	if ((result = usb_register(&ddk_driver)))
	{
		printk(KERN_ERR "usb_register failed. Error number %d\n", result);
	}
	printk(KERN_INFO "ddkb: DDK usb_registered\n");
	return result;
}

static void __exit ddk_exit(void)
{
	/* Deregister this driver with the USB subsystem */
	usb_deregister(&ddk_driver);
	printk(KERN_INFO "ddkb: DDK usb_deregistered\n");
}

module_init(ddk_init);
module_exit(ddk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("USB Block Device Driver for DDK v1.1");
