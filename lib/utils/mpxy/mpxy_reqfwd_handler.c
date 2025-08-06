#include <sbi/sbi_error.h>
#include <sbi/sbi_heap.h>
#include <sbi/sbi_mpxy.h>
#include <libfdt.h>
#include <sbi_utils/mpxy/fdt_mpxy.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_console.h>
#include <sbi_utils/mailbox/rpmi_msgprot.h>

#define RPMI_HDR_LEN sizeof(struct rpmi_message_header)
#define RPMI_MM_COMM_SIZE 16

struct rpmi_message_header prepare_rpmi_message_header(u32 payload_len)
{
	struct rpmi_message_header hdr;
	hdr.servicegroup_id = RPMI_SRVGRP_REQUEST_FORWARD;
	hdr.service_id = RPMI_REQFWD_SRV_RETRIEVE_CURRENT_MESSAGE;
	hdr.flags = RPMI_MSG_ACKNOWLDGEMENT;
	hdr.token = 0;
	hdr.datalen = payload_len;
	return hdr;
}

static int sbi_mpxy_copy_rpmi_payload(struct sbi_domain *current_domain, u32 channel_id, u32 msg_len, unsigned long src_addr)
{
	void *src_shmem_base = sbi_get_domain_shmem_base(current_domain);
	sbi_memcpy(src_shmem_base, (void *)src_addr, msg_len);
	update_channel_data_len(channel_id, get_channel_data_len(channel_id) + msg_len);
	return SBI_SUCCESS;
}

static struct rpmi_reqfwd_complete_message_resp 
		get_rpmi_response_packet_reqfwd_complete(u32 ret_status, u16 payload_len)
{
	struct rpmi_reqfwd_complete_message_resp final_message;
	struct reqfwd_complete_message_resp complete_resp;

	complete_resp.status = ret_status;
	final_message.resp = complete_resp;
	final_message.hdr = prepare_rpmi_message_header(payload_len);
	
	return final_message;
}

static struct rpmi_reqfwd_retrieve_message_resp 
		get_rpmi_response_packet_reqfwd_retrieve(u32 ret_status, u16 payload_len)
{
	struct rpmi_reqfwd_retrieve_message_resp final_message;
	struct reqfwd_retrieve_message_resp complete_resp;

	complete_resp.status = ret_status;
	complete_resp.remaining = 0;
	complete_resp.returned = RPMI_MM_COMM_SIZE;

	final_message.resp = complete_resp;
	final_message.hdr = prepare_rpmi_message_header(payload_len);
	
	return final_message;
}

/** 
 * TODO: In future we can add a lot more validation and sanity checks
 * to validate the RPMI header and also how the total packet is formed. 
 */
static int validate_rpmi_hdr(struct sbi_domain *current_domain,
							u32 channel_id,
							u32 msg_len,
							void *msgbuf)
{
	struct rpmi_message_header rhdr;
	memset(&rhdr, 0, sizeof(struct rpmi_message_header));
	sbi_memcpy(&rhdr, msgbuf, sizeof(struct rpmi_message_header));

	if(rhdr.servicegroup_id != RPMI_SRVGRP_REQUEST_FORWARD)
		return SBI_EFAIL;

	switch(rhdr.service_id){
		case RPMI_REQFWD_SRV_ENABLE_NOTIFICATION:
		case RPMI_REQFWD_SRV_RETRIEVE_CURRENT_MESSAGE:
		case RPMI_REQFWD_SRV_COMPLETE_CURRENT_MESSAGE:
			break;
		default:
			return SBI_EFAIL;
	}

	switch(rhdr.flags){
		case RPMI_MSG_NORMAL_REQUEST:
		case RPMI_MSG_POSTED_REQUEST:
		case RPMI_MSG_ACKNOWLDGEMENT:
		case RPMI_MSG_NOTIFICATION:
			break;
		default:
			return SBI_EFAIL;
	}

	return SBI_SUCCESS;
}

int mpxy_copy_rpmi_resp_complete_msg(struct rpmi_reqfwd_complete_message_resp *resp, 
				u32 channel_id, struct sbi_domain *current_domain)
{
	update_channel_data_len(channel_id, 0);
	*resp = get_rpmi_response_packet_reqfwd_complete(RPMI_SUCCESS, sizeof(struct rpmi_reqfwd_complete_message_resp));

	if(sbi_mpxy_copy_rpmi_payload(current_domain,
								channel_id,
								sizeof(struct rpmi_reqfwd_complete_message_resp),
								(unsigned long)resp))
		return SBI_EFAIL;

	return SBI_SUCCESS;
}

