/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Intel Corporation. All rights reserved.
 */

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

// RPMI Messages
#define RPMI_REQFWD_ENABLE_NOTIFICATION       0x1
#define RPMI_REQFWD_RETRIEVE_CURRENT_MESSAGE  0x2
#define RPMI_REQFWD_COMPLETE_CURRENT_MESSAGE  0x3

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

static struct sbi_domain *__get_domain(char* name)
{
	// int i;
	struct sbi_domain *dom = NULL;
	sbi_domain_for_each(dom)
	{
		if (!sbi_strcmp(dom->name, name)) {
			return dom;
		}
	}
	return NULL;
}


static int mpxy_mm_setup_bootinfo(const void *fdt, int nodeoff, const struct fdt_match *match,
								u32 *out_channel_id, u32 *out_channel_server_id)
{
	const u32 *prop_instance, *prop_value;
	u64 base64, size64;
	char name[64];
	int i, len, offset;

	struct mm_boot_args *boot_args = NULL;
	struct mm_boot_info *boot_info = NULL;
	struct sbi_domain *tdomain = NULL;

	prop_instance = fdt_getprop(fdt, nodeoff, "tdomain-instance", &len);
	if (!prop_instance || len < 4) {
		return SBI_EINVAL;
	}
	offset = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop_instance));
	if (offset < 0) {
		return SBI_EINVAL;
	}
	sbi_memset(name, 0, 64);
	strncpy(name, fdt_get_name(fdt, offset, NULL), sizeof(name));
	tdomain = __get_domain(name);
	if (NULL == tdomain)
		return SBI_EINVAL;

	boot_args = (void *)tdomain->next_arg1;
	boot_info = &boot_args->boot_info;

	prop_value = fdt_getprop(fdt, nodeoff, "num-regions", &len);
	if (!prop_value || len < 4)
		return SBI_EINVAL;
	boot_info->num_mem_region = (unsigned int)fdt32_to_cpu(*prop_value);

	prop_value = fdt_getprop(fdt, nodeoff, "memory-reg", &len);
	if (!prop_value || len < 16)
		return SBI_EINVAL;
	base64 = fdt32_to_cpu(prop_value[0]);
	base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
	size64 = fdt32_to_cpu(prop_value[2]);
	size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
	boot_info->mm_mem_base	= base64;
	boot_info->mm_mem_limit	= base64 + size64;

	prop_value = fdt_getprop(fdt, nodeoff, "image-reg", &len);
	if (!prop_value || len < 16)
		return SBI_EINVAL;
	base64 = fdt32_to_cpu(prop_value[0]);
	base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
	size64 = fdt32_to_cpu(prop_value[2]);
	size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
	boot_info->mm_image_base	= base64;
	boot_info->mm_image_size	= size64;

	prop_value = fdt_getprop(fdt, nodeoff, "heap-reg", &len);
	if (!prop_value || len < 16)
		return SBI_EINVAL;
	base64 = fdt32_to_cpu(prop_value[0]);
	base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
	size64 = fdt32_to_cpu(prop_value[2]);
	size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
	boot_info->mm_heap_base	= base64;
	boot_info->mm_heap_size	= size64;

	prop_value = fdt_getprop(fdt, nodeoff, "stack-reg", &len);
	if (!prop_value || len < 16)
		return SBI_EINVAL;
	base64 = fdt32_to_cpu(prop_value[0]);
	base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
	size64 = fdt32_to_cpu(prop_value[2]);
	size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
	boot_info->mm_stack_base	= base64 + size64 -1;

	prop_value = fdt_getprop(fdt, nodeoff, "pcpu-stack-size", &len);
	if (!prop_value || len < 4)
		return SBI_EINVAL;
	boot_info->mm_pcpu_stack_size = (unsigned long)fdt32_to_cpu(*prop_value);

	prop_value = fdt_getprop(fdt, nodeoff, "ns-comm-buf", &len);
	if (!prop_value || len < 16)
		return SBI_EINVAL;
	base64 = fdt32_to_cpu(prop_value[0]);
	base64 = (base64 << 32) | fdt32_to_cpu(prop_value[1]);
	size64 = fdt32_to_cpu(prop_value[2]);
	size64 = (size64 << 32) | fdt32_to_cpu(prop_value[3]);
	boot_info->mm_ns_comm_buf_base	= base64;
	boot_info->mm_ns_comm_buf_size	= size64;
	boot_info->num_cpus = 0;
	sbi_hartmask_for_each_hartindex(i, tdomain->possible_harts) {
		boot_args->cpu_info[i].linear_id = sbi_hartindex_to_hartid(i);
		boot_args->cpu_info[i].flags = 0;
		boot_info->num_cpus += 1;
	}
	boot_info->cpu_info = boot_args->cpu_info;

	prop_value = fdt_getprop(fdt, nodeoff, "riscv,sbi-mpxy-channel-id", &len);
	if (!prop_value || len < 4)
		return SBI_EINVAL;
	*out_channel_id = (unsigned int)fdt32_to_cpu(*prop_value);
	boot_info->mm_channel_id = *out_channel_id;

	prop_value = fdt_getprop(fdt, nodeoff, "riscv,sbi-mpxy-channel-server-id", &len);
	if (!prop_value || len < 4)
		return SBI_EINVAL;
	*out_channel_server_id = (unsigned int)fdt32_to_cpu(*prop_value);

	return 0;
}

