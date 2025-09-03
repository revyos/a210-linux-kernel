// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2024 Alibaba Group Holding Limited.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/device.h>
#include <linux/watchdog.h>
#include <linux/firmware/zhihe/ipc.h>
#include <linux/delay.h>

#define DRV_NAME	"a210-wdt"

/*
 *   Watchdog selector to timeout in seconds.
 *   0: WDT disabled;
 *   others: timeout = 2048 ms * 2^(TWDSCALE-1).
 */
static const unsigned int wdt_timeout[] = {8, 16, 32,128};
#define A210_TWDSCALE_DISABLE		0
#define A210_TWDSCALE_MIN		1
#define A210_TWDSCALE_MAX		(ARRAY_SIZE(wdt_timeout) - 1)
#define A210_WDT_MIN_TIMEOUT		wdt_timeout[A210_TWDSCALE_MIN]
#define A210_WDT_MAX_TIMEOUT		wdt_timeout[A210_TWDSCALE_MAX]
#define A210_WDT_TIMEOUT		wdt_timeout[3]
#define A210_RESET_PROTECTION_MS	256

struct a210_aon_msg_wdg_ctrl {
	struct a210_aon_rpc_msg_hdr hdr;
	u32 timeout;
	u32 running_state;
	u32 reserved[1];
} __packed __aligned(1);

struct a210_aon_msg_wdg_ctrl_ack {
	struct a210_aon_rpc_ack_common ack_hdr;
	u32 timeout;
	u32 running_state;
	u32 reserved[1];
} __packed __aligned(1);

struct a210_wdt_device {
	struct device *dev;
	struct a210_aon_ipc *ipc_handle;
	struct a210_aon_msg_wdg_ctrl msg;
	unsigned int    is_aon_wdt_ena;
};

struct a210_wdt_device *a210_power_off_wdt;

static unsigned int a210_wdt_timeout_to_sel(unsigned secs)
{
	unsigned int i;

	for (i = A210_TWDSCALE_MIN; i <= A210_TWDSCALE_MAX; i++) {
		if (wdt_timeout[i] >= secs)
			return i;
	}

	return A210_TWDSCALE_MAX;
}

static void a210_wdt_msg_hdr_fill(struct a210_aon_rpc_msg_hdr *hdr, enum a210_aon_wdg_func func)
{
	hdr->svc = (uint8_t)A210_AON_RPC_SVC_WDG;
	hdr->func = (uint8_t)func;
	hdr->size = A210_AON_RPC_MSG_NUM;
}

static int a210_wdt_is_running(struct a210_wdt_device *wdt_dev)
{
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_msg_wdg_ctrl_ack ack_msg= {0};
	int ret;
	
	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_GET_STATE);
	wdt_dev->msg.running_state = -1;
	
	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	printk("a210_wdt_is_running ret %d",ret);
	if (ret)
		return ret;

	//RPC_GET_BE32(&ack_msg.timeout, 0, &wdt_dev->msg.timeout);
	RPC_GET_BE32(&ack_msg.timeout, 4, &wdt_dev->msg.running_state);

	pr_debug("ret = %d, timeout = %d, running_state = %d\n", ret, wdt_dev->msg.timeout,
			wdt_dev->msg.running_state);
	printk("ret = %d, timeout = %d, running_state = %d\n", ret, wdt_dev->msg.timeout,
			wdt_dev->msg.running_state);	
	return wdt_dev->msg.running_state;
}

static int a210_wdt_update_timeout(struct a210_wdt_device *wdt_dev, unsigned int timeout)
{
	/*
	 * The watchdog triggers a reboot if a timeout value is already
	 * programmed because the timeout value combines two functions
	 * in one: indicating the counter limit and starting the watchdog.
	 * The watchdog must be disabled to be able to change the timeout
	 * value if the watchdog is already running. Then we can set the
	 * new timeout value which enables the watchdog again.
	 */
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_msg_wdg_ctrl_ack ack_msg= {0};
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_TIMEOUTSET);

    RPC_SET_BE32(&wdt_dev->msg.timeout, 0 , timeout);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret)
		return ret;

	return 0;
}

