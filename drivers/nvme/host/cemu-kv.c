#include <linux/capability.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "cemu.h"
#include "../../../fs/fdmfs/fdmfs.h"

struct cemu_pinned_mrs {
	struct list_head list;
	u16 rsid;
	u32 count;
	bool input_registered;
	struct file *files[127];
	struct nvme_memory_range ranges[127];
};

struct cemu_kv_bind_wire {
	__le32 magic, version, nvm_nsid, batch_pages;
	__le64 session, generation, input_bytes;
	__le32 geometry[10];
	__le32 slots, protocol;
};

static_assert(sizeof(struct cemu_kv_bind_wire) == 88);
static_assert(offsetof(struct cemu_kv_bind_wire, input_bytes) == 32);
static_assert(offsetof(struct cemu_kv_bind_wire, slots) == 80);
static_assert(offsetof(struct cemu_kv_bind_wire, protocol) == 84);
static_assert(sizeof(struct ioctl_fdm_kv_bind) == 16);

static struct cemu_pinned_mrs *find_mrs(struct cemu_dev *dev, u16 rsid)
{
	struct cemu_pinned_mrs *entry;

	list_for_each_entry(entry, &dev->pinned_mrs, list)
		if (entry->rsid == rsid)
			return entry;
	return NULL;
}

static void release_mrs(struct cemu_pinned_mrs *entry)
{
	u32 index;

	if (entry->input_registered) {
		inode_lock(file_inode(entry->files[1]));
		fdmfs_unregister_input_locked(entry->files[1]);
		inode_unlock(file_inode(entry->files[1]));
	}
	for (index = 0; index < entry->count; index++) {
		fdmfs_unpin_memory_range(entry->files[index]);
		fput(entry->files[index]);
	}
	kfree(entry);
}

static int delete_device_mrs(struct cemu_dev *dev, u16 rsid)
{
	struct nvme_command cmd = {};
	int ret;

	cmd.common.opcode = 0x21;
	cmd.common.nsid = cpu_to_le32(3);
	cmd.common.cdw10 = cpu_to_le32(1 | ((u32)rsid << 16));
	ret = nvme_submit_sync_cmd(dev->admin_q, &cmd, NULL, 0);
	return ret > 0 ? -EREMOTEIO : ret;
}

int cemu_kv_create_mrs(struct cemu_dev *dev, unsigned long arg)
{
	struct ioctl_create_mrs request;
	struct cemu_pinned_mrs *entry;
	struct nvme_command cmd = {};
	union nvme_result result;
	int *fds;
	s64 *offsets, *sizes;
	int index, ret = -ENOMEM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&request, (void __user *)arg, sizeof(request)))
		return -EFAULT;
	if (request.nr_fd < 2 || request.nr_fd > 127)
		return -EINVAL;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	fds = memdup_user(u64_to_user_ptr(request.fd), request.nr_fd * sizeof(*fds));
	offsets = memdup_user(u64_to_user_ptr(request.off), request.nr_fd * sizeof(*offsets));
	sizes = memdup_user(u64_to_user_ptr(request.size), request.nr_fd * sizeof(*sizes));
	if (IS_ERR(fds) || IS_ERR(offsets) || IS_ERR(sizes)) {
		ret = IS_ERR(fds) ? PTR_ERR(fds) : IS_ERR(offsets) ? PTR_ERR(offsets) : PTR_ERR(sizes);
		goto out;
	}
	for (index = 0; index < request.nr_fd; index++) {
		struct file *file;
		struct nvme_memory_range *range = &entry->ranges[index];

		if (offsets[index] < 0 || offsets[index] % 512 ||
		    sizes[index] <= 0 || sizes[index] > U32_MAX || sizes[index] % 512) {
			ret = -EINVAL;
			goto out;
		}
		file = fget(fds[index]);
		if (!file) {
			ret = -EBADF;
			goto out;
		}
		ret = fdmfs_pin_memory_range(file, dev->disk->part0, range);
		if (ret) {
			fput(file);
			goto out;
		}
		entry->files[entry->count++] = file;
		if (offsets[index] > range->len || sizes[index] > range->len - offsets[index]) {
			ret = -EINVAL;
			goto out;
		}
		range->sb = cpu_to_le64(range->sb + offsets[index]);
		range->len = cpu_to_le32(sizes[index]);
		range->nsid = cpu_to_le32(range->nsid);
	}
	cmd.common.opcode = 0x21;
	cmd.common.nsid = cpu_to_le32(3);
	cmd.common.cdw11 = cpu_to_le32(entry->count);
	mutex_lock(&dev->mrs_lock);
	if (!list_empty(&dev->pinned_mrs)) {
		ret = -EBUSY;
		mutex_unlock(&dev->mrs_lock);
		goto out;
	}
	ret = __nvme_submit_sync_cmd(dev->admin_q, &cmd, &result, entry->ranges,
		entry->count * sizeof(entry->ranges[0]), NVME_QID_ANY, 0);
	if (!ret) {
		entry->rsid = le16_to_cpu(result.u16);
		list_add_tail(&entry->list, &dev->pinned_mrs);
		if (copy_to_user(&((struct ioctl_create_mrs __user *)arg)->rsid,
				 &entry->rsid, sizeof(entry->rsid))) {
			ret = -EFAULT;
			if (!delete_device_mrs(dev, entry->rsid)) {
				list_del(&entry->list);
				release_mrs(entry);
			} else {
				pr_err("CEMU pinned MRS %u retained after copyout failure\n", entry->rsid);
			}
		}
		entry = NULL;
	}
	mutex_unlock(&dev->mrs_lock);
