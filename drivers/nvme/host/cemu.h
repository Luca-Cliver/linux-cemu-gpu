#ifndef __LINUX_CEMU_H__
#define __LINUX_CEMU_H__

#include <linux/pci.h>
#include <linux/io_uring/cmd.h>
#include <linux/cemu_ioctl.h>
#include "nvme.h"

#define CEMU_BLKDEV_NAME	"cemu"
#define CEMU_CHRDEV_NAME	"cemuc"
#define CEMU_MAX_MINOR		64
#define CEMU_SLM_BAR		2

struct cemu_bio {
	atomic_t		ref;
	unsigned		flags;
	int			error;
	size_t			done_before;
	bool			wait_for_completion;
	struct io_uring_cmd	*cmd;

	struct {
		struct iov_iter		*iter;
		struct task_struct	*waiter;
	} submit;

	/* nvme command field*/
	int pind;
	int ptype;
	int sel;
	int psize;
	int jit;
	int indirect;
	int target;
	int runtime;
	int runtime_scale;
};

struct cemu_dev {
	struct cdev cdev;
	struct gendisk *disk;
	struct request_queue *rq;
	struct request_queue *admin_q;
	struct request_queue *io_q;
	struct device *dev;
	struct block_device *nvme_bdev;
	struct pci_dev *pdev;
	struct scatterlist *dma_sgl;
	struct nvme_ctrl *ctrl;
	int sgl_nents;
	/*
	 * Index into the packed cemu_bdev[] / cemu_p2p_*[] arrays used by the
	 * NVMe host path for P2P address lookup.
	 */
	int idx;
	/* Minor number used for /dev node allocation (cemu majors). */
	int minor;
	size_t size;
	dma_addr_t p2p_addr;
	struct kobject *sys_fs;
	int cur_pind;
	int cur_rsid;
	struct list_head prog_list;
	struct mutex lock;
};

extern int cemu_bdev_major;
extern int cemu_bdev_minor;
extern int cemu_cdev_major;
extern int cemu_cdev_minor;
extern struct cemu_dev *cemu_bdev[CEMU_MAX_MINOR];
extern dma_addr_t cemu_p2p_start[CEMU_MAX_MINOR];
extern dma_addr_t cemu_p2p_end[CEMU_MAX_MINOR];

int cemu_dev_add(struct pci_dev *pdev, struct nvme_ctrl *ctrl);
void cemu_dev_remove(struct pci_dev *pdev, struct nvme_ctrl *ctrl);
size_t cemu_dev_get_size(struct block_device *bdev);
void *cemu_dev_get_p2p_addr(struct block_device *bdev);
void cemu_bio_end_io(struct bio *bio);

#endif /* __LINUX_CEMU_H__ */
