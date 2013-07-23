/* Block Driver over DDK */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/usb.h>
#include <linux/fs.h> // ioctl cmds are also here
#include <linux/types.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h> // For at least, struct block_device_operations
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/workqueue.h> // For workqueue related functionalities
#include <linux/errno.h>
#include <asm/atomic.h> // For atomic_t, ...

#include "ddk_block.h"
#include "ddk_storage.h"

#define DDK_FIRST_MINOR 0
#define DDK_MINOR_CNT 16

/* 
 * The internal structure representation of our Device
 */
struct ddk_device
{
	/* USB Device of this interface */
	struct usb_device *device;
	u_int major;
	/* Size is the size of the device (in sectors) */
	unsigned int size;
	/* For access protection of the request queue */
	spinlock_t lock;
	/* And the Request Queue */
	struct request_queue *queue;
	/* This is kernel's representation of an individual disk device */
	struct gendisk *disk;
	/* Data structures used in request processing work */
	atomic_t under_process; /* Allows only one read / write at a time */
	struct request *current_req;
	struct work_struct work;
};

static int ddk_open(struct block_device *bdev, fmode_t mode)
{
	unsigned unit = iminor(bdev->bd_inode);

	printk(KERN_INFO "ddkb: Device is opened\n");
	printk(KERN_INFO "ddkb: Inode number is %d\n", unit);

	if (unit > DDK_MINOR_CNT)
		return -ENODEV;
	return 0;
}

static int ddk_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "ddkb: Device is closed\n");
	return 0;
}

static void ddk_geo_fill(struct hd_geometry *geo)
{
	/* TODO: Try tuning */
	geo->heads = 1;
	geo->cylinders = 1;
	geo->sectors = 1;
	geo->start = 0;
}

static int ddk_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg)
{
	struct ddk_device *ddk_dev = (struct ddk_device *)bdev->bd_disk->private_data;
	long lval;
	u64 dval;
	size_t sval;
	int val;
	struct hd_geometry geo;

	switch (cmd)
	{
		case BLKGETSIZE:
			lval = (long)ddk_dev->size;
			if (copy_to_user((long *)arg, &lval, sizeof(long)))
			{
				return -EFAULT;
			}
			break;
		case BLKGETSIZE64:
			dval = 0; /* TODO: Block device size in bytes */
			if (copy_to_user((u64 *)arg, &dval, sizeof(u64)))
			{
				return -EFAULT;
			}
			break;
		case BLKSSZGET:
			sval = (size_t)DDK_SECTOR_SIZE;
			if (copy_to_user((size_t *)arg, &sval, sizeof(size_t)))
			{
				return -EFAULT;
			}
			break;
		case BLKBSZGET:
			val = DDK_SECTOR_SIZE;
			if (copy_to_user((int *)arg, &val, sizeof(int)))
			{
				return -EFAULT;
			}
			break;
		case HDIO_GETGEO:
			ddk_geo_fill(&geo);
			if (copy_to_user((struct hd_geometry *)arg, &geo, sizeof(struct hd_geometry)))
			{
				return -EFAULT;
			}
			break;
		default:
			return -EINVAL;
			break;
	}
	return 0;
}

static int ddk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	ddk_geo_fill(geo);
	return 0;
}

static void ddk_transfer(struct work_struct *w)
{
	struct ddk_device *ddk_dev = container_of(w, struct ddk_device, work);
	struct request *req = ddk_dev->current_req;
	int dir = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);

	struct bio_vec *bv;
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;

	int ret = 0, ret2;

	//printk(KERN_DEBUG "ddkb: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		buffer = page_address(bv->bv_page) + bv->bv_offset;
		if (bv->bv_len % DDK_SECTOR_SIZE != 0)
		{
			printk(KERN_ERR "ddkb: Should never happen: "
				"bio size (%d) is not a multiple of DDK_SECTOR_SIZE (%d).\n"
				"This may lead to data truncation.\n",
				bv->bv_len, DDK_SECTOR_SIZE);
			ret = -EIO;
		}
		sectors = bv->bv_len / DDK_SECTOR_SIZE;
		printk(KERN_DEBUG "ddkb: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
			sector_offset, buffer, sectors);

		if (dir == WRITE) /* TODO: Write to the device */
		{
			//ret2 = 
		}
		else /* TODO: Read from the device */
		{
			//ret2 = 
		}

		if (ret2 < 0)
		{
			ret = ret2;
		}
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "ddkb: bio info doesn't match with the request info");
		ret = -EIO;
	}

	blk_end_request_all(req, ret); // Servicing the request done
	atomic_set(&ddk_dev->under_process, 0);
}

