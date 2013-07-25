#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h> /* For errno */
#include <string.h> /* For strerror() */
#include <sys/ioctl.h> /* For ioctl() */
#include <linux/fs.h> /* For BLKGETSIZE64 */

#include "ddk_fs_ds.h"

#define DFS_ENTRY_RATIO 0.10 /* 10% of all blocks */
#define DFS_ENTRY_TABLE_BLOCK_START 1

dfs_super_block_t sb =
{
	.type = DDK_FS_TYPE,
	.block_size = DDK_FS_BLOCK_SIZE,
	.entry_size = DDK_FS_ENTRY_SIZE,
	.entry_table_block_start = DFS_ENTRY_TABLE_BLOCK_START
};
dfs_file_entry_t fe; /* All 0's */

void write_super_block(int dfs_handle, dfs_super_block_t *sb)
{
	write(dfs_handle, sb, sizeof(dfs_super_block_t));
}
void clear_file_entries(int dfs_handle, dfs_super_block_t *sb)
{
	int i;
	byte1_t block[DDK_FS_BLOCK_SIZE];

	for (i = 0; i < sb->block_size / sb->entry_size; i++)
	{
		memcpy(block + i * sb->entry_size, &fe, sizeof(fe));
	}
	for (i = 0; i < sb->entry_table_size; i++)
	{
		write(dfs_handle, block, sizeof(block));
	}
}

int main(int argc, char *argv[])
{
	int dfs_handle;
	byte8_t size;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <partition's device file>\n", argv[0]);
		return 1;
	}
	dfs_handle = open(argv[1], O_RDWR);
	if (dfs_handle == -1)
	{
		fprintf(stderr, "Error formatting %s: %s\n", argv[1], strerror(errno));
		return 2;
	}
	if (ioctl(dfs_handle, /* TODO: Replace the 0 with the ioctl command */ BLKGETSIZE64, &size) == -1)
	{
		fprintf(stderr, "Error getting size of %s: %s\n", argv[1], strerror(errno));
		return 3;
	}
	/* TODO: Fill up the partition size in blocks */
	sb.partition_size = 48;
	/* TODO: Fill up the entry table size in blocks */
	sb.entry_table_size = sb.partition_size * DFS_ENTRY_RATIO;
	/* TODO: Fill up the total number of entries */
	sb.entry_count = sb.entry_table_size * sb.block_size/sb.entry_size;
	/* Block number of the first data block */
	sb.data_block_start = DFS_ENTRY_TABLE_BLOCK_START +  sb.entry_table_size;

	printf("Partitioning %Ld byte sized %s ... ", size, argv[1]);
	fflush(stdout);
	write_super_block(dfs_handle, &sb);
	clear_file_entries(dfs_handle, &sb);

	close(dfs_handle);
	printf("done\n");

	return 0;
}
