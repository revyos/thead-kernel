// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */

#include <linux/err.h>
#include <linux/firmware/thead/ipc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/light_proc_debug.h>
#include <linux/firmware/thead/ipc.h>

/* wait for response for 3000ms instead of 300ms (fix me pls)*/
#define MAX_RX_TIMEOUT		(msecs_to_jiffies(3000))
#define MAX_TX_TIMEOUT		(msecs_to_jiffies(500))

struct light_aon_chan {
	struct light_aon_ipc *aon_ipc;

	struct mbox_client cl;
	struct mbox_chan *ch;
	struct completion tx_done;
    /*for log proc*/
	phys_addr_t log_phy;
	size_t log_size;
	void __iomem *log_mem;
	void *log_ctrl;
    struct proc_dir_entry *proc_dir;
};

struct light_aon_ipc {
	struct light_aon_chan chans;
	struct device *dev;
	struct mutex lock;
	struct completion done;
	u32 *msg;
};

/*
 * This type is used to indicate error response for most functions.
 */
enum light_aon_error_codes {
	LIGHT_AON_ERR_NONE = 0,	/* Success */
	LIGHT_AON_ERR_VERSION = 1,	/* Incompatible API version */
	LIGHT_AON_ERR_CONFIG = 2,	/* Configuration error */
	LIGHT_AON_ERR_PARM = 3,	/* Bad parameter */
	LIGHT_AON_ERR_NOACCESS = 4,	/* Permission error (no access) */
	LIGHT_AON_ERR_LOCKED = 5,	/* Permission error (locked) */
	LIGHT_AON_ERR_UNAVAILABLE = 6,	/* Unavailable (out of resources) */
	LIGHT_AON_ERR_NOTFOUND = 7,	/* Not found */
	LIGHT_AON_ERR_NOPOWER = 8,	/* No power */
	LIGHT_AON_ERR_IPC = 9,		/* Generic IPC error */
	LIGHT_AON_ERR_BUSY = 10,	/* Resource is currently busy/active */
	LIGHT_AON_ERR_FAIL = 11,	/* General I/O failure */
	LIGHT_AON_ERR_LAST
};

static int light_aon_linux_errmap[LIGHT_AON_ERR_LAST] = {
	0,	 /* LIGHT_AON_ERR_NONE */
	-EINVAL, /* LIGHT_AON_ERR_VERSION */
	-EINVAL, /* LIGHT_AON_ERR_CONFIG */
	-EINVAL, /* LIGHT_AON_ERR_PARM */
	-EACCES, /* LIGHT_AON_ERR_NOACCESS */
	-EACCES, /* LIGHT_AON_ERR_LOCKED */
	-ERANGE, /* LIGHT_AON_ERR_UNAVAILABLE */
	-EEXIST, /* LIGHT_AON_ERR_NOTFOUND */
	-EPERM,	 /* LIGHT_AON_ERR_NOPOWER */
	-EPIPE,	 /* LIGHT_AON_ERR_IPC */
	-EBUSY,	 /* LIGHT_AON_ERR_BUSY */
	-EIO,	 /* LIGHT_AON_ERR_FAIL */
};

static struct light_aon_ipc *light_aon_ipc_handle;

static inline int light_aon_to_linux_errno(int errno)
{
	if (errno >= LIGHT_AON_ERR_NONE && errno < LIGHT_AON_ERR_LAST)
		return light_aon_linux_errmap[errno];
	return -EIO;
}

/*
 * Get the default handle used by SCU
 */
int light_aon_get_handle(struct light_aon_ipc **ipc)
{
	if (!light_aon_ipc_handle)
		return -EPROBE_DEFER;

	*ipc = light_aon_ipc_handle;
	return 0;
}
EXPORT_SYMBOL(light_aon_get_handle);

static void light_aon_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	struct light_aon_chan *aon_chan = container_of(cl, struct light_aon_chan, cl);

	complete(&aon_chan->tx_done);
}