static int ddk_initiate_transfer(struct request *req)
{
	struct ddk_device *ddk_dev = (struct ddk_device *)(req->rq_disk->private_data);
	int ret;

	atomic_set(&ddk_dev->under_process, 1);
	ddk_dev->current_req = req;
	if ((ret = schedule_work(&ddk_dev->work)) < 0)
	{
		atomic_set(&ddk_dev->under_process, 0);
		return ret;
	}
	return 0;
}

/*
 * Represents a block I/O request for us to process.
 * Caller of this would have taken the spin_lock on the queue.
 * So, use all the no spin_lock'ing functions
 */
static void ddk_request(struct request_queue *q)
{
	struct request *req = blk_peek_request(q);
	struct ddk_device *ddk_dev;
	int ret;

	if (!req)
		return;
	else
		ddk_dev = (struct ddk_device *)(req->rq_disk->private_data);

	if (atomic_read(&ddk_dev->under_process)) // Already under process, come again later
	{
		return;
	}
	/*
	 * The above 'test' & then 'set' in ddk_initiate_transfer need not be atomic,
	 * as this func is non-reentrant bcoz of request queue's spin lock
	 */

	/* Gets the current request from the dispatch queue */
	while ((req = blk_fetch_request(q)) != NULL)
	{
#if 0
		/*
		 * This function tells us whether we are looking at a filesystem request
		 * - one that moves block of data
		 */
		if (!blk_fs_request(req))
		{
			printk(KERN_NOTICE "ddkb: Skip non-fs request\n");
			/* We pass 0 to indicate that we successfully completed the request */
			__blk_end_request_all(req, 0);
			continue;
		}
#endif
		/*
		 * The following call initiates the transfer & returns asynchronously,
		 * as we can't yield here, as request functions doesn't execute in any
		 * process context - in fact they may get executed in interrupt context
		 */
		ret = ddk_initiate_transfer(req);
		if (ret < 0) // Transfer not possible: Finish off this request & continue
		{
			__blk_end_request_all(req, ret);
			//__blk_end_request(req, ret, blk_rq_bytes(req)); // Partial request served
		}
		else
		/*
		 * Request under processing - come again later. Also, the completing of
		 * the request using *blk_end_request* will be done once the request is
		 * actually processed
		 */
		{
			return;
		}
	}
}

/* 
 * These are the file operations that performed on the ddk block device
 */
static struct block_device_operations ddk_fops =
{
	.owner = THIS_MODULE,
	.open = ddk_open,
	.release = ddk_close,
	.ioctl = ddk_ioctl,
	.getgeo = ddk_getgeo
};
	
/* 
 * This is the registration and initialization section of the ddk block device
 * driver
 */
