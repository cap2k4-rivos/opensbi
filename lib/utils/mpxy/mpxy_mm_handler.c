#include <sbi/sbi_error.h>
#include <sbi/sbi_heap.h>
#include <sbi/sbi_mpxy.h>
#include <libfdt.h>
#include <sbi_utils/mpxy/fdt_mpxy.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_console.h>

#define RISCV_MSG_ID_SMM_VERSION		0x1
#define RISCV_MSG_ID_SMM_COMMUNICATE	0x4
#define RISCV_MSG_ID_SMM_EVENT_COMPLETE 0x3
#define RISCV_MSG_SMM_MAX_LEN	64

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

static struct mm_get_attributes attr;

int sbi_mpxy_mm_message_handler(struct mpxy_channel_info *channel_info,
				  u32 msg_id, void *msgbuf, u32 msg_len,
			    void *respbuf, u32 resp_max_len,
			    unsigned long *ack_len, 
                struct sbi_domain *switch_to_domain)
{
	update_channel_data_len(channel_info->channel.channel_id, msg_len);

	if (RISCV_MSG_ID_SMM_VERSION == msg_id) {
		memset(&attr, 0, sizeof(struct mm_get_attributes));
		attr.status	 = 0;
		attr.mm_version = SMM_VERSION_COMPILED;
		attr.mm_shmem_addr_low = 0xFFE00000;
		attr.mm_shmem_size     = 0x200000;
		sbi_memcpy((void *)respbuf, &attr,
			   sizeof(struct mm_get_attributes));
		if (ack_len)
			*ack_len = sizeof(struct mm_get_attributes);
	} else if (RISCV_MSG_ID_SMM_EVENT_COMPLETE == msg_id) {
		sbi_domain_context_exit();

	} else if (RISCV_MSG_ID_SMM_COMMUNICATE == msg_id) {
        update_channel_data_len(channel_info->server_channel_id, 0);
		sbi_mpxy_copy_context(channel_info->channel_domain, channel_info->channel.channel_id,
                            switch_to_domain, channel_info->server_channel_id, 0);
		sbi_domain_context_enter(switch_to_domain);
        if(get_response_len(channel_info->channel.channel_id) == 0)
            return SBI_EFAIL;
	} else {
		return SBI_EFAIL;
	}

	return SBI_OK;
}