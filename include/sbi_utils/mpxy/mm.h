#ifndef __SBI_MPXY_MM_H__
#define __SBI_MPXY_MM_H__

#include<sbi/sbi_hartmask.h>
#include <sbi_utils/mpxy/fdt_mpxy.h>

#define RISCV_MSG_ID_SMM_VERSION		0x1
#define RISCV_MSG_ID_SMM_COMMUNICATE	0x4
#define RISCV_MSG_ID_SMM_EVENT_COMPLETE 0x3

#define SMM_VERSION_MAJOR        1
#define SMM_VERSION_MAJOR_SHIFT  16
#define SMM_VERSION_MAJOR_MASK   0x7FFF
#define SMM_VERSION_MINOR        0
#define SMM_VERSION_MINOR_SHIFT  0
#define SMM_VERSION_MINOR_MASK   0xFFFF
#define SMM_VERSION_FORM(major, minor) ((major << SMM_VERSION_MAJOR_SHIFT) | \
                                       (minor))
#define SMM_VERSION_COMPILED     SMM_VERSION_FORM(SMM_VERSION_MAJOR, \
                                                SMM_VERSION_MINOR)

struct mm_get_attributes {
	int status;
	u32 mm_version;
	u32 mm_shmem_addr_low;
	u32 mm_shmem_addr_high;
	u32 mm_shmem_size;
};

struct mm_cpu_info {
	u64 mpidr;
	u32 linear_id;
	u32 flags;
};

struct mm_boot_info {
	u64 mm_mem_base;
	u64 mm_mem_limit;
	u64 mm_image_base;
	u64 mm_stack_base;
	u64 mm_heap_base;
	u64 mm_ns_comm_buf_base;
	u64 mm_image_size;
	u64 mm_pcpu_stack_size;
	u64 mm_heap_size;
	u64 mm_ns_comm_buf_size;
	u32 num_mem_region;
	u32 num_cpus;
	u32 mm_channel_id;
	struct mm_cpu_info *cpu_info;
};

struct mm_boot_args {
	struct mm_boot_info boot_info;
	struct mm_cpu_info cpu_info[SBI_HARTMASK_MAX_BITS];
};

/**
 * Setup the boot infomation required by the Management mode.
 * This will include the MPXY channel id over which the
 * communication with Management Mode will take place.
 */
int mm_setup_bootinfo(const void *fdt, int nodeoff, const struct fdt_match *match,
								u32 *out_channel_id, u32 *out_channel_server_id);

#endif