static struct sbi_domain *mpxy_get_server_domain(u32 server_channel_id)
{
	struct sbi_mpxy_channel *server_channel = sbi_mpxy_find_channel(server_channel_id);
	struct mpxy_channel_info *server_channel_info = 
		container_of(server_channel, struct mpxy_channel_info, channel);
	struct sbi_domain *server_domain = server_channel_info->channel_domain;
	return server_domain;
}

static void *mpxy_get_respbuf(u32 msg_id, struct sbi_mpxy_channel *channel, void *msgbuf,
							enum MpxyServiceGroup service_group)
{
	void *respbuf;
	struct mpxy_channel_info *channel_info = 
		container_of(channel, struct mpxy_channel_info, channel);

	if(service_group == 0xB){
		if(RISCV_MSG_ID_SMM_VERSION == msg_id)
			return msgbuf;
	} else {
		if(RPMI_REQFWD_ENABLE_NOTIFICATION == msg_id)
			return NULL;
	}
	struct sbi_domain *server_domain = mpxy_get_server_domain(channel_info->server_channel_id);
	respbuf = sbi_get_domain_shmem_base(server_domain);
	return respbuf;
}

static int mpxy_mm_send_message(struct sbi_mpxy_channel *channel,
				  u32 msg_id, void *msgbuf, u32 msg_len,
			    void *respbuf, u32 resp_max_len,
			    unsigned long *ack_len)
{
	int ret;
	struct mpxy_channel_info *channel_info = 
		container_of(channel, struct mpxy_channel_info, channel);

	enum MpxyServiceGroup service_group = channel_info->service_group;

	void *resp_buf = mpxy_get_respbuf(msg_id, channel, msgbuf, service_group);

	struct sbi_domain *server_domain = mpxy_get_server_domain(channel_info->server_channel_id);
	
	if(service_group == 0xB){
		ret = sbi_mpxy_mm_message_handler(channel_info, msg_id, msgbuf, msg_len, 
								resp_buf, resp_max_len, ack_len, server_domain);
		if(ret != SBI_SUCCESS)
			return SBI_EFAIL;
	} else {
		ret = sbi_mpxy_reqfwd_message_handler(channel_info, msg_id, msgbuf, msg_len, 
								resp_buf, resp_max_len, ack_len, server_domain);
		if(ret != SBI_SUCCESS)
			return SBI_EFAIL;
	}

	return SBI_OK;
}

