#ifndef DDK_FS_OPS_H
#define DDK_FS_OPS_H

#include <linux/fs.h>

#include "ddk_fs_ds.h"

/*
 * Inode number 0 is not treated as a valid inode number at many places,
 * including applications like ls. So, the inode numbering should start
 * at not less than 1. For example, making root inode's number 0, leads
 * the lookuped entries . & .. to be not shown in ls
 */
#define ROOT_INODE_NUM (1)
#define S2V_INODE_NUM(i) (ROOT_INODE_NUM + 1 + (i)) // DDK FS to VFS
#define V2S_INODE_NUM(i) ((i) - (ROOT_INODE_NUM + 1)) // VFS to DDK FS
#define INV_INODE (-1)
#define INV_BLOCK (-1)

int dfs_init(dfs_info_t *info);
void dfs_shut(dfs_info_t *info);

int dfs_get_data_block(dfs_info_t *info); // Returns block number or INV_BLOCK
void dfs_put_data_block(dfs_info_t *info, int i);

int dfs_list(dfs_info_t *info, struct file *file, void *dirent, filldir_t filldir);

/* The following 4 APIs returns VFS inode number or INV_INODE */
int dfs_lookup(dfs_info_t *info, char *fn, dfs_file_entry_t *fe);
int dfs_create(dfs_info_t *info, char *fn, int perms, dfs_file_entry_t *fe);
int dfs_remove(dfs_info_t *info, char *fn);
int dfs_rename(dfs_info_t *info, char *src_fn, char *dst_fn);

int dfs_read_file_entry(dfs_info_t *info, int vfs_ino, dfs_file_entry_t *fe);
int dfs_write_file_entry(dfs_info_t *info, int vfs_ino, dfs_file_entry_t *fe);
int dfs_update(dfs_info_t *info, int vfs_ino, int *size, int *timestamp, int *perms);

#endif
