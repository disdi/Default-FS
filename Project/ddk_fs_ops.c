#include <linux/fs.h> /* For struct super_block */
#include <linux/errno.h> /* For error codes */
#include <linux/buffer_head.h> /* struct buffer_head, sb_bread, ... */
#include <linux/slab.h> /* For kzalloc, ... */
#include <linux/string.h> /* For memcpy */
#include <linux/vmalloc.h> /* For vmalloc, ... */
#include <linux/time.h> /* For get_seconds, ... */

#include "ddk_fs_ds.h"
#include "ddk_fs_ops.h"

static int read_sb_from_ddk_fs(dfs_info_t *info, dfs_super_block_t *sb)
{
	struct buffer_head *bh;

	if (!(bh = sb_bread(info->vfs_sb, 0 /* Super block is the 0th block */)))
	{
		return -EIO;
	}
	memcpy(sb, bh->b_data, DDK_FS_BLOCK_SIZE);
	brelse(bh);
	return 0;
}
static int read_from_ddk_fs(dfs_info_t *info, byte4_t block, byte4_t offset, void *buf, byte4_t len)
{
	byte4_t block_size = info->sb.block_size;
	byte4_t bd_block_size = info->vfs_sb->s_bdev->bd_block_size;
	byte4_t abs;
	struct buffer_head *bh;

	// Translating the DDK FS block numbering to underlying block device block numbering, for sb_bread()
	abs = block * block_size + offset;
	block = abs / bd_block_size;
	offset = abs % bd_block_size;
	if (offset + len > bd_block_size) // Should never happen
	{
		return -EINVAL;
	}
	if (!(bh = sb_bread(info->vfs_sb, block)))
	{
		return -EIO;
	}
	memcpy(buf, bh->b_data + offset, len);
	brelse(bh);
	return 0;
}
static int write_to_ddk_fs(dfs_info_t *info, byte4_t block, byte4_t offset, void *buf, byte4_t len)
{
	byte4_t block_size = info->sb.block_size;
	byte4_t bd_block_size = info->vfs_sb->s_bdev->bd_block_size;
	byte4_t abs;
	struct buffer_head *bh;

	// Translating the DDK FS block numbering to underlying block device block numbering, for sb_bread()
	abs = block * block_size + offset;
	block = abs / bd_block_size;
	offset = abs % bd_block_size;
	if (offset + len > bd_block_size) // Should never happen
	{
		return -EINVAL;
	}
	if (!(bh = sb_bread(info->vfs_sb, block)))
	{
		return -EIO;
	}
	memcpy(bh->b_data + offset, buf, len);
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}
static int read_entry_from_ddk_fs(dfs_info_t *info, int ino, dfs_file_entry_t *fe)
{
	return 0;
	//return read_from_ddk_fs(info, /* TODO: Populate all the other parameters */ );
}
static int write_entry_to_ddk_fs(dfs_info_t *info, int ino, dfs_file_entry_t *fe)
{
	return 0;
	//return write_to_ddk_fs(info,  /* TODO: Populate all the other parameters */ );
}

int dfs_init(dfs_info_t *info)
{
	byte1_t *used_blocks;
	byte4_t free_block_count, free_entry_count;
	int i, j;
	dfs_file_entry_t fe;
	int retval;

	if ((retval = read_sb_from_ddk_fs(info, &info->sb)) < 0)
	{
		return retval;
	}
	if (info->sb.type != DDK_FS_TYPE)
	{
		printk(KERN_ERR "Invalid DDK FS detected. Giving up.\n");
		return -EINVAL;
	}

	/* Mark used blocks */
	used_blocks = (byte1_t *)(vmalloc(info->sb.partition_size));
	if (!used_blocks)
	{
		return -ENOMEM;
	}
	free_block_count = 0;
	free_entry_count = 0;
	for (i = 0; i < info->sb.data_block_start; i++)
	{
		used_blocks[i] = 1;
	}
	for (; i < info->sb.partition_size; i++)
	{
		used_blocks[i] = 0;
		free_block_count++;
	}

	for (i = 0; i < info->sb.entry_count; i++)
	{
	/*	if ((retval = read_entry_from_ddk_fs(info, i, &fe)) < 0)
		{
			vfree(used_blocks);
			return retval;
		}*/

		/* TODO: Update the free_entry_count appropriately here */


		for (j = 0; j < DDK_FS_DATA_BLOCK_CNT; j++)
		{
			if (fe.blocks[j] == 0) break;
			used_blocks[fe.blocks[j]] = 1;
			free_block_count--;
		}
	}

	info->used_blocks = used_blocks;
	info->free_block_count = free_block_count;
	info->free_entry_count = free_entry_count;
	info->vfs_sb->s_fs_info = info;
	spin_lock_init(&info->lock);
	return 0;
}
void dfs_shut(dfs_info_t *info)
{
	if (info->used_blocks)
		vfree(info->used_blocks);
}