static int initialise_channel(struct mpxy_channel_info *channel_info, const void *fdt, int nodeoff, const struct fdt_match *match)
{
	const u32 *prop_value;
	int rc, len;
	struct sbi_domain *channel_domain = NULL;

	u32 channel_id = 0;
	u32 channel_server_id = 0;

	prop_value = fdt_getprop(fdt, nodeoff, "channel-domain", &len);
	if(prop_value){
		int domain_offset = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*prop_value));
        if (domain_offset < 0) {
            sbi_printf("MPXY: Invalid phandle for 'channel-domain'.\n");
            return SBI_EINVAL;
        }
        char dom_name[64];
        sbi_memset(dom_name, 0, sizeof(dom_name));
        const char *dn = fdt_get_name(fdt, domain_offset, NULL);
        if (dn) {
            sbi_strncpy(dom_name, dn, sizeof(dom_name) - 1);
            dom_name[sizeof(dom_name) - 1] = '\0';
        } else {
            sbi_printf("MPXY: Failed to get name for 'channel-domain'.\n");
            return SBI_EINVAL;
        }
		channel_domain = __get_domain(dom_name);
		if(!channel_domain){
			sbi_printf("MPXY: Channel domain '%s' not found.\n", dom_name);
			return SBI_EINVAL;
		}
	} else {
        channel_domain = sbi_domain_thishart_ptr();
        if (!channel_domain) {
             sbi_printf("MPXY: No 'channel-domain' specified and current domain not found.\n");
             return SBI_EINVAL;
        }
    }

	if(sbi_strncmp("trusted-domain", channel_domain->name, 14) == 0){
		channel_info->service_group = RequestForward;
		rc = mpxy_mm_setup_bootinfo(fdt, nodeoff, match,
                                    &channel_id, &channel_server_id);
        if (rc != SBI_SUCCESS) {
            return rc;
        }
	} else {
		channel_info->service_group = ManagementMode;
        prop_value = fdt_getprop(fdt, nodeoff, "riscv,sbi-mpxy-channel-id", &len);
        if (!prop_value || len < 4) { sbi_printf("MPXY: Missing 'riscv,sbi-mpxy-channel-id'.\n"); return SBI_EINVAL; }
        channel_id = fdt32_to_cpu(*prop_value);

        prop_value = fdt_getprop(fdt, nodeoff, "riscv,sbi-mpxy-channel-server-id", &len);
        if (!prop_value || len < 4) { sbi_printf("MPXY: Missing 'riscv,sbi-mpxy-channel-server-id'.\n"); return SBI_EINVAL; }
        channel_server_id = fdt32_to_cpu(*prop_value);
    }

	channel_info->channel_domain = channel_domain;
	channel_info->channel.channel_id = channel_id;
	channel_info->server_channel_id = channel_server_id;
	return SBI_SUCCESS;
}

static int mpxy_mm_init(const void *fdt, int nodeoff, const struct fdt_match *match)
{
	int rc;
	struct mpxy_channel_info *channel_info;

	/* Allocate context for MPXY channel */
	channel_info = sbi_zalloc(sizeof(struct mpxy_channel_info));
	if (!channel_info)
		return SBI_ENOMEM;

	rc = initialise_channel(channel_info, fdt, nodeoff, match);
	if (rc != SBI_SUCCESS) {
        sbi_printf("MPXY: Failed to initialize channel from DT node (error %d).\n", rc);
        sbi_free(channel_info);
        return rc;
    }

	channel_info->channel.send_message_with_response = mpxy_mm_send_message;
	channel_info->channel.attrs.msg_data_maxlen = 4096;
	rc = sbi_mpxy_register_channel(&channel_info->channel);
	if (rc) {
		sbi_free(channel_info);
		return rc;
	}

	return 0;
}

static const struct fdt_match mpxy_mm_match[] = {
	{ .compatible = "riscv,sbi-mpxy-mm", .data = NULL },
	{ .compatible = "riscv,sbi-mpxy-uefi", .data = NULL },
	{},
};

struct fdt_driver fdt_mpxy_mm = {
	.match_table = mpxy_mm_match,
	.init = mpxy_mm_init,
};
