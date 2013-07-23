/* DDK File System Module */
#include <linux/module.h> /* For module related macros, ... */
#include <linux/kernel.h> /* For printk, ... */
#include <linux/version.h> /* For LINUX_VERSION_CODE & KERNEL_VERSION */
#include <linux/fs.h> /* For system calls, structures, ... */
#include <linux/errno.h> /* For error codes */
#include <linux/slab.h> /* For kzalloc, ... */
#include <linux/buffer_head.h> /* map_bh, block_write_begin, block_write_full_page, generic_write_end, ... */
#include <linux/mpage.h> /* mpage_readpage, ... */
#include <linux/statfs.h> /* struct kstatfs, ... */

#include "ddk_fs_ds.h" /* For DDK FS related defines, data structures, ... */
#include "ddk_fs_ops.h" /* For DDK FS related operations */

/*
 * Data declarations
 */
static struct file_system_type dfs;
static struct super_operations dfs_sops;
static struct inode_operations dfs_iops;
static struct file_operations dfs_fops;
static struct address_space_operations dfs_aops;

static struct inode *dfs_root_inode;

/*
 * File Operations
 */
static int dfs_file_release(struct inode *inode, struct file *file)
{
	//printk(KERN_INFO "ddkfs: dfs_file_release\n");
	return 0;
}
static int dfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	int retval;
	struct dentry *de = file->f_dentry;
	dfs_info_t *info = de->d_inode->i_sb->s_fs_info;

	printk(KERN_INFO "ddkfs: dfs_readdir: %Ld\n", file->f_pos);

	if (file->f_pos == 0)
	{
		retval = filldir(dirent, ".", 1, file->f_pos, de->d_inode->i_ino, DT_DIR);
		if (retval)
			return retval;
		file->f_pos++;
	}
	if (file->f_pos == 1)
	{
		retval = filldir(dirent, "..", 2, file->f_pos, de->d_parent->d_inode->i_ino, DT_DIR);
		if (retval)
			return retval;
		file->f_pos++;
	}
	return 0;
	//return dfs_list(info, file, dirent, filldir); // TODO: Ready the dfs_list to uncomment this
}
static struct file_operations dfs_fops =
{
	open: generic_file_open,
	release: dfs_file_release,
	read: do_sync_read,
	write: do_sync_write,
	aio_read: generic_file_aio_read,
	aio_write: generic_file_aio_write,
	llseek:	generic_file_llseek,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
	fsync: simple_sync_file
#else
	fsync: noop_fsync
#endif
};
static struct file_operations dfs_dops =
{
	readdir: dfs_readdir
};

static int dfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	dfs_info_t *info = (dfs_info_t *)(sb->s_fs_info);
	dfs_file_entry_t fe;
	sector_t phys;
	int retval;

	printk(KERN_INFO "ddkfs: dfs_get_block called for I: %ld, B: %lld, C: %d\n", inode->i_ino, iblock, create);

	if (iblock >= DDK_FS_DATA_BLOCK_CNT)
	{
		return -ENOSPC;
	}
	if ((retval = dfs_read_file_entry(info, inode->i_ino, &fe)) < 0)
	{
		return retval;
	}
	if (!fe.blocks[iblock])
	{
		if (!create)
		{
			return -EIO;
		}
		else
		{
			/*
			 * TODO: Get a free data block for fe.blocks[iblock]
			 * If it is INV_BLOCK then return -ENOSPC, other proceed further
			 * to dfs_write_file_entry
			 *
			 * Add your code here and delete the return
			 */
			return -EIO;

			if ((retval = dfs_write_file_entry(info, inode->i_ino, &fe)) < 0)
			{
				return retval;
			}
		}
	}
	phys = fe.blocks[iblock];
	map_bh(bh_result, sb, phys);

	return 0;
}
static int dfs_readpage(struct file *file, struct page *page)
{
	printk(KERN_INFO "ddkfs: dfs_readpage\n");
	return mpage_readpage(page, dfs_get_block);
}
static int dfs_write_begin(struct file *file, struct address_space *mapping,
	loff_t pos, unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
	printk(KERN_INFO "ddkfs: dfs_write_begin\n");
	*pagep = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
	return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
		dfs_get_block);
#else
	return block_write_begin(mapping, pos, len, flags, pagep, dfs_get_block);