int dfs_get_data_block(dfs_info_t *info)
{
	int i;

	spin_lock(&info->lock); // To prevent racing on used_blocks, ... access
	for (i = info->sb.data_block_start; i < info->sb.partition_size; i++)
	{
		if (info->used_blocks[i] == 0)
		{
			info->used_blocks[i] = 1;
			info->free_block_count--;
			spin_unlock(&info->lock);
			return i;
		}
	}
	spin_unlock(&info->lock);
	return INV_BLOCK;
}
void dfs_put_data_block(dfs_info_t *info, int i)
{
	spin_lock(&info->lock); // To prevent racing on used_blocks, ... access
	info->used_blocks[i] = 0;
	info->free_block_count++;
	spin_unlock(&info->lock);
}

int dfs_list(dfs_info_t *info, struct file *file, void *dirent, filldir_t filldir)
{
	loff_t pos;
	int ino;
	dfs_file_entry_t fe;
	int retval;

	pos = 1; /* Starts at 1 as . is position 0 & .. is position 1 */
	for (ino = 0; ino < info->sb.entry_count; ino++)
	{
		if ((retval = read_entry_from_ddk_fs(info, ino, &fe)) < 0)
			return retval;
		if (!fe.name[0]) continue;
		pos++; /* Position of this file */
		if (file->f_pos == pos)
		{
			retval = filldir(dirent, /* TODO: filename */"", /* TODO: filename length */0, file->f_pos, S2V_INODE_NUM(ino), DT_REG);
			if (retval)
			{
				return retval;
			}
			file->f_pos++;
		}
	}
	return 0;
}
int dfs_lookup(dfs_info_t *info, char *fn, dfs_file_entry_t *fe)
{
	//int ino;

	/* TODO: If found, return S2V_INODE_NUM(ino) else INV_INODE */

	return INV_INODE;
}
int dfs_create(dfs_info_t *info, char *fn, int perms, dfs_file_entry_t *fe)
/* This function is called only if the file doesn't exist */
{
	int ino, free_ino, i;

	free_ino = INV_INODE;
	for (ino = 0; ino < info->sb.entry_count; ino++)
	{
		if (read_entry_from_ddk_fs(info, ino, fe) < 0)
			return INV_INODE;
		if (!fe->name[0])
		{
			free_ino = ino;
			break;
		}
	}
	if (free_ino == INV_INODE)
	{
		printk(KERN_ERR "No entries left\n");
		return INV_INODE;
	}

	/*
	 * TODO: Fill up the fe->name with filename fn.
	 * NB Take care of no buffer overflow.
	 */

	fe->size = 0;
	fe->timestamp = get_seconds();
	fe->perms = perms;
	for (i = 0; i < DDK_FS_DATA_BLOCK_CNT; i++)
	{
		fe->blocks[i] = 0;
	}

	if (write_entry_to_ddk_fs(info, free_ino, fe) < 0)
		return INV_INODE;

	spin_lock(&info->lock); // To prevent racing on free_entry_count access
	info->free_entry_count--;
	spin_unlock(&info->lock);

	return S2V_INODE_NUM(free_ino);
}
int dfs_remove(dfs_info_t *info, char *fn)
{
	int vfs_ino;
	dfs_file_entry_t fe;

	if ((vfs_ino = dfs_lookup(info, fn, &fe)) == INV_INODE)
	{
		printk(KERN_ERR "File %s doesn't exist\n", fn);
		return INV_INODE;
	}

	/* TODO: Free up all allocated blocks, if any */


	memset(&fe, 0, sizeof(dfs_file_entry_t));

	if (write_entry_to_ddk_fs(info, V2S_INODE_NUM(vfs_ino), &fe) < 0)
		return INV_INODE;

	spin_lock(&info->lock); // To prevent racing on free_entry_count access
	info->free_entry_count++;
	spin_unlock(&info->lock);

	return vfs_ino;
}
int dfs_rename(dfs_info_t *info, char *src_fn, char *dst_fn)
{
	int vfs_ino;
	dfs_file_entry_t fe;

	/* TODO: Get the inode & its number of the src_fn */


	/* TODO: Implement the rename logic */

	/* Write the inode back */
	if (write_entry_to_ddk_fs(info, V2S_INODE_NUM(vfs_ino), &fe) < 0)
		return INV_INODE;

	return vfs_ino;
}

int dfs_read_file_entry(dfs_info_t *info, int vfs_ino, dfs_file_entry_t *fe)
{
	return read_entry_from_ddk_fs(info, V2S_INODE_NUM(vfs_ino), fe);
}
int dfs_write_file_entry(dfs_info_t *info, int vfs_ino, dfs_file_entry_t *fe)
{
	return write_entry_to_ddk_fs(info, V2S_INODE_NUM(vfs_ino), fe);
}
int dfs_update(dfs_info_t *info, int vfs_ino, int *size, int *timestamp, int *perms)
{
	dfs_file_entry_t fe;
	int i;
	int retval;

	if ((retval = dfs_read_file_entry(info, vfs_ino, &fe)) < 0)
	{
		return retval;
	}

	/* TODO: Update the file entry */

	for (i = (fe.size + info->sb.block_size - 1) / info->sb.block_size; i < DDK_FS_DATA_BLOCK_CNT; i++)
	{
		if (fe.blocks[i])
		{
			dfs_put_data_block(info, fe.blocks[i]);
			fe.blocks[i] = 0;
		}
	}

	return dfs_write_file_entry(info, vfs_ino, &fe);
}