static void light_aon_rx_callback(struct mbox_client *c, void *msg)
{
	struct light_aon_chan *aon_chan = container_of(c, struct light_aon_chan, cl);
	struct light_aon_ipc *aon_ipc = aon_chan->aon_ipc;
	struct light_aon_rpc_msg_hdr* hdr = (struct light_aon_rpc_msg_hdr*)msg;
	uint8_t recv_size  = sizeof(struct light_aon_rpc_msg_hdr) + hdr->size;

	memcpy(aon_ipc->msg, msg, recv_size);
	dev_dbg(aon_ipc->dev, "msg head: 0x%x, size:%d\n", *((u32 *)msg), recv_size);
	complete(&aon_ipc->done);
}

static int light_aon_ipc_write(struct light_aon_ipc *aon_ipc, void *msg)
{
	struct light_aon_rpc_msg_hdr *hdr = msg;
	struct light_aon_chan *aon_chan;
	u32 *data = msg;
	int ret;

	/* check size, currently it requires 7 MSG in one transfer */
	if (hdr->size != LIGHT_AON_RPC_MSG_NUM)
		return -EINVAL;

	dev_dbg(aon_ipc->dev, "RPC SVC %u FUNC %u SIZE %u\n", hdr->svc,
		hdr->func, hdr->size);

	aon_chan = &aon_ipc->chans;

	if (!wait_for_completion_timeout(&aon_chan->tx_done,
					 MAX_TX_TIMEOUT)) {
		dev_err(aon_ipc->dev, "tx_done timeout\n");
		return -ETIMEDOUT;
	}
	reinit_completion(&aon_chan->tx_done);

	ret = mbox_send_message(aon_chan->ch, data);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * RPC command/response
 */
int light_aon_call_rpc(struct light_aon_ipc *aon_ipc, void *msg, void *ack_msg, bool have_resp)
{
	struct light_aon_rpc_msg_hdr *hdr = msg;
	int ret = 0;

	if (WARN_ON(!aon_ipc || !msg))
		return -EINVAL;
	if(have_resp && WARN_ON(!ack_msg))
	    return -EINVAL;
	mutex_lock(&aon_ipc->lock);
	reinit_completion(&aon_ipc->done);

	RPC_SET_VER(hdr, LIGHT_AON_RPC_VERSION);
	/*svc id use 6bit for version 2*/
    RPC_SET_SVC_ID(hdr, hdr->svc);
	RPC_SET_SVC_FLAG_MSG_TYPE(hdr, RPC_SVC_MSG_TYPE_DATA);

	if (have_resp){
        aon_ipc->msg = ack_msg;
		RPC_SET_SVC_FLAG_ACK_TYPE(hdr, RPC_SVC_MSG_NEED_ACK);
	} else {
		RPC_SET_SVC_FLAG_ACK_TYPE(hdr, RPC_SVC_MSG_NO_NEED_ACK);
	}

	ret = light_aon_ipc_write(aon_ipc, msg);
	if (ret < 0) {
		dev_err(aon_ipc->dev, "RPC send msg failed: %d\n", ret);
		goto out;
	}

	if (have_resp) {
		if (!wait_for_completion_timeout(&aon_ipc->done,
						 MAX_RX_TIMEOUT)) {
			dev_err(aon_ipc->dev, "RPC send msg timeout\n");
			mutex_unlock(&aon_ipc->lock);
			return -ETIMEDOUT;
		}

		/* response status is stored in msg data[0] field */
		struct light_aon_rpc_ack_common* ack = ack_msg;
		ret = ack->err_code;
	}

out:
	mutex_unlock(&aon_ipc->lock);

	dev_dbg(aon_ipc->dev, "RPC SVC done\n");

	return light_aon_to_linux_errno(ret);
}
EXPORT_SYMBOL(light_aon_call_rpc);

int  get_aon_log_mem(struct device *dev, phys_addr_t* mem, size_t* mem_size)
{
    struct resource r;
	ssize_t fw_size;
	void *mem_va;
	struct device_node *node;
	int ret;

	*mem = 0;
	*mem_size = 0;

	node = of_parse_phandle(dev->of_node, "log-memory-region", 0);
	if (!node) {
		dev_err(dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret) {
	    dev_err(dev, "memory-region get resource faild\n");
		return -EINVAL;
	}

	*mem = r.start;
	*mem_size = resource_size(&r);
    return 0;
}

static int light_aon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct light_aon_ipc *aon_ipc;
	struct light_aon_chan *aon_chan;
	struct mbox_client *cl;
	char dir_name[32] = {0x0};
	int ret;

	aon_ipc = devm_kzalloc(dev, sizeof(*aon_ipc), GFP_KERNEL);
	if (!aon_ipc)
		return -ENOMEM;

	aon_chan = &aon_ipc->chans;
	cl = &aon_chan->cl;
	cl->dev = dev;
	cl->tx_block = false;
	cl->knows_txdone = true;
	cl->rx_callback = light_aon_rx_callback;

	/* Initial tx_done completion as "done" */
	cl->tx_done = light_aon_tx_done;
	init_completion(&aon_chan->tx_done);
	complete(&aon_chan->tx_done);

	aon_chan->aon_ipc = aon_ipc;
	aon_chan->ch = mbox_request_channel_byname(cl, "aon");
	if (IS_ERR(aon_chan->ch)) {
		ret = PTR_ERR(aon_chan->ch);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request aon mbox chan ret %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "request thead mbox chan: aon\n");

	aon_ipc->dev = dev;
	mutex_init(&aon_ipc->lock);
	init_completion(&aon_ipc->done);
    aon_chan->log_ctrl = NULL;

    ret = get_aon_log_mem(dev, &aon_chan->log_phy, &aon_chan->log_size);
	if(ret) {
      return ret;
	}
    aon_chan->log_mem = ioremap(aon_chan->log_phy, aon_chan->log_size);
	if (!IS_ERR(aon_chan->log_mem)) {
		printk("%s:virtual_log_mem=0x%p, phy base=0x%llx,size:%d\n",
			__func__, aon_chan->log_mem, aon_chan->log_phy,
			aon_chan->log_size);
	} else {
		aon_chan->log_mem = NULL;
		dev_err(dev, "%s:get aon log region fail\n",
				__func__);
		return -1;
	}

	sprintf(dir_name, "aon_proc");
    aon_chan->proc_dir = proc_mkdir(dir_name, NULL);
    if (NULL != aon_chan->proc_dir) {
		aon_chan->log_ctrl = light_create_panic_log_proc(aon_chan->log_phy,
			aon_chan->proc_dir, aon_chan->log_mem, aon_chan->log_size);
	} else {
		dev_err(dev, "create %s fail\n", dir_name);
		return ret;
	}
	light_aon_ipc_handle = aon_ipc;

	return devm_of_platform_populate(dev);
}

static const struct of_device_id light_aon_match[] = {
	{ .compatible = "thead,light-aon", },
	{ /* Sentinel */ }
};

static int __maybe_unused light_aon_resume_noirq(struct device *dev)
{
	struct light_aon_chan *aon_chan;
	int ret;

	aon_chan = &light_aon_ipc_handle->chans;

	complete(&aon_chan->tx_done);
	return 0;
}

static const struct dev_pm_ops light_aon_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(NULL,
				      light_aon_resume_noirq)
};
static struct platform_driver light_aon_driver = {
	.driver = {
		.name = "light-aon",
		.of_match_table = light_aon_match,
		.pm = &light_aon_pm_ops,
	},
	.probe = light_aon_probe,
};
builtin_platform_driver(light_aon_driver);

MODULE_AUTHOR("fugang.duan <duanfugang.dfg@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead Light firmware protocol driver");
MODULE_LICENSE("GPL v2");