static int a210_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct a210_wdt_device *wdt_dev = watchdog_get_drvdata(wdd);
	int ret = 0;

	/*
	 * There are two cases when a set_timeout() will be called:
	 * 1. The watchdog is off and someone wants to set the timeout for the
	 *    further use.
	 * 2. The watchdog is already running and a new timeout value should be
	 *    set.
	 *
	 * The watchdog can't store a timeout value not equal zero without
	 * enabling the watchdog, so the timeout must be buffered by the driver.
	 */
	if (watchdog_active(wdd))
		ret = a210_wdt_update_timeout(wdt_dev, timeout);
	else
		wdd->timeout = wdt_timeout[a210_wdt_timeout_to_sel(timeout)];

	return ret;
}

static int a210_wdt_start(struct watchdog_device *wdd)
{
	struct a210_wdt_device *wdt_dev = watchdog_get_drvdata(wdd);
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_rpc_ack_common ack_msg = {0};
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_START);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret)
		return ret;

	return 0;
}

static int a210_wdt_stop(struct watchdog_device *wdd)
{
	struct a210_wdt_device *wdt_dev = watchdog_get_drvdata(wdd);
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_rpc_ack_common ack_msg = {0};
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_STOP);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret)
		return ret;

	return 0;
}

static int a210_wdt_ping(struct watchdog_device *wdd)
{
	struct a210_wdt_device *wdt_dev = watchdog_get_drvdata(wdd);
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_rpc_ack_common ack_msg = {0};
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_PING);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret)
		return ret;

	return 0;
}

static int a210_wdt_restart(struct watchdog_device *wdd, unsigned long action, void *data)
{
	struct a210_wdt_device *wdt_dev = watchdog_get_drvdata(wdd);
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_RESTART);

	pr_debug("[%s,%d]: Inform aon to restart the whole system....\n", __func__, __LINE__);
	printk("a210_wdt_restart msg 0x%x\n",wdt_dev->msg.hdr);
	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, NULL, false);
	if (ret)
		return ret;
	pr_debug("[%s,%d]: Finish to inform aon to restart the whole system....\n", __func__, __LINE__);
	printk("Finish to inform aon to restart the whole system....\n");

	return 0;
}

static const struct watchdog_info a210_watchdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "p100 Watchdog",
};


static const struct watchdog_ops a210_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = a210_wdt_start,
	.stop = a210_wdt_stop,
	.ping = a210_wdt_ping,
	.set_timeout = a210_wdt_set_timeout,
	.restart = a210_wdt_restart,
};

static ssize_t aon_sys_wdt_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct a210_wdt_device *wdt_dev = platform_get_drvdata(pdev);
	return sprintf(buf,"%u\n",wdt_dev->is_aon_wdt_ena);
}

static ssize_t aon_sys_wdt_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct a210_wdt_device *wdt_dev = platform_get_drvdata(pdev);
	struct a210_aon_rpc_ack_common ack_msg = {0};
	struct a210_aon_ipc *ipc;
	int ret;
	char *start = (char *)buf;
	unsigned long val;

	ipc = wdt_dev->ipc_handle;
	val = simple_strtoul(start, &start, 0);
	wdt_dev->is_aon_wdt_ena = val;
	if (val)
		a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_AON_WDT_ON);
	else
		a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_AON_WDT_OFF);
	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret){
		pr_err("%s: err:%d \n",__func__,ret);
		return -EINVAL;
	}
	return size;
}

void a210_pm_power_off(void)
{
	struct a210_wdt_device *wdt_dev = a210_power_off_wdt;
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_rpc_ack_common ack_msg = {0};
	int ret;

	pr_info("[%s,%d]poweroff system...\n", __func__, __LINE__);

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_POWER_OFF);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);

	if (ret)
		pr_err("failed to power off the system\n");
}


static DEVICE_ATTR(aon_sys_wdt, 0644, aon_sys_wdt_show, aon_sys_wdt_store);

static struct attribute *aon_sys_wdt_sysfs_entries[] = {
	&dev_attr_aon_sys_wdt.attr,
	NULL
};
static const struct attribute_group dev_attr_aon_sys_wdt_group = {
	.attrs = aon_sys_wdt_sysfs_entries,
};

static int a210_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_wdt_device *wdt_dev;
	int ret;
	struct watchdog_device *wdd;

	printk("a210_wdt_probe\n");
	msleep(1000);
	wdt_dev = devm_kzalloc(dev, sizeof(*wdt_dev), GFP_KERNEL);
	if (!wdt_dev)
		return -ENOMEM;
	wdt_dev->is_aon_wdt_ena = 0;

	ret = a210_aon_get_handle(&(wdt_dev->ipc_handle),"aon0");
	printk("a210_wdt_probe wdt_dev->ipc_handle 0x%x",wdt_dev->ipc_handle);
	printk("a210_wdt_probe ret %d",ret);
	if (ret == -EPROBE_DEFER)
		return ret;
	printk("a210_wdt_probe get_handleOK %d",ret);
	wdd = devm_kzalloc(dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	wdd->info = &a210_watchdog_info;
	wdd->ops = &a210_watchdog_ops;
	wdd->min_timeout = A210_WDT_MIN_TIMEOUT;
	wdd->max_timeout = A210_WDT_MAX_TIMEOUT;
	wdd->min_hw_heartbeat_ms = A210_RESET_PROTECTION_MS;
	wdd->status = WATCHDOG_NOWAYOUT_INIT_STATUS;

	watchdog_set_restart_priority(wdd, 128);
	watchdog_set_drvdata(wdd, wdt_dev);

	/* Set default timeout, maybe default value if the watchdog is running */
	wdd->timeout = A210_WDT_TIMEOUT;
	watchdog_init_timeout(wdd, 0, dev);
	a210_wdt_set_timeout(wdd, wdd->timeout);

	platform_set_drvdata(pdev, wdt_dev);
	ret = a210_wdt_is_running(wdt_dev);
	if (ret < 0) {
		printk("a210_wdt_probe failed to get pmic wdt running state\n");
		pr_err("failed to get pmic wdt running state\n");
		return ret;
	}

	if (ret) {
		a210_wdt_update_timeout(wdt_dev, wdd->timeout);
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	}

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret)
		return ret;

	printk("[%s,%d] register power off callback\n", __func__, __LINE__);
	pr_info("[%s,%d] register power off callback\n", __func__, __LINE__);

	pm_power_off = a210_pm_power_off;
	a210_power_off_wdt = wdt_dev;
	ret = sysfs_create_group(&pdev->dev.kobj, &dev_attr_aon_sys_wdt_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create aon_sys_wdt sysfs.\n");
		return ret;
	}

	pr_info("succeed to register p100 pmic watchdog\n");
	printk("a210_wdt_probe succeed to register p100 pmic watchdog\n");

	return 0;
}

static int a210_wdt_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct a210_wdt_device *wdt_dev = platform_get_drvdata(pdev);
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_rpc_ack_common ack_msg = {0};
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_STOP);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret)
		dev_err(dev, "a210_wdt_suspend call aon wdt stop fail, ret:%d\n", ret);

	return 0;
}

static int a210_wdt_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct a210_wdt_device *wdt_dev = platform_get_drvdata(pdev);
	struct a210_aon_ipc *ipc = wdt_dev->ipc_handle;
	struct a210_aon_rpc_ack_common ack_msg = {0};
	int ret;

	a210_wdt_msg_hdr_fill(&wdt_dev->msg.hdr, A210_AON_WDG_FUNC_START);

	ret = a210_aon_call_rpc(ipc, &wdt_dev->msg, &ack_msg, true);
	if (ret)
		dev_err(dev, "a210_wdt_resume call aon wdt start fail, ret:%d\n", ret);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(a210_wdt_pm_ops, a210_wdt_suspend, a210_wdt_resume);

static struct platform_driver a210_wdt_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm	= pm_sleep_ptr(&a210_wdt_pm_ops),
	},
	.probe = a210_wdt_probe,
};

static int __init a210_wdt_init(void)
{
	static struct platform_device *pdev;
	int ret;

	pdev = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = platform_driver_register(&a210_wdt_driver);
	if (ret) {
		platform_device_unregister(pdev);
		return PTR_ERR(pdev);
	}

	pr_info("Watchdog module: %s loaded\n", DRV_NAME);
	printk("Watchdog module: %s loaded\n", DRV_NAME);
	return 0;
}
device_initcall(a210_wdt_init);

MODULE_AUTHOR("Wei.Liu <lw312886@linux.alibaba.com>");
MODULE_DESCRIPTION("PMIC Watchdog Driver for p100");
MODULE_LICENSE("GPL");
