#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/falloc.h>
#include <linux/iomap.h>

#include "fdmfs.h"

#define CREATE_TRACE_POINTS

#include <trace/events/fdmfs.h>

static int fdmfs_open(struct inode *inode, struct file *filp) {
	// pr_info("FDMFS: open\n");
	filp->private_data = inode->i_private;
	filp->f_mode |= FMODE_NOWAIT;
	return 0;
}

static int fdmfs_release(struct inode *inode, struct file *filp) {
	// pr_info("FDMFS: close\n");
	return 0;
}

static ssize_t fdmfs_rw_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	// pr_info("FDMFS: rw_iter, size %lu, off %llu\n", iov_iter_count(iter), iocb->ki_pos);
	if (iocb->ki_pos % 512) {
		pr_err("FDMFS: rw_iter require 512-aligned offset!\n");
		return -EINVAL;
	}

	trace_fdmfs_rw_begin(iocb);
	struct inode *ino = file_inode(iocb->ki_filp);

	inode_lock_shared(ino);
	ssize_t ret = iomap_dio_rw(iocb, iter, &fdmfs_iomap_ops, NULL, 0, NULL, 0);
	trace_fdmfs_rw_middle(iocb);
	inode_unlock_shared(ino);
	file_accessed(iocb->ki_filp);
	trace_fdmfs_rw_end(iocb);
	return ret;
}

ssize_t fdmfs_copy_file_range_kiocb(struct kiocb *kiocb, struct file *file_in,
					loff_t pos_in, struct file *file_out,
					loff_t pos_out, size_t size)
{
	trace_fdmfs_copy_file_range_begin(file_in);
	bool in_is_fdmfs = file_in->f_op == &fdmfs_fops;
	bool out_is_fdmfs = file_out->f_op == &fdmfs_fops;
	struct fdmfs_inode *inode = in_is_fdmfs ? file_in->private_data : file_out->private_data;
	struct iov_iter iter;
	struct bio_vec bvec;
	loff_t fdm_off;
	ssize_t ret;

	// pr_info("FDMFS: copy_file_range %zu bytes, pos_in %llu, pos_out %llu, in_is_fdmfs %d, out_is_fdmfs %d\n", size, pos_in, pos_out, in_is_fdmfs, out_is_fdmfs);

	if (in_is_fdmfs) {
		kiocb->ki_filp = file_out;
		kiocb->ki_pos = pos_out;
		fdm_off = pos_in;
	} else {
		kiocb->ki_filp = file_in;
		kiocb->ki_pos = pos_in;
		fdm_off = pos_out;
	}
	kiocb->ki_flags |= kiocb->ki_filp->f_iocb_flags,

	bvec_set_virt(&bvec, fdmfs_region_addr(inode) + fdm_off, size);
	unsigned int dir = in_is_fdmfs ? ITER_SOURCE : ITER_DEST;
	iov_iter_bvec(&iter, dir, &bvec, 1, size);

	trace_fdmfs_copy_file_range_end(file_in);
	if (in_is_fdmfs)
		ret = call_write_iter(file_out, kiocb, &iter);
	else
		ret = call_read_iter(file_in, kiocb, &iter);
	return ret;
}

ssize_t fdmfs_copy_file_range(struct file *file_in, loff_t pos_in,
				     struct file *file_out, loff_t pos_out,
				     size_t size, unsigned int flags)
{
	trace_fdmfs_copy_file_range_begin(file_in);
	bool in_is_fdmfs = file_in->f_op == &fdmfs_fops;
	bool out_is_fdmfs = file_out->f_op == &fdmfs_fops;
	struct fdmfs_inode *inode = in_is_fdmfs ? file_in->private_data : file_out->private_data;
	struct kiocb kiocb;
	struct iov_iter iter;
	struct bio_vec bvec;
	loff_t fdm_off;
	ssize_t ret;

	// pr_info("FDMFS: copy_file_range %zu bytes, pos_in %llu, pos_out %llu, in_is_fdmfs %d, out_is_fdmfs %d\n", size, pos_in, pos_out, in_is_fdmfs, out_is_fdmfs);

	if (in_is_fdmfs) {
		init_sync_kiocb(&kiocb, file_out);
		kiocb.ki_pos = pos_out;
		fdm_off = pos_in;
	} else {
		init_sync_kiocb(&kiocb, file_in);
		kiocb.ki_pos = pos_in;
		fdm_off = pos_out;
	}

	if (flags & COPY_FILE_ASYNC)
		kiocb.ki_flags |= IOCB_NOWAIT;

	bvec_set_virt(&bvec, fdmfs_region_addr(inode) + fdm_off, size);
	unsigned int dir = in_is_fdmfs ? ITER_SOURCE : ITER_DEST;
	iov_iter_bvec(&iter, dir, &bvec, 1, size);

	if (in_is_fdmfs)
		ret = call_write_iter(file_out, &kiocb, &iter);
	else
		ret = call_read_iter(file_in, &kiocb, &iter);
	trace_fdmfs_copy_file_range_end(file_in);
	return ret;
}

