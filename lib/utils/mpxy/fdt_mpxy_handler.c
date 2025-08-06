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
#include <sbi_utils/mpxy/mm.h>
#include <sbi_utils/mailbox/rpmi_msgprot.h>

static struct sbi_domain *mpxy_get_server_domain(u32 server_channel_id)
{
	struct sbi_mpxy_channel *server_channel = sbi_mpxy_find_channel(server_channel_id);
	struct mpxy_channel_info *server_channel_info = 
		container_of(server_channel, struct mpxy_channel_info, channel);
	struct sbi_domain *server_domain = server_channel_info->channel_domain;
	return server_domain;
}

static void *mpxy_get_respbuf(u32 msg_id, struct sbi_mpxy_channel *channel, void *msgbuf,
							enum rpmi_servicegroup_id service_group)
{
	void *respbuf;
	struct mpxy_channel_info *channel_info = 
		container_of(channel, struct mpxy_channel_info, channel);

	if(RPMI_SRVGRP_MANAGEMENT_MODE == service_group){
		if(RISCV_MSG_ID_SMM_GET_ATTRIBUTES == msg_id)
			return msgbuf;
	} else if(RPMI_SRVGRP_REQUEST_FORWARD == service_group){
		if(RPMI_REQFWD_SRV_ENABLE_NOTIFICATION == msg_id)
			return NULL;
	} else{
		return NULL;
	}
	struct sbi_domain *server_domain = mpxy_get_server_domain(channel_info->server_channel_id);
	respbuf = sbi_get_domain_shmem_base(server_domain);
	return respbuf;
}

static int mpxy_handle_send_message(struct sbi_mpxy_channel *channel,
				  u32 msg_id, void *msgbuf, u32 msg_len,
			    void *respbuf, u32 resp_max_len,
			    unsigned long *ack_len)
{
	int ret;
	struct mpxy_channel_info *channel_info = 
		container_of(channel, struct mpxy_channel_info, channel);

	enum rpmi_servicegroup_id service_group = channel_info->service_group;

	void *resp_buf = mpxy_get_respbuf(msg_id, channel, msgbuf, service_group);

	struct sbi_domain *server_domain = mpxy_get_server_domain(channel_info->server_channel_id);
	
	if(service_group == RPMI_SRVGRP_MANAGEMENT_MODE){
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
		channel_domain = get_domain(dom_name);
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
		channel_info->service_group = RPMI_SRVGRP_REQUEST_FORWARD;
		rc = mm_setup_bootinfo(fdt, nodeoff, match,
                                    &channel_id, &channel_server_id);
        if (rc != SBI_SUCCESS) {
            return rc;
        }
	} else {
		channel_info->service_group = RPMI_SRVGRP_MANAGEMENT_MODE;
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

static int mpxy_init(const void *fdt, int nodeoff, const struct fdt_match *match)
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
	sbi_printf("[OpenSBI MPXY] Channel with channel id: %d initialised!\n", channel_info->channel.channel_id);

	channel_info->channel.send_message_with_response = mpxy_handle_send_message;
	channel_info->channel.attrs.msg_data_maxlen = 4096;
	rc = sbi_mpxy_register_channel(&channel_info->channel);
	if (rc) {
		sbi_free(channel_info);
		return rc;
	}

	return 0;
}

static const struct fdt_match mpxy_match[] = {
	{ .compatible = "riscv,sbi-mpxy-mm", .data = NULL },
	{ .compatible = "riscv,sbi-mpxy-smm", .data = NULL },
	{},
};

struct fdt_driver fdt_mpxy_handler = {
	.match_table = mpxy_match,
	.init = mpxy_init,
};
