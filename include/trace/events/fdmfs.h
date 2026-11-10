#undef TRACE_SYSTEM
#define TRACE_SYSTEM fdmfs

#if !defined(_TRACE_FDMFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FDMFS_H

#include <linux/nvme.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>

TRACE_EVENT(fdmfs_rw_begin,
	    TP_PROTO(struct kiocb *iocb, size_t bytes),
	    TP_ARGS(iocb, bytes),
	    TP_STRUCT__entry(
		__field(struct kiocb *, iocb)
		__field(loff_t, offset)
		__field(size_t, bytes)
		__string(name, iocb->ki_filp->f_path.dentry->d_name.name)
	    ),
	    TP_fast_assign(
		__entry->iocb = iocb;
		__entry->offset = iocb->ki_pos;
		__entry->bytes = bytes;
		__assign_str(name, iocb->ki_filp->f_path.dentry->d_name.name);
	    ),
	    TP_printk("iocb %p offset=%lld bytes=%zu name=%s", __entry->iocb,
		      __entry->offset, __entry->bytes, __get_str(name))
);

TRACE_EVENT(fdmfs_rw_middle,
	    TP_PROTO(struct kiocb *iocb),
	    TP_ARGS(iocb),
	    TP_STRUCT__entry(
		__field(struct kiocb *, iocb)
	    ),
	    TP_fast_assign(
		__entry->iocb = iocb;
	    ),
	    TP_printk("iocb %p", __entry->iocb)
);

TRACE_EVENT(fdmfs_rw_end,
	    TP_PROTO(struct kiocb *iocb),
	    TP_ARGS(iocb),
	    TP_STRUCT__entry(
		__field(struct kiocb *, iocb)
	    ),
	    TP_fast_assign(
		__entry->iocb = iocb;
	    ),
	    TP_printk("iocb %p", __entry->iocb)
);

TRACE_EVENT(fdmfs_copy_file_range_begin,
	    TP_PROTO(struct file *file),
	    TP_ARGS(file),
	    TP_STRUCT__entry(
		__field(struct file *, file)
	    ),
	    TP_fast_assign(
		__entry->file = file;
	    ),
	    TP_printk("file %p", __entry->file)
);

TRACE_EVENT(fdmfs_copy_file_range_end,
	    TP_PROTO(struct file *file),
	    TP_ARGS(file),
	    TP_STRUCT__entry(
		__field(struct file *, file)
	    ),
	    TP_fast_assign(
		__entry->file = file;
	    ),
	    TP_printk("file %p", __entry->file)
);

TRACE_EVENT(fdmfs_iomap_begin,
	    TP_PROTO(struct inode *inode),
	    TP_ARGS(inode),
	    TP_STRUCT__entry(
		__field(struct inode *, inode)
	    ),
	    TP_fast_assign(
		__entry->inode = inode;
	    ),
	    TP_printk("inode %p", __entry->inode)
);

TRACE_EVENT(fdmfs_iomap_end,
	    TP_PROTO(struct inode *inode),
	    TP_ARGS(inode),
	    TP_STRUCT__entry(
		__field(struct inode *, inode)
	    ),
	    TP_fast_assign(
		__entry->inode = inode;
	    ),
	    TP_printk("inode %p", __entry->inode)
);


#endif /* _TRACE_FDMFS_H */

// #undef TRACE_INCLUDE_PATH
// #define TRACE_INCLUDE_PATH .
// #undef TRACE_INCLUDE_FILE
// #define TRACE_INCLUDE_FILE fdmfs

/* This part must be outside protection */
#include <trace/define_trace.h>