static int mpxy_copy_rpmi_resp_retrieve_msg(struct rpmi_reqfwd_retrieve_message_resp *resp, 
				u32 channel_id, struct sbi_domain *current_domain)
{
	update_channel_data_len(channel_id, 0);
	*resp = get_rpmi_response_packet_reqfwd_retrieve(RPMI_SUCCESS, sizeof(struct rpmi_reqfwd_retrieve_message_resp));

	if(sbi_mpxy_copy_rpmi_payload(current_domain,
							channel_id,
							sizeof(struct rpmi_reqfwd_retrieve_message_resp), 
							(unsigned long)resp))
		return SBI_EFAIL;

	return SBI_SUCCESS;
}

int sbi_mpxy_reqfwd_message_handler(struct mpxy_channel_info *channel_info,
				  u32 msg_id, void *msgbuf, u32 msg_len,
			    void *respbuf, u32 resp_max_len,
			    unsigned long *ack_len, 
                struct sbi_domain *switch_to_domain)
{
	int ret;

	update_channel_data_len(channel_info->channel.channel_id, msg_len);

	ret = validate_rpmi_hdr(channel_info->channel_domain, channel_info->channel.channel_id, msg_len, msgbuf);
	if(ret != SBI_SUCCESS){
		return SBI_EFAIL;
	}
	sbi_printf("[OpenSBI MPXY] Request Forward Service Handler: channel_id: %d, message_id: %d, message_type: ", channel_info->channel.channel_id, msg_id);
	if (RPMI_REQFWD_SRV_RETRIEVE_CURRENT_MESSAGE == msg_id) {
		sbi_printf("REQFWD_RETRIEVE_CURRENT_MESSAGE\n");
		update_channel_data_len(channel_info->channel.channel_id, 0);

		struct rpmi_reqfwd_retrieve_message_resp resp_struct;
		memset(&resp_struct, 0, sizeof(struct rpmi_reqfwd_retrieve_message_resp));

		ret = mpxy_copy_rpmi_resp_retrieve_msg(&resp_struct, channel_info->channel.channel_id, 
										channel_info->channel_domain);
		if (ret != SBI_SUCCESS)
			return SBI_EFAIL;

		ret = sbi_mpxy_copy_context(switch_to_domain, channel_info->server_channel_id,
							channel_info->channel_domain, channel_info->channel.channel_id, 0);
		if(ret != SBI_SUCCESS)
			return SBI_EFAIL;

		if(get_channel_data_len(channel_info->channel.channel_id) == 0)
            return SBI_EFAIL;
		
		*ack_len = get_channel_data_len(channel_info->channel.channel_id);

	} else if (RPMI_REQFWD_SRV_COMPLETE_CURRENT_MESSAGE == msg_id) {
		sbi_printf("REQFWD_COMPLETE_CURRENT_MESSAGE\n");
		ret = check_shmem_initialised(switch_to_domain);
		if(ret != SBI_SUCCESS) {
			sbi_domain_context_exit();
		} else {
			update_channel_data_len(channel_info->server_channel_id, 0);
			
			ret = sbi_mpxy_copy_context(channel_info->channel_domain, channel_info->channel.channel_id,
								switch_to_domain, channel_info->server_channel_id, RPMI_HDR_LEN);
			
			if(ret != SBI_SUCCESS)
				return SBI_EFAIL;
			
			sbi_domain_context_exit();

			struct rpmi_reqfwd_complete_message_resp resp_struct;
			memset(&resp_struct, 0, sizeof(struct rpmi_reqfwd_complete_message_resp));

			ret = mpxy_copy_rpmi_resp_complete_msg(&resp_struct, channel_info->channel.channel_id,
											channel_info->channel_domain);
			if(ret != SBI_SUCCESS)
				return SBI_EFAIL;
				
			if(get_channel_data_len(channel_info->channel.channel_id) == 0)
				return SBI_EFAIL;
			
			*ack_len = get_channel_data_len(channel_info->channel.channel_id);
		}

	} else {
		return SBI_EFAIL;
	}

	return SBI_OK;
}