#endif
}
static int dfs_writepage(struct page *page, struct writeback_control *wbc)
{
	printk(KERN_INFO "ddkfs: dfs_writepage\n");
	return block_write_full_page(page, dfs_get_block, wbc);
}
static struct address_space_operations dfs_aops =
{
	.readpage = dfs_readpage,
	.write_begin = dfs_write_begin,
	.writepage = dfs_writepage,
	.write_end = generic_write_end
};

/*
 * Inode Operations
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
static struct dentry *dfs_inode_lookup(struct inode *parent_inode, struct dentry *dentry, struct nameidata *nameidata)
#else
static struct dentry *dfs_inode_lookup(struct inode *parent_inode, struct dentry *dentry, unsigned int flags)
#endif
{
	dfs_info_t *info = (dfs_info_t *)(parent_inode->i_sb->s_fs_info);
	char fn[dentry->d_name.len + 1];
	int ino;
	dfs_file_entry_t fe;
	struct inode *file_inode = NULL;

	printk(KERN_INFO "ddkfs: dfs_inode_lookup\n");

	if (parent_inode->i_ino != dfs_root_inode->i_ino)
		return ERR_PTR(-ENOENT);
	strncpy(fn, dentry->d_name.name, dentry->d_name.len);
	fn[dentry->d_name.len] = 0;
	if ((ino = dfs_lookup(info, fn, &fe)) == INV_INODE) // TODO: Fill up the dfs_lookup
	  return d_splice_alias(file_inode, dentry); // Possibly create a new one

	printk(KERN_INFO "ddkfs: Getting an existing inode\n");
	file_inode = iget_locked(parent_inode->i_sb, ino);
	if (!file_inode)
		return ERR_PTR(-EACCES);
	if (file_inode->i_state & I_NEW)
	{
		printk(KERN_INFO "ddkfs: Got new VFS inode for #%d, let's fill in\n", ino);
		file_inode->i_size = fe.size;
		file_inode->i_mode = S_IFREG;
		file_inode->i_mode |= ((fe.perms & 4) ? S_IRUSR | S_IRGRP | S_IROTH : 0);
		file_inode->i_mode |= ((fe.perms & 2) ? S_IWUSR | S_IWGRP | S_IWOTH : 0);
		file_inode->i_mode |= ((fe.perms & 1) ? S_IXUSR | S_IXGRP | S_IXOTH : 0);
		file_inode->i_atime.tv_sec = file_inode->i_mtime.tv_sec = file_inode->i_ctime.tv_sec = fe.timestamp;
		file_inode->i_atime.tv_nsec = file_inode->i_mtime.tv_nsec = file_inode->i_ctime.tv_nsec = 0;
		file_inode->i_mapping->a_ops = &dfs_aops;
		file_inode->i_fop = &dfs_fops;
		unlock_new_inode(file_inode);
	}
	else
	{
		printk(KERN_INFO "ddkfs: Got VFS inode from inode cache\n");
	}
	d_add(dentry, file_inode);
	return NULL;
	// Above 2 lines can be replaced by 'return d_splice_alias(file_inode, dentry);'
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
static int dfs_inode_create(struct inode *parent_inode, struct dentry *dentry, umode_t mode, struct nameidata *nameidata)
#else
static int dfs_inode_create(struct inode *parent_inode, struct dentry *dentry, umode_t mode, bool excl)
#endif
{
	char fn[dentry->d_name.len + 1];
	int perms = 0;
	dfs_info_t *info = (dfs_info_t *)(parent_inode->i_sb->s_fs_info);
	int ino;
	struct inode *file_inode;
	dfs_file_entry_t fe;

	printk(KERN_INFO "ddkfs: dfs_inode_create\n");

	strncpy(fn, dentry->d_name.name, dentry->d_name.len);
	fn[dentry->d_name.len] = 0;
	if (mode & (S_IRUSR | S_IRGRP | S_IROTH))
		mode |= (S_IRUSR | S_IRGRP | S_IROTH);
	if (mode & (S_IWUSR | S_IWGRP | S_IWOTH))
		mode |= (S_IWUSR | S_IWGRP | S_IWOTH);
	if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
		mode |= (S_IXUSR | S_IXGRP | S_IXOTH);
	perms |= (mode & S_IRUSR) ? 4 : 0;
	perms |= (mode & S_IWUSR) ? 2 : 0;
	perms |= (mode & S_IXUSR) ? 1 : 0;
	if ((ino = dfs_create(info, fn, perms, &fe)) == INV_INODE) // TODO: Fill up the dfs_create
		return -ENOSPC;

	file_inode = new_inode(parent_inode->i_sb);
	if (!file_inode)
	{
		dfs_remove(info, fn); // Nothing to do, even if it fails
		return -ENOMEM;
	}
	printk(KERN_INFO "ddkfs: Created new VFS inode for #%d, let's fill in\n", ino);
	file_inode->i_ino = ino;
	file_inode->i_size = fe.size;
	file_inode->i_mode = S_IFREG | mode;
	file_inode->i_atime.tv_sec = file_inode->i_mtime.tv_sec = file_inode->i_ctime.tv_sec = fe.timestamp;
	file_inode->i_atime.tv_nsec = file_inode->i_mtime.tv_nsec = file_inode->i_ctime.tv_nsec = 0;
	file_inode->i_mapping->a_ops = &dfs_aops;
	file_inode->i_fop = &dfs_fops;
	if (insert_inode_locked(file_inode) < 0)
	{
		make_bad_inode(file_inode);
		iput(file_inode);
		dfs_remove(info, fn); // Nothing to do, even if it fails
		return -EIO;
	}
	d_instantiate(dentry, file_inode);
	unlock_new_inode(file_inode);

	return 0;
}
#if 0
static int dfs_inode_unlink(struct inode *parent_inode, struct dentry *dentry)
{
	char fn[dentry->d_name.len + 1];
	dfs_info_t *info = (dfs_info_t *)(parent_inode->i_sb->s_fs_info);
	int ino;
	struct inode *file_inode = dentry->d_inode;

	printk(KERN_INFO "ddkfs: dfs_inode_unlink\n");

	strncpy(fn, dentry->d_name.name, dentry->d_name.len);
	fn[dentry->d_name.len] = 0;
	if ((ino = dfs_remove(info, fn)) == INV_INODE) // TODO: Fill up the dfs_remove
		return -EINVAL;

	inode_dec_link_count(file_inode);
	return 0;
}
static int dfs_inode_rename(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	dfs_info_t *info = (dfs_info_t *)(old_dir->i_sb->s_fs_info);
	char src_fn[old_dentry->d_name.len + 1];
	char dst_fn[new_dentry->d_name.len + 1];

	printk(KERN_INFO "ddkfs: dfs_inode_rename\n");
	if ((old_dir != new_dir) || (old_dir->i_ino != ROOT_INODE_NUM))
	/* Both files not at root level */
		return -EINVAL;
	strncpy(src_fn, old_dentry->d_name.name, old_dentry->d_name.len);
	src_fn[old_dentry->d_name.len] = 0;
	strncpy(dst_fn, new_dentry->d_name.name, new_dentry->d_name.len);
	dst_fn[new_dentry->d_name.len] = 0;

	if (dfs_rename(info, src_fn, dst_fn) == INV_INODE) // TODO: Fill up the dfs_rename
		return -ENOENT;
	else
		return 0;
}
#endif
static struct inode_operations dfs_iops =
{
	lookup: dfs_inode_lookup,
	create: dfs_inode_create,
	//unlink: dfs_inode_unlink, /* TODO: Now try removing the files */
	//rename: dfs_inode_rename /* TODO: Now try renaming the files */
};

