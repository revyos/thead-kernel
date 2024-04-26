// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#include <linux/firmware/thead/ipc.h>

struct light_aon_msg_req_misc_set_ctrl {
	struct light_aon_rpc_msg_hdr hdr;
	u32 ctrl;
	u32 val;
	u16 resource;
	u16 reserved[7];
} __packed __aligned(1);

struct light_aon_msg_req_misc_get_ctrl {
	struct light_aon_rpc_msg_hdr hdr;
	u32 ctrl;
	u16 resource;
	u16 reserved[9];
} __packed __aligned(1);

struct light_aon_msg_resp_misc_get_ctrl {
	struct light_aon_rpc_ack_common ack_hdr;
	u32 val;
	u32 reserved[5];
} __packed __aligned(1);

int light_aon_misc_set_control(struct light_aon_ipc *ipc, u16 resource,
			    u32 ctrl, u32 val)
{
	struct light_aon_msg_req_misc_set_ctrl msg;
	struct light_aon_rpc_ack_common ack_msg;
	struct light_aon_rpc_msg_hdr *hdr = &msg.hdr;

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_MISC;
	hdr->func = (uint8_t)LIGHT_AON_MISC_FUNC_SET_CONTROL;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	RPC_SET_BE32(&msg.ctrl, 0, ctrl);
	RPC_SET_BE32(&msg.ctrl, 4, val);
	RPC_SET_BE16(&msg.ctrl, 8, resource);

	return light_aon_call_rpc(ipc, &msg, &ack_msg, true);
}
EXPORT_SYMBOL(light_aon_misc_set_control);

int light_aon_misc_get_control(struct light_aon_ipc *ipc, u16 resource,
			    u32 ctrl, u32 *val)
{
	struct light_aon_msg_req_misc_get_ctrl msg;
	struct light_aon_msg_resp_misc_get_ctrl resp;
	struct light_aon_rpc_msg_hdr *hdr = &msg.hdr;
	int ret;

	hdr->svc = (uint8_t)LIGHT_AON_RPC_SVC_MISC;
	hdr->func = (uint8_t)LIGHT_AON_MISC_FUNC_GET_CONTROL;
	hdr->size = LIGHT_AON_RPC_MSG_NUM;

	RPC_SET_BE32(&msg.ctrl, 0, ctrl);
	RPC_SET_BE16(&msg.ctrl, 4, resource);

	ret = light_aon_call_rpc(ipc, &msg, &resp, true);
	if (ret)
		return ret;

	if (val != NULL)
		RPC_GET_BE32(&resp.val, 0, val);

	return 0;
}
EXPORT_SYMBOL(light_aon_misc_get_control);
