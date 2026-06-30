/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_CEMU_IOCTL_H
#define _UAPI_LINUX_CEMU_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * CEMU ioctl ABI
 *
 * Use standard Linux ioctl encoding macros so command values are unique and
 * carry argument size information.
 *
 * Pointer fields are represented as __u64 in the ABI. Kernel code should cast
 * them to (void __user *)(uintptr_t) before using copy_from_user helpers.
 */
#define CEMU_IOCTL_MAGIC  'C'

enum {
	PROGRAM_TARGET_HOST,
	PROGRAM_TARGET_CUDA_DEVPTR,
	PROGRAM_TARGET_INVALID,
};

/* IOCTL_CEMU_DOWNLOAD / IOCTL_CEMU_UNLOAD / ACTIVATE / DEACTIVATE argument */
struct ioctl_download {
	__u64	name;          /* const char * (user pointer) */
	__u64	addr;          /* void * (user pointer) */
	__s32	size;
	__s32	ptype;
	__s32	runtime;
	__s32	runtime_scale;
	__s32	jit;
	__s32	indirect;
	__s32	target;
	__s32	pind;          /* out for DOWNLOAD */
};

/* IOCTL_CEMU_EXECUTE argument */
struct ioctl_execute {
	__u64	cparam1;
	__u64	cparam2;
	__u64	memory_fd;     /* int * (user pointer) */
	__u64	buffer;        /* void * (user pointer) */
	__u16	nr_fd;
	__u16	buffer_len;
	__u16	pind;
	__u16	rsid;
	__u32	runtime;
};

/* IOCTL_CEMU_CREATE_MRS / IOCTL_CEMU_DELETE_MRS argument */
struct ioctl_create_mrs {
	__s32	nr_fd;
	__u64	fd;            /* int * (user pointer) */
	__u64	off;           /* long long * (user pointer) */
	__u64	size;          /* long long * (user pointer) */
	__u16	rsid;          /* out for CREATE_MRS / in for DELETE_MRS */
};

#define IOCTL_CEMU_DOWNLOAD    _IOWR(CEMU_IOCTL_MAGIC, 0x00, struct ioctl_download)
#define IOCTL_CEMU_UNLOAD      _IOW (CEMU_IOCTL_MAGIC, 0x01, struct ioctl_download)
#define IOCTL_CEMU_ACTIVATE    _IOW (CEMU_IOCTL_MAGIC, 0x02, struct ioctl_download)
#define IOCTL_CEMU_DEACTIVATE  _IOW (CEMU_IOCTL_MAGIC, 0x03, struct ioctl_download)
#define IOCTL_CEMU_EXECUTE     _IOW (CEMU_IOCTL_MAGIC, 0x04, struct ioctl_execute)
#define IOCTL_CEMU_CREATE_MRS  _IOWR(CEMU_IOCTL_MAGIC, 0x05, struct ioctl_create_mrs)
#define IOCTL_CEMU_DELETE_MRS  _IOW (CEMU_IOCTL_MAGIC, 0x06, struct ioctl_create_mrs)

#endif /* _UAPI_LINUX_CEMU_IOCTL_H */
