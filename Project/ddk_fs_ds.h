#ifndef DDK_FS_DS_H
#define DDK_FS_DS_H

#include <linux/fs.h>
#ifdef __KERNEL__
#include <linux/spinlock.h>
#endif

#define DDK_FS_TYPE 0x13090D15 /* Magic Number for our file system */
#define DDK_FS_BLOCK_SIZE 512 /* in bytes */
#define DDK_FS_BLOCK_SIZE_BITS 9 /* log(DDK_FS_BLOCK_SIZE) w/ base 2 */
#define DDK_FS_ENTRY_SIZE 64 /* in bytes */
#define DDK_FS_FILENAME_LEN 15
#define DDK_FS_DATA_BLOCK_CNT ((DDK_FS_ENTRY_SIZE - ((DDK_FS_FILENAME_LEN + 1) + 3 * 4)) / 4)

typedef unsigned char byte1_t;
typedef unsigned int byte4_t;
typedef unsigned long long byte8_t;

typedef struct dfs_super_block
{
	byte4_t type; /* Magic number to identify the file system */
	byte4_t block_size; /* Unit of allocation */
	byte4_t partition_size; /* in blocks */
	byte4_t entry_size; /* in bytes */
	byte4_t entry_table_size; /* in blocks */
	byte4_t entry_table_block_start; /* in blocks */
	byte4_t entry_count; /* Total entries in the file system */
	byte4_t data_block_start; /* in blocks */
	byte4_t reserved[DDK_FS_BLOCK_SIZE / 4 - 8];
} dfs_super_block_t; /* Making it of DDK_FS_BLOCK_SIZE */

typedef struct dfs_file_entry
{
	char name[DDK_FS_FILENAME_LEN + 1];
	byte4_t size; /* in bytes */
	byte4_t timestamp; /* Seconds since Epoch */
	byte4_t perms; /* Permissions only for user; Replicated for group & others */
	byte4_t blocks[DDK_FS_DATA_BLOCK_CNT];
} dfs_file_entry_t;

#ifdef __KERNEL__
typedef struct dfs_info
{
	struct super_block *vfs_sb; /* Super block structure from VFS for this fs */
	dfs_super_block_t sb; /* Our fs super block */
	byte1_t *used_blocks; /* Used blocks tracker */
	byte4_t free_block_count; /* Count of free blocks */
	byte4_t free_entry_count; /* Count of free entries */
	spinlock_t lock; /* Used for protecting access of used_blocks, ... */
} dfs_info_t;
#endif

#endif
