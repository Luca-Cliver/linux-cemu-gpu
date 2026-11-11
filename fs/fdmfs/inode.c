#include <linux/fs.h>
#include <linux/iomap.h>

#include "fdmfs.h"

static void fdmfs_add_entry(struct fdmfs_inode *dir, struct fdmfs_inode *inode)
{
	pr_info("FDMFS add_entry dir: %s, inode: %s\n", dir->name, inode->name);
	list_add(&inode->sibling, &dir->children);
}

static void fdmfs_del_entry(struct fdmfs_inode *inode)
{
	list_del(&inode->sibling);
}

static bool fdmfs_empty_dir(struct fdmfs_inode *dir)
{
	return list_empty(&dir->children);
}

static struct inode *fdmfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	inode = iget_locked(sb, ino);

	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	pr_err("FDMFS: fdmfs_iget cannot find inode from inode cache!\n");
	return NULL;
}

struct inode *fdmfs_icreate(struct fdmfs_sb_info *sbi, struct mnt_idmap *idmap,
		const struct inode *dir, umode_t mode, const unsigned char *name)
{
	struct inode *inode;
	struct fdmfs_inode *fdmfs_inode;

	pr_info("FDMFS: icreate %s, mode %d\n", name, mode);

	inode = new_inode(sbi->sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	fdmfs_inode = kzalloc(sizeof(struct fdmfs_inode), GFP_KERNEL);
	if (!fdmfs_inode) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}

	inode->i_ino = sbi->ino_count++;
	inode->i_blocks = 0;
	inode->i_private = fdmfs_inode;
	inode->i_blkbits = 1;
	inode_init_owner(idmap, inode, dir, mode);
	simple_inode_init_ts(inode);

	switch (mode & S_IFMT) {
	case S_IFDIR:
		inode->i_op = &fdmfs_dir_inode_ops;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(inode);
		break;
	case S_IFREG:
		inode->i_op = &fdmfs_inode_ops;
		inode->i_fop = &fdmfs_fops;
		inode->i_mapping->a_ops = &fdmfs_aops;
		break;
	default:
		pr_err("FDMFS: unknown inode type\n");
		return ERR_PTR(-EINVAL);
	}

	fdmfs_inode->sbi = sbi;
	fdmfs_inode->ino = inode->i_ino;
	fdmfs_inode->inode = inode;
	mutex_init(&fdmfs_inode->kv_write_lock);
	fdmfs_inode->name = name;
	INIT_LIST_HEAD(&fdmfs_inode->sibling);
	INIT_LIST_HEAD(&fdmfs_inode->children);
	return inode;
}

static int fdmfs_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode;
	struct fdmfs_sb_info *sbi = FDMFS_SB(dir->i_sb);

	pr_info("FDMFS: create %s, mode %d\n", dentry->d_name.name, mode);

	inode = fdmfs_icreate(sbi, idmap, dir, mode, dentry->d_name.name);
	fdmfs_add_entry(FDMFS_I(dir), FDMFS_I(inode));
	d_instantiate(dentry, inode);
	return 0;
}

static struct dentry *fdmfs_lookup(struct inode *dir, struct dentry *dentry,
					unsigned int flags)
{
	struct fdmfs_inode *fdmfs_inode, *tmp;
	struct inode *inode = NULL;
	// struct fdmfs_sb_info *sbi = FDMFS_SB(dir->i_sb);

	// pr_info("FDMFS: lookup %s\n", dentry->d_name.name);

	if (dentry->d_name.len > FDMFS_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	fdmfs_inode = FDMFS_I(dir);
	list_for_each_entry(tmp, &fdmfs_inode->children, sibling) {
		if (!strcmp(tmp->name, dentry->d_name.name)) {
			inode = fdmfs_iget(dir->i_sb, tmp->ino);
			if (IS_ERR(inode))
				return ERR_CAST(inode);
		}
	}
	return d_splice_alias(inode, dentry);
}

static int fdmfs_link(struct dentry *old, struct inode *dir, struct dentry *new)
{
	struct inode *inode = d_inode(old);
	if (FDMFS_I(inode)->region_pins)
		return -EBUSY;

	pr_info("FDMFS: link %s\n", new->d_name.name);

	fdmfs_add_entry(FDMFS_I(dir), FDMFS_I(inode));
	inode_inc_link_count(inode);
	d_instantiate(new, inode);
	return 0;
}

static int fdmfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	if (FDMFS_I(inode)->region_pins)
		return -EBUSY;

	fdmfs_del_entry(FDMFS_I(inode));
	inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
	inode_dec_link_count(inode);
	fdmfs_deallocate(inode->i_private);
	pr_info("FDMFS: unlink %s, link count: %d\n", dentry->d_name.name, inode->i_nlink);
	return 0;
}