int block_register_dev(struct usb_interface *interface)
{
	struct ddk_device *ddk_dev;
	int ret;

	if ((ddk_dev = (struct ddk_device *)(kzalloc(sizeof(struct ddk_device), GFP_KERNEL))) == NULL)
		return -ENOMEM;
	/* TODO: Initialize with USB device */
	ddk_dev->device = NULL;
	/*
	 * Set up the request processing work related data structures as the first
	 * thing, as the request processing would get triggered internally by call
	 * of some of the function(s) below, esp. add_disk
	 */
	atomic_set(&ddk_dev->under_process, 0);
	ddk_dev->current_req = NULL;
	INIT_WORK(&ddk_dev->work, ddk_transfer);

	/* Set up our DDK Storage */
	if ((ret = ddk_storage_init(ddk_dev->device)) < 0)
	{
		kfree(ddk_dev);
		return ret;
	}
	/* TODO: Initialize with DDK memory size */
	ddk_dev->size = 

	/* Get Registered */
	ddk_dev->major = register_blkdev(0, "ddk");
	if (ddk_dev->major <= 0)
	{
		printk(KERN_ERR "ddkb: Unable to get Major Number\n");
		ddk_storage_cleanup(ddk_dev->device);
		kfree(ddk_dev);
		return -EBUSY;
	}

	/* Get a request queue (here queue is created) */
	spin_lock_init(&ddk_dev->lock);
	ddk_dev->queue = blk_init_queue(ddk_request, &ddk_dev->lock);
	if (ddk_dev->queue == NULL)
	{
		printk(KERN_ERR "ddkb: blk_init_queue failure\n");
		unregister_blkdev(ddk_dev->major, "ddk");
		ddk_storage_cleanup(ddk_dev->device);
		kfree(ddk_dev);
		return -ENOMEM;
	}
	
	/*
	 * Add the gendisk structure
	 * By using this memory allocation is involved, 
	 * the minor number we need to pass bcz the device 
	 * will support this much partitions 
	 */
	ddk_dev->disk = alloc_disk(DDK_MINOR_CNT);
	if (!ddk_dev->disk)
	{
		printk(KERN_ERR "ddkb: alloc_disk failure\n");
		blk_cleanup_queue(ddk_dev->queue);
		unregister_blkdev(ddk_dev->major, "ddk");
		ddk_storage_cleanup(ddk_dev->device);
		kfree(ddk_dev);
		return -ENOMEM;
	}

 	/* TODO: Setting the major number */
	ddk_dev->disk->major = 0;
  	/* Setting the first minor number */
	ddk_dev->disk->first_minor = DDK_FIRST_MINOR;
 	/* Setting the block device operations */
	ddk_dev->disk->fops = &ddk_fops;
 	/* Driver-specific own internal data */
	ddk_dev->disk->private_data = &ddk_dev;
 	/* TODO: Setting up the request queue */
	ddk_dev->disk->queue = NULL;
	/*
	 * You do not want partition information to show up in 
	 * cat /proc/partitions set this flags
	 */
	//ddk_dev->disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(ddk_dev->disk->disk_name, "ddk");
	
	/*
	 * This function sets the capacity of the device in its gendisk structure.
	 * Prototype of this function is in linux/genhd.h.
	 * ddk_dev->disk->part0.nr_sects = size;
	 */
	set_capacity(ddk_dev->disk, ddk_dev->size);

	/* For accessing the ddk_dev, from anywhere */
	ddk_dev->disk->private_data = ddk_dev;
	/* Adding the disk to the system. Now the disk is "live" */
	add_disk(ddk_dev->disk);
	printk(KERN_INFO "ddkb: DDK Block driver initialised (%d sectors; %d bytes)\n",
		ddk_dev->size, ddk_dev->size * DDK_SECTOR_SIZE);
	usb_set_intfdata(interface, ddk_dev);

	return 0;
}
/*
 * This is the unregistration and uninitialization section of the ddk block
 * device driver
 */
void block_deregister_dev(struct usb_interface *interface)
{
	struct ddk_device *ddk_dev = (struct ddk_device *)(usb_get_intfdata(interface));

	cancel_work_sync(&ddk_dev->work); // In case, any work is already scheduled
	del_gendisk(ddk_dev->disk);
	put_disk(ddk_dev->disk);
	blk_cleanup_queue(ddk_dev->queue);
	unregister_blkdev(ddk_dev->major, "ddk");
	ddk_storage_cleanup(ddk_dev->device);
	kfree(ddk_dev);
}