out:
	if (!IS_ERR(fds))
		kfree(fds);
	if (!IS_ERR(offsets))
		kfree(offsets);
	if (!IS_ERR(sizes))
		kfree(sizes);
	if (entry)
		release_mrs(entry);
	return ret > 0 ? -EREMOTEIO : ret;
}

int cemu_kv_bind(struct cemu_dev *dev, unsigned long arg)
{
	struct ioctl_fdm_kv_bind request;
	struct cemu_kv_bind_wire wire;
	struct cemu_kv_bind_wire *dma_wire;
	struct cemu_pinned_mrs *entry;
	struct nvme_memory_range range = {};
	struct nvme_command cmd = {};
	u64 input_bytes;
	u32 slots;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&request, (void __user *)arg, sizeof(request)))
		return -EFAULT;
	if (request.bytes != sizeof(wire) || !request.pind)
		return -EINVAL;
	if (copy_from_user(&wire, u64_to_user_ptr(request.addr), sizeof(wire)))
		return -EFAULT;
	input_bytes = le64_to_cpu(wire.input_bytes);
	slots = le32_to_cpu(wire.slots);
	if (le32_to_cpu(wire.magic) != 0x31424b46 || le32_to_cpu(wire.version) != 2 ||
	    le32_to_cpu(wire.protocol) != 1 || !slots || slots > 2 ||
	    !input_bytes || input_bytes % 512 || input_bytes > MAX_RW_COUNT)
		return -EINVAL;
	dma_wire = kmemdup(&wire, sizeof(wire), GFP_KERNEL);
	if (!dma_wire)
		return -ENOMEM;
	mutex_lock(&dev->mrs_lock);
	entry = find_mrs(dev, request.rsid);
	if (!entry || entry->input_registered) {
		ret = entry ? -EBUSY : -ENOENT;
		goto out;
	}
	ret = fdmfs_get_memory_range(entry->files[1], &range);
	if (ret)
		goto out;
	if (range.sb != le64_to_cpu(entry->ranges[1].sb) ||
	    input_bytes * slots > le32_to_cpu(entry->ranges[1].len)) {
		ret = -EINVAL;
		goto out;
	}
	ret = fdmfs_register_input(entry->files[1], input_bytes);
	if (ret)
		goto out;
	entry->input_registered = true;
	cmd.common.opcode = 0x21;
	cmd.common.nsid = cpu_to_le32(3);
	cmd.common.cdw10 = cpu_to_le32(2 | ((u32)entry->rsid << 16));
	cmd.common.cdw11 = cpu_to_le32(sizeof(wire));
	cmd.common.cdw12 = cpu_to_le32(request.pind);
	ret = nvme_submit_sync_cmd(dev->admin_q, &cmd, dma_wire, sizeof(wire));
	if (!ret)
		fdmfs_enable_input(entry->files[1]);
out:
	mutex_unlock(&dev->mrs_lock);
	kfree(dma_wire);
	return ret > 0 ? -EREMOTEIO : ret;
}

int cemu_kv_delete_mrs(struct cemu_dev *dev, u16 rsid)
{
	struct cemu_pinned_mrs *entry;
	struct inode *inode = NULL;
	int ret = -ENOENT;

	mutex_lock(&dev->mrs_lock);
	entry = find_mrs(dev, rsid);
	if (!entry)
		goto out;
	if (!capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
		goto out;
	}
	if (entry->input_registered) {
		inode = file_inode(entry->files[1]);
		inode_lock(inode);
	}
	ret = delete_device_mrs(dev, rsid);
	if (!ret && entry->input_registered) {
		fdmfs_unregister_input_locked(entry->files[1]);
		entry->input_registered = false;
	}
	if (inode)
		inode_unlock(inode);
	if (!ret) {
		list_del(&entry->list);
		release_mrs(entry);
	}
out:
	mutex_unlock(&dev->mrs_lock);
	return ret;
}

void cemu_kv_cleanup(struct cemu_dev *dev)
{
	struct cemu_pinned_mrs *entry, *next;

	mutex_lock(&dev->mrs_lock);
	list_for_each_entry_safe(entry, next, &dev->pinned_mrs, list) {
		list_del(&entry->list);
		release_mrs(entry);
	}
	mutex_unlock(&dev->mrs_lock);
}