static int fdmfs_mkdir(struct mnt_idmap * idmap,
	struct inode * dir, struct dentry * dentry, umode_t mode)
{
	struct inode * inode;
	struct fdmfs_sb_info * sbi = FDMFS_SB(dir->i_sb);

	pr_info("FDMFS: mkdir %s, mode %d\n", dentry->d_name.name, mode);

	inode = fdmfs_icreate(sbi, idmap, dir, mode | S_IFDIR, dentry->d_name.name);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	fdmfs_add_entry(FDMFS_I(dir), FDMFS_I(inode));

	inode_inc_link_count(dir);
	inode_inc_link_count(inode);
	d_instantiate(dentry, inode);
	return 0;
}

static int fdmfs_rmdir(struct inode *dir,struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	if (fdmfs_empty_dir(FDMFS_I(inode))) {
		return fdmfs_unlink(dir, dentry);
	} else {
		return -ENOTEMPTY;
	}
}

static int fdmfs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		      struct dentry *old_dentry, struct inode *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	struct fdmfs_inode *fdmfs_inode = FDMFS_I(inode);

	if (fdmfs_inode->region_pins ||
	    (d_inode(new_dentry) && FDMFS_I(d_inode(new_dentry))->region_pins))
		return -EBUSY;
	inode_init_owner(idmap, inode, new_dir, inode->i_mode);

	fdmfs_inode->name = new_dentry->d_name.name;
	fdmfs_del_entry(fdmfs_inode);
	fdmfs_add_entry(FDMFS_I(new_dir), fdmfs_inode);

	return 0;
}

static int fdmfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			 struct iattr *attr)
{
	if ((attr->ia_valid & ATTR_SIZE) && FDMFS_I(d_inode(dentry))->region_pins)
		return -EBUSY;
	return simple_setattr(idmap, dentry, attr);
}

// O_DIRECT requires direct_IO()
const struct address_space_operations fdmfs_aops = {
	.direct_IO = noop_direct_IO,
};

const struct inode_operations fdmfs_dir_inode_ops = {
	.lookup		= fdmfs_lookup,
	.create		= fdmfs_create,
	.link		= fdmfs_link,
	.unlink		= fdmfs_unlink,
	.mkdir		= fdmfs_mkdir,
	.rmdir		= fdmfs_rmdir,
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

const struct inode_operations fdmfs_inode_ops = {
	.setattr	= fdmfs_setattr,
	.getattr	= simple_getattr,
};

static int fdmfs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned flags, struct iomap *iomap, struct iomap *srcmap)
{
	struct fdmfs_inode *fdmfs_inode = FDMFS_I(inode);
	iomap->type = IOMAP_MAPPED;
	iomap->bdev = fdmfs_inode->sbi->cemu_bdev;
	iomap->addr = fdmfs_inode->region->off + offset;
	iomap->length = length;
	iomap->offset = offset;
	iomap->flags = 0;
	return 0;
}

static int fdmfs_iomap_end(struct inode *inode, loff_t offset, loff_t length,
			  ssize_t written, unsigned flags, struct iomap *iomap)
{
	return 0;
}

const struct iomap_ops fdmfs_iomap_ops = {
	.iomap_begin		= fdmfs_iomap_begin,
	.iomap_end		= fdmfs_iomap_end,
};
