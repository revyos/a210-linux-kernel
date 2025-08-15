// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 ZHIHE Group Holding Limited.
 */

#include <linux/err.h>
#include <linux/firmware/zhihe/ipc.h>
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
//#include <linux/p100_proc_debug.h>




/* wait for response for 3000ms instead of 300ms (fix me pls)*/
#define MAX_RX_TIMEOUT (msecs_to_jiffies(3000))
#define MAX_TX_TIMEOUT (msecs_to_jiffies(500))

struct p100_aon_chan {
	struct p100_aon_ipc *aon_ipc;
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

struct p100_aon_ipc {
	struct p100_aon_chan chans;
	struct device *dev;
	struct mutex lock;
	struct completion done;
	const char *mbox_name;
	u32 *msg;
};

/*
 * This type is used to indicate error response for most functions.
 */
enum p100_aon_error_codes {
	P100_AON_ERR_NONE = 0, /* Success */
	P100_AON_ERR_VERSION = 1, /* Incompatible API version */
	P100_AON_ERR_CONFIG = 2, /* Configuration error */
	P100_AON_ERR_PARM = 3, /* Bad parameter */
	P100_AON_ERR_NOACCESS = 4, /* Permission error (no access) */
	P100_AON_ERR_LOCKED = 5, /* Permission error (locked) */
	P100_AON_ERR_UNAVAILABLE = 6, /* Unavailable (out of resources) */
	P100_AON_ERR_NOTFOUND = 7, /* Not found */
	P100_AON_ERR_NOPOWER = 8, /* No power */
	P100_AON_ERR_IPC = 9, /* Generic IPC error */
	P100_AON_ERR_BUSY = 10, /* Resource is currently busy/active */
	P100_AON_ERR_FAIL = 11, /* General I/O failure */
	P100_AON_ERR_LAST
};

static int p100_aon_linux_errmap[P100_AON_ERR_LAST] = {
	0, /* P100_AON_ERR_NONE */
	-EINVAL, /* P100_AON_ERR_VERSION */
	-EINVAL, /* P100_AON_ERR_CONFIG */
	-EINVAL, /* P100_AON_ERR_PARM */
	-EACCES, /* P100_AON_ERR_NOACCESS */
	-EACCES, /* P100_AON_ERR_LOCKED */
	-ERANGE, /* P100_AON_ERR_UNAVAILABLE */
	-EEXIST, /* P100_AON_ERR_NOTFOUND */
	-EPERM, /* P100_AON_ERR_NOPOWER */
	-EPIPE, /* P100_AON_ERR_IPC */
	-EBUSY, /* P100_AON_ERR_BUSY */
	-EIO, /* P100_AON_ERR_FAIL */
};
#define P100_AON_CHN_MAX     (4)
static struct p100_aon_ipc *p100_aon_ipc_handle[P100_AON_CHN_MAX];
static uint32_t g_aon_ipc_handle_num;

static inline int p100_aon_to_linux_errno(int errno)
{
	if (errno >= P100_AON_ERR_NONE && errno < P100_AON_ERR_LAST)
		return p100_aon_linux_errmap[errno];
	return -EIO;
}

/*
 * Get the default handle used by SCU
 */
int p100_aon_get_handle(struct p100_aon_ipc **ipc, char* name)
{
	uint32_t i = 0;
	if(!name)
		return -1;
	for(i = 0;i<g_aon_ipc_handle_num;i++) {
		if(!p100_aon_ipc_handle[i])
			return -1;
		if(!strcmp(name, p100_aon_ipc_handle[i]->mbox_name))) {
			*ipc = p100_aon_ipc_handle[i];
			return 0;
		}
	}
	return -1;
}
EXPORT_SYMBOL(p100_aon_get_handle);

static void p100_aon_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	struct p100_aon_chan *aon_chan =
		container_of(cl, struct p100_aon_chan, cl);

	complete(&aon_chan->tx_done);
}

static void p100_aon_rx_callback(struct mbox_client *c, void *msg)
{
	struct p100_aon_chan *aon_chan =
		container_of(c, struct p100_aon_chan, cl);
	struct p100_aon_ipc *aon_ipc = aon_chan->aon_ipc;
	struct p100_aon_rpc_msg_hdr *hdr =
		(struct p100_aon_rpc_msg_hdr *)msg;
	uint8_t recv_size = sizeof(struct p100_aon_rpc_msg_hdr) + hdr->size;

	memcpy(aon_ipc->msg, msg, recv_size);
	dev_dbg(aon_ipc->dev, "msg head: 0x%x, size:%d\n", *((u32 *)msg),
		recv_size);
	complete(&aon_ipc->done);
}