void fdmfs_deallocate(struct fdmfs_inode *inode)
{
	struct fdmfs_sb_info *sbi = inode->sbi;
	struct fdm_region *region = inode->region;
	struct fdm_region *r;

	if (region == NULL)
		return;

	list_for_each_entry(r, &sbi->fdm_free, list) {
		if (region->off + region->size == r->off) {
			r->size += region->size;
			r->off -= region->size;
			list_del(&region->list);
			kfree(region);
			goto out;
		} else if (region->off + region->size < region->off) {
			list_del(&region->list);
			list_add_tail(&region->list, &r->list);
			goto out;
		}
	}

	list_del(&region->list);
	list_add_tail(&region->list, &sbi->fdm_free);

out:
	inode->region = NULL;
	i_size_write(inode->inode, 0);
	return;
}

static long fdmfs_fallocate(struct file *filp, int mode,
				loff_t offset, loff_t length)
{
	struct inode *inode = file_inode(filp);
	struct fdmfs_inode *fdmfs_inode = filp->private_data;
	struct fdmfs_sb_info *sbi = fdmfs_inode->sbi;
	struct fdm_region *region;
	struct fdm_region *new_region;
	int ret = 0;

	pr_info("FDMFS: fallocate mode %d, offset %lld, length %lld\n",
		mode, offset, length);

	if (mode != 0) {
		pr_err("FDMFS: fallocate doesn't support mode!\n");
		return -EINVAL;
	}

	if (offset != 0) {
		pr_err("FDMFS: fallocate doesn't support offset!\n");
		return -EINVAL;
	}

	if (length % 4096) {
		pr_err("FDMFS: fallocate require 4096-aligned length!\n");
		return -EINVAL;
	}

	inode_lock(inode);

	if (filp->f_inode->i_size != 0 || offset != 0) {
		pr_err("FDMFS: fallocate doesn't support truncate\n");
		ret = -EOPNOTSUPP;
		goto err;
	}

	ret = inode_newsize_ok(inode, length);
	if (ret)
		goto err;

	ret = file_modified(filp);
	if (ret)
		goto err;

	// find free region with equal size first
	list_for_each_entry(region, &sbi->fdm_free, list) {
		if (region->size == length) {
			list_del(&region->list);
			new_region = region;
			goto out;
		}
	}

	list_for_each_entry(region, &sbi->fdm_free, list) {
		if (region->size > length) {
			new_region = kzalloc(sizeof(struct fdm_region), GFP_KERNEL);
			new_region->off = region->off;
			new_region->size = length;
			region->size -= length;
			region->off += length;
			goto out;
		}
	}

	ret = -ENOMEM;
	goto err;
out:
	list_add(&new_region->list, &sbi->fdm_used);
	FDMFS_I(inode)->region = new_region;
	if (!(mode & FALLOC_FL_KEEP_SIZE)) {
		i_size_write(inode, length);
		inode->i_blocks = (length+(1<<inode->i_blkbits)-1) >> inode->i_blkbits;
	}
	inode_set_ctime_current(inode);
	pr_info("FDMFS: fallocate success, inode size %lld\n", filp->f_inode->i_size);
err:
	inode_unlock(inode);
	return ret;
}

int fdmfs_get_memory_range(struct file *filp, struct nvme_memory_range *mr)
{
	struct fdmfs_inode *fdmfs_inode;
	struct fdm_region *region;

	if (!filp || !mr || !file_inode(filp) ||
	    file_inode(filp)->i_sb->s_magic != FDMFS_MAGIC)
		return -EINVAL;

	fdmfs_inode = filp->private_data;
	if (!fdmfs_inode)
		return -EINVAL;
	region = fdmfs_inode->region;

	if (region == NULL)
		return -EINVAL;

	mr->nsid = 2;
	mr->len = region->size;
	mr->sb = region->off;
	return 0;
}
EXPORT_SYMBOL_GPL(fdmfs_get_memory_range);

const struct file_operations fdmfs_fops = {
	.open			= fdmfs_open,
	.release		= fdmfs_release,
	.read_iter		= fdmfs_rw_iter,
	.write_iter		= fdmfs_rw_iter,
	.copy_file_range	= fdmfs_copy_file_range,
	.fallocate		= fdmfs_fallocate,
	.iopoll			= iocb_bio_iopoll,
	.fsync			= noop_fsync,
	.llseek			= generic_file_llseek,
};