/*
 * Super-Block Operations
 */
static void dfs_put_super(struct super_block *sb)
{
	dfs_info_t *info = (dfs_info_t *)(sb->s_fs_info);

	printk(KERN_INFO "ddkfs: dfs_put_super\n");
	if (info)
	{
		dfs_shut(info);
		kfree(info);
		sb->s_fs_info = NULL;
	}
}
#if 0
static int dfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	dfs_info_t *info = (dfs_info_t *)(sb->s_fs_info);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	printk(KERN_INFO "ddkfs: dfs_statfs\n");
	buf->f_type = info->sb.type;
	buf->f_bsize = info->sb.block_size;
	buf->f_blocks = info->sb.partition_size;
	/* TODO: Total number of free blocks */
	buf->f_bfree = 0;
	/* Total number of blocks available to unprivileged user */
	buf->f_bavail = buf->f_bfree;
	/* TODO: Total number of entries */
	buf->f_files = 0;
	/* TODO: Total number of free entries */
	buf->f_ffree = 0;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = DDK_FS_FILENAME_LEN;
	return 0;
}
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))
static int dfs_write_inode(struct inode *inode, int do_sync)
#else
static int dfs_write_inode(struct inode *inode, struct writeback_control *wbc)
#endif
{
	dfs_info_t *info = (dfs_info_t *)(inode->i_sb->s_fs_info);
	int size, timestamp, perms;

	printk(KERN_INFO "ddkfs: dfs_write_inode (i_ino = %ld)\n", inode->i_ino);

	if (!(S_ISREG(inode->i_mode))) // DDK FS deals only with regular files
		return 0;

	size = i_size_read(inode);
	timestamp = inode->i_mtime.tv_sec > inode->i_ctime.tv_sec ? inode->i_mtime.tv_sec : inode->i_ctime.tv_sec;
	perms = 0;
	perms |= (inode->i_mode & (S_IRUSR | S_IRGRP | S_IROTH)) ? 4 : 0;
	perms |= (inode->i_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) ? 2 : 0;
	perms |= (inode->i_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) ? 1 : 0;

	printk(KERN_INFO "ddkfs: Writing inode with %d bytes @ %d secs w/ %o\n", size, timestamp, perms);

	return dfs_update(info, inode->i_ino, &size, &timestamp, &perms); // TODO: Fill up the dfs_update
}