static int p100_aon_ipc_write(struct p100_aon_ipc *aon_ipc, void *msg)
{
	struct p100_aon_rpc_msg_hdr *hdr = msg;
	struct p100_aon_chan *aon_chan;
	u32 *data = msg;
	int ret;
	/* check size, currently it requires 7 MSG in one transfer */
	if (hdr->size != P100_AON_RPC_MSG_NUM)
		return -EINVAL;

	dev_dbg(aon_ipc->dev, "RPC SVC %u FUNC %u SIZE %u\n", hdr->svc,
		hdr->func, hdr->size);

	aon_chan = &aon_ipc->chans;

	if (!wait_for_completion_timeout(&aon_chan->tx_done, MAX_TX_TIMEOUT)) {
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
int p100_aon_call_rpc(struct p100_aon_ipc *aon_ipc, void *msg,
			void *ack_msg, bool have_resp)
{
	struct p100_aon_rpc_msg_hdr *hdr = msg;
	int ret = 0;
	if (WARN_ON(!aon_ipc || !msg))
		return -EINVAL;
	if (have_resp && WARN_ON(!ack_msg))
		return -EINVAL;
	mutex_lock(&aon_ipc->lock);
	reinit_completion(&aon_ipc->done);
	RPC_SET_VER(hdr, P100_AON_RPC_VERSION);
	/*svc id use 6bit for version 2*/
	RPC_SET_SVC_ID(hdr, hdr->svc);
	RPC_SET_SVC_FLAG_MSG_TYPE(hdr, RPC_SVC_MSG_TYPE_DATA);
	if (have_resp) {
		aon_ipc->msg = ack_msg;
		RPC_SET_SVC_FLAG_ACK_TYPE(hdr, RPC_SVC_MSG_NEED_ACK);
	} else {
		RPC_SET_SVC_FLAG_ACK_TYPE(hdr, RPC_SVC_MSG_NO_NEED_ACK);
	}
	ret = p100_aon_ipc_write(aon_ipc, msg);
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
		struct p100_aon_rpc_ack_common *ack = ack_msg;
		ret = ack->err_code;
	}
out:
	mutex_unlock(&aon_ipc->lock);

	dev_dbg(aon_ipc->dev, "RPC SVC done\n");

	return p100_aon_to_linux_errno(ret);
}
EXPORT_SYMBOL(p100_aon_call_rpc);

static int p100_aon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p100_aon_ipc *aon_ipc;
	struct p100_aon_chan *aon_chan;
	struct mbox_client *cl;
	struct device_node *np;
	int ret;
	aon_ipc = devm_kzalloc(dev, sizeof(*aon_ipc), GFP_KERNEL);
	if (!aon_ipc)
		return -ENOMEM;
	aon_chan = &aon_ipc->chans;
	cl = &aon_chan->cl;
	cl->dev = dev;
	cl->tx_block = false;
	cl->knows_txdone = true;
	cl->rx_callback = p100_aon_rx_callback;

	/* Initial tx_done completion as "done" */
	cl->tx_done = p100_aon_tx_done;
	init_completion(&aon_chan->tx_done);
	complete(&aon_chan->tx_done);

	aon_chan->aon_ipc = aon_ipc;
	np = cl->dev->of_node;
	aon_ipc->mbox_name = of_get_property(np, "mbox-names", NULL);
	aon_chan->ch = mbox_request_channel_byname(cl, aon_ipc->mbox_name);
	if (IS_ERR(aon_chan->ch)) {
		ret = PTR_ERR(aon_chan->ch);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request aon mbox chan ret %d\n",
				ret);
		return ret;
	}
	dev_dbg(dev, "request p100 mbox chan: %s\n",aon_ipc->mbox_name);

	aon_ipc->dev = dev;
	mutex_init(&aon_ipc->lock);
	init_completion(&aon_ipc->done);
	aon_chan->log_ctrl = NULL;
	if(g_aon_ipc_handle_num>=P100_AON_CHN_MAX) {
		dev_err(dev, "aon_ipc:%s handle num overflow\n",aon_ipc->mbox_name);
		return -1;
	}
	p100_aon_ipc_handle[g_aon_ipc_handle_num] = aon_ipc;
	g_aon_ipc_handle_num++;
	return devm_of_platform_populate(dev);
}

static const struct of_device_id p100_aon_match[] = {
	{
		.compatible = "zhihe,p100-aon",
	},
	{ /* Sentinel */ }
};

static int __maybe_unused p100_aon_resume_noirq(struct device *dev)
{
#if 0
	struct p100_aon_chan *aon_chan;
	aon_chan = &p100_aon_ipc_handle->chans;

	complete(&aon_chan->tx_done);
#endif	
	return 0;
}

static const struct dev_pm_ops p100_aon_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(NULL, p100_aon_resume_noirq)
};
static struct platform_driver p100_aon_driver = {
	.driver = {
		.name = "p100-aon",
		.of_match_table = p100_aon_match,
		.pm = &p100_aon_pm_ops,
	},
	.probe = p100_aon_probe,
};
builtin_platform_driver(p100_aon_driver);

MODULE_AUTHOR("xionglue.huang <huangxionglue@zhcomputing.com>");
MODULE_DESCRIPTION("ZHIHE firmware protocol driver");
MODULE_LICENSE("GPL v2");
