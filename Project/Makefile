# If called directly from the command line, invoke the kernel build system.
ifeq ($(KERNELRELEASE),)

	KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default: mkfs.ddkfs
	$(MAKE) -C $(KERNEL_SOURCE) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SOURCE) SUBDIRS=$(PWD) clean
	$(RM) mkfs.ddkfs

# Otherwise KERNELRELEASE is defined; we've been invoked from the
# kernel build system and can use its language.
else

	obj-m := first_driver.o
	#obj-m += dor.o
	#obj-m += ddkb.o
	#obj-m += ddkfs.o
	dor-y := ram_block.o ram_device.o partition.o
	ddkb-y := ddk_block.o ddk_storage.o
	ddkfs-y := ddk_fs.o ddk_fs_ops.o

endif