static struct super_operations dfs_sops =
{
	put_super: dfs_put_super,
	//statfs: dfs_statfs, /* used by df to show it up */ /* TODO: Now try getting the stats */
	write_inode: dfs_write_inode
};

/*
 * File-System Supporting Operations
 */
static int get_bit_pos(unsigned int val)
{
	int i;

	for (i = 0; val; i++)
	{   
		val >>= 1;
	}   
	return (i - 1); 
}
static int dfs_fill_super(struct super_block *sb, void *data, int silent)
{
	dfs_info_t *info;

	printk(KERN_INFO "ddkfs: dfs_fill_super\n");
	if (!(info = (dfs_info_t *)(kzalloc(sizeof(dfs_info_t), GFP_KERNEL))))
		return -ENOMEM;
	info->vfs_sb = sb;
	if (dfs_init(info) < 0)
	{
		kfree(info);
		return -EIO;
	}
	/* Update the VFS super_block */
	sb->s_magic = /* TODO: File system type */
	sb->s_blocksize = /* TODO: File system block size */
	sb->s_blocksize_bits = get_bit_pos(sb->s_blocksize);
	sb->s_type = &dfs; // file_system_type
	sb->s_op = &dfs_sops; // super block operations

	dfs_root_inode = iget_locked(sb, ROOT_INODE_NUM); // obtain an inode from VFS
	if (!dfs_root_inode)
	{
		dfs_shut(info);
		kfree(info);
		return -EACCES;
	}
	if (dfs_root_inode->i_state & I_NEW) // allocated fresh now
	{
		printk(KERN_INFO "ddkfs: Got root's new VFS inode, let's fill in\n");
		dfs_root_inode->i_op = &dfs_iops; // inode operations
		dfs_root_inode->i_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
		dfs_root_inode->i_fop = &dfs_dops; // file operations for directory
		dfs_root_inode->i_mapping->a_ops = &dfs_aops; // address operations
		unlock_new_inode(dfs_root_inode);
	}
	else
	{
		printk(KERN_INFO "ddkfs: Got root's VFS inode from inode cache\n");
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
	sb->s_root = d_alloc_root(dfs_root_inode);
#else
	sb->s_root = d_make_root(dfs_root_inode);
#endif
	if (!sb->s_root)
	{
		iget_failed(dfs_root_inode);
		dfs_shut(info);
		kfree(info);
		return -ENOMEM;
	}

	return 0;
}

/*
 * File-System Operations
 */
static struct dentry *dfs_mount(struct file_system_type *fs_type, int flags, const char *devname, void *data)
{
	printk(KERN_INFO "ddkfs: dfs_mount: devname = %s\n", devname);

	 /* dfs_fill_super this will be called to fill the super block */
	return mount_bdev(fs_type, flags, devname, data, &dfs_fill_super);
}

static struct file_system_type dfs =
{
	name: "ddkfs", /* Name of our file system */
	fs_flags: FS_REQUIRES_DEV, /* Removes nodev from /proc/filesystems */
	mount:  dfs_mount,
	kill_sb: kill_block_super,
	owner: THIS_MODULE
};

static int __init ddk_fs_init(void)
{
	int err;

	printk(KERN_INFO "ddkfs: dfs_init\n");
	err = register_filesystem(&dfs);
	return err;
}

static void __exit ddk_fs_exit(void)
{
	printk(KERN_INFO "ddkfs: dfs_exit\n");
	unregister_filesystem(&dfs);
}

module_init(ddk_fs_init);
module_exit(ddk_fs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia aka Pugs <email@sarika-pugs.com>");
MODULE_DESCRIPTION("File System Module for DDK File System");
