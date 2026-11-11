#ifndef _FDMFS_H
#define _FDMFS_H

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/nvme.h>

#define FDMFS_BLKSIZE		512
#define FDMFS_SUPER_OFFSET	1024
#define FDMFS_MAGIC		0x19990422
#define FDMFS_NAMELEN		255

struct fdmfs_inode;

struct fdm_region {
	struct list_head list;
	off_t off;
	size_t size;
	struct fdmfs_inode *inode;
};

struct fdmfs_inode {
	const unsigned char *name;
	struct inode *inode;
	struct fdmfs_sb_info *sbi;
	struct list_head sibling;
	struct list_head children;
	struct fdm_region *region;
	int ino;
	unsigned int region_pins;
	bool kv_registered;
	bool kv_failed;
	u64 kv_input_bytes;
	u64 kv_transfer_id;
	struct mutex kv_write_lock;
};

// on-disk super block
struct fdmfs_super_block {
	__u32 magic;
};

struct fdmfs_sb_info {
	struct super_block *sb;			/* pointer to VFS super block */
	struct fdmfs_super_block *raw_super;
	int root_ino;
	void *fdm_addr;
	size_t fdm_size;
	struct list_head fdm_free;
	struct list_head fdm_used;
	dev_t cemu_dev;
	struct block_device *cemu_bdev;
	struct fdmfs_inode *root_inode;
	uintptr_t ino_count;
};

static inline struct fdmfs_sb_info *FDMFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct fdmfs_inode *FDMFS_I(struct inode *i)
{
	return i->i_private;
}

static inline void *fdmfs_region_addr(struct fdmfs_inode *inode)
{
	struct fdmfs_sb_info *sbi = inode->sbi;
	struct fdm_region *region = inode->region;
	return sbi->fdm_addr + region->off;
}

struct inode *fdmfs_icreate(struct fdmfs_sb_info *sbi, struct mnt_idmap *idmap,
	const struct inode *dir, umode_t mode, const unsigned char *name);
void fdmfs_deallocate(struct fdmfs_inode *inode);
ssize_t fdmfs_copy_file_range(struct file *file_in, loff_t pos_in,
				     struct file *file_out, loff_t pos_out,
				     size_t size, unsigned int flags);
int fdmfs_get_memory_range(struct file *filp, struct nvme_memory_range *mr);
int fdmfs_pin_memory_range(struct file *file, struct block_device *bdev,
			   struct nvme_memory_range *range);
void fdmfs_unpin_memory_range(struct file *file);
int fdmfs_register_input(struct file *file, u64 input_bytes);
void fdmfs_unregister_input_locked(struct file *file);
void fdmfs_enable_input(struct file *file);

extern const struct iomap_ops fdmfs_iomap_ops;
extern const struct file_operations fdmfs_fops;
extern const struct inode_operations fdmfs_inode_ops;
extern const struct inode_operations fdmfs_dir_inode_ops;
extern const struct address_space_operations fdmfs_aops;
// extern const struct file_operations fdmfs_dir_fops;

#endif // _FDMFS_H
