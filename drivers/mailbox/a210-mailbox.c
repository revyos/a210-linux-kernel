// SPDX-License-Identifier: GPL-2.0
/*
 * a210 MailBox support
 *
 * Copyright (C) 2024 ZHIHE Group Holding Limited.
 *
 * Author: xionglue.huang <huangxionglue@zhcomputing.com>
 *
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>

/* Status Register */
#define A210_MBOX_STA			0x0
#define A210_MBOX_CLR			0x4
#define A210_MBOX_MASK		0xc

/* Transmit/receive data register:
 * INFO0 ~ INFO6
 */
#define A210_MBOX_INFO_NUM		8
#define A210_MBOX_DATA_INFO_NUM	7
#define A210_MBOX_INFO0		0x14
/* Transmit ack register: INFO7 */
#define A210_MBOX_INFO7		0x30

/* Generate remote icu IRQ Register */
#define A210_MBOX_GEN			0x10
#define A210_MBOX_GEN_RX_DATA		BIT(6)
#define A210_MBOX_GEN_TX_ACK		BIT(7)

#define A210_MBOX_CHAN_RES_SIZE	0x1000
#define A210_MBOX_CHANS		4
#define A210_MBOX_CHAN_NAME_SIZE	20

#define A210_MBOX_ACK_MAGIC		0xdeadbeaf

#ifdef CONFIG_PM_SLEEP
/* store MBOX context across system-wide suspend/resume transitions */
struct a210_mbox_context {
	u32 intr_mask[A210_MBOX_CHANS - 1];
};

#endif
enum a210_mbox_chan_type {
	A210_MBOX_TYPE_TXRX,		/* Tx & Rx chan */
	A210_MBOX_TYPE_DB,		/* Tx & Rx doorbell */
};
enum a210_mbox_icu_cpu_id {
	A210_MBOX_ICU_CPU0  = 0,		/*die0-908*/
	A210_MBOX_ICU_CPU1  = 1,		
	A210_MBOX_ICU_CPU2  = 2,		
	A210_MBOX_ICU_CPU3  = 3,		
};

enum a210_mbox_local_id {
	A210_MBOX_INTERRUPT = 0,
	A210_MBOX_DATA_CH0  = 1,		/* die0-908--die0-902*/
	A210_MBOX_DATA_CH1  = 2,		
	A210_MBOX_DATA_CH2  = 3,		
};
enum a210_mbox_remote_id {
	A210_MBOX_REMOTE_CH0  = 0,		/* die0-908--die0-902*/
	A210_MBOX_REMOTE_CH1  = 1,		
	A210_MBOX_REMOTE_CH2  = 2,			
};

struct a210_mbox_con_priv {
	enum a210_mbox_local_id	idx;
	enum a210_mbox_chan_type	type;
	void __iomem			*comm_local_base;
	void __iomem			*comm_remote_base;
	char				irq_desc[A210_MBOX_CHAN_NAME_SIZE];
	struct mbox_chan		*chan;
	struct tasklet_struct		txdb_tasklet;
};

struct a210_mbox_priv {
	struct device			*dev;
	void __iomem			*local_icu[A210_MBOX_CHANS];
	void __iomem			*remote_icu[A210_MBOX_CHANS - 1];
	void __iomem			*cur_cpu_ch_base;
	enum a210_mbox_icu_cpu_id	cur_icu_cpu_id;
	spinlock_t			mbox_lock; /* control register lock */

	struct mbox_controller		mbox;
	struct mbox_chan		mbox_chans[A210_MBOX_CHANS];

	struct a210_mbox_con_priv	con_priv[A210_MBOX_CHANS];
	struct clk			*clk;
	int				irq;
#ifdef CONFIG_PM_SLEEP
	struct a210_mbox_context	*ctx;
#endif
};

static struct a210_mbox_priv *to_a210_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct a210_mbox_priv, mbox);
}

static void a210_mbox_write(struct a210_mbox_priv *priv, u32 val, u32 offs)
{
	iowrite32(val, priv->cur_cpu_ch_base + offs);
}

static u32 a210_mbox_read(struct a210_mbox_priv *priv, u32 offs)
{
	return ioread32(priv->cur_cpu_ch_base + offs);
}

static u32 a210_mbox_rmw(struct a210_mbox_priv *priv,
			   u32 off, u32 set, u32 clr)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv->mbox_lock, flags);
	val = a210_mbox_read(priv, off);
	val &= ~clr;
	val |= set;
	a210_mbox_write(priv, val, off);
	spin_unlock_irqrestore(&priv->mbox_lock, flags);

	return val;
}

static void a210_mbox_chan_write(struct a210_mbox_con_priv *cp,
				   u32 val, u32 offs, bool is_remote)
{
	if (is_remote)
		iowrite32(val, cp->comm_remote_base + offs);
	else
		iowrite32(val, cp->comm_local_base + offs);
}

static u32 a210_mbox_chan_read(struct a210_mbox_con_priv *cp,
				 u32 offs, bool is_remote)
{
	uint32_t t = 0;
	if (is_remote)
		t = ioread32(cp->comm_remote_base + offs);
	else
		t = ioread32(cp->comm_local_base + offs);
	return t;
}

static void a210_mbox_chan_rmw(struct a210_mbox_con_priv *cp,
				 u32 off, u32 set, u32 clr, bool is_remote)
{
	u32 val;
	unsigned long flags;
	struct a210_mbox_priv *priv = to_a210_mbox_priv(cp->chan->mbox);
	spin_lock_irqsave(&priv->mbox_lock, flags);
	val = a210_mbox_chan_read(cp, off, is_remote);
	val &= ~clr;
	val |= set;
	a210_mbox_chan_write(cp, val, off, is_remote);
	spin_unlock_irqrestore(&priv->mbox_lock, flags);
}

static void a210_mbox_chan_rd_data(struct a210_mbox_con_priv *cp,
				     void *data, bool is_remote)
{
	u32 i;
	u32 *arg = data;
	u32 off = A210_MBOX_INFO0;

	/* read info0 ~ info6, totally 28 bytes
	 * requires data memory size is 28 bytes
	 */
	for (i = 0; i < A210_MBOX_DATA_INFO_NUM; i++) {
		*arg = a210_mbox_chan_read(cp, off, is_remote);
		off += 4;
		arg++;
	}
}

static void a210_mbox_chan_wr_data(struct a210_mbox_con_priv *cp,
				     void *data, bool is_remote)
{
	u32 i;
	u32 *arg = data;
	u32 off = A210_MBOX_INFO0;

	/* write info0 ~ info6, totally 28 bytes
	 * requires data memory is 28 bytes valid data
	 */
	for (i = 0; i < A210_MBOX_DATA_INFO_NUM; i++) {
		a210_mbox_chan_write(cp, *arg, off, is_remote);
		off += 4;
		arg++;
	}
}

static void a210_mbox_chan_wr_ack(struct a210_mbox_con_priv *cp,
				    void *data, bool is_remote)
{
	u32 *arg = data;
	u32 off = A210_MBOX_INFO7;

	a210_mbox_chan_write(cp, *arg, off, is_remote);
}

static int a210_mbox_chan_id_to_mapbit(struct a210_mbox_con_priv *cp)
{
	int i;
	int mapbit = 0;
	for (i = 0; i < A210_MBOX_CHANS; i++) {
		if (i == cp->idx)
			return mapbit;

		if (i != A210_MBOX_INTERRUPT)
			mapbit++;
	}

	if (i == A210_MBOX_CHANS)
		dev_err(cp->chan->mbox->dev, "convert to mapbit failed\n");

	return 0;
}

static void a210_mbox_txdb_tasklet(unsigned long data)
{
	struct a210_mbox_con_priv *cp = (struct a210_mbox_con_priv *)data;

	mbox_chan_txdone(cp->chan, 0);
}

static irqreturn_t a210_mbox_isr(int irq, void *p)
{
	u32 info0_data, info7_data;
	u32 sta, dat[A210_MBOX_DATA_INFO_NUM];
	u32 ack_magic = A210_MBOX_ACK_MAGIC;
	struct mbox_chan *chan = p;
	struct a210_mbox_con_priv *cp = chan->con_priv;
	int mapbit = a210_mbox_chan_id_to_mapbit(cp);
	struct a210_mbox_priv *priv = to_a210_mbox_priv(chan->mbox);

	sta = a210_mbox_read(priv, A210_MBOX_STA);
	if (!(sta & BIT(mapbit)))
		return IRQ_NONE;
	/* clear chan irq bit in STA register */
	a210_mbox_rmw(priv, A210_MBOX_CLR, BIT(mapbit), 0);
	/* rx doorbell */
	if (cp->type == A210_MBOX_TYPE_DB) {
		mbox_chan_received_data(cp->chan, NULL);
		return IRQ_HANDLED;
	}
	/* info0 is the protocol word, should not be zero! */
	info0_data = a210_mbox_chan_read(cp, A210_MBOX_INFO0, false);
	if (info0_data) {
		/* read info0~info6 data */
		a210_mbox_chan_rd_data(cp, dat, false);

		/* clear local info0 */
		a210_mbox_chan_write(cp, 0x0, A210_MBOX_INFO0, false);
		/* notify remote cpu */
		a210_mbox_chan_wr_ack(cp, &ack_magic, true);
		/* transfer the data to client */
		mbox_chan_received_data(chan, (void *)dat);
	}
	/* info7 magic value mean the real ack signal, not generate bit7 */
	info7_data = a210_mbox_chan_read(cp, A210_MBOX_INFO7, false);
	if (info7_data == A210_MBOX_ACK_MAGIC) {
		/* clear local info7 */
		a210_mbox_chan_write(cp, 0x0, A210_MBOX_INFO7, false);

		/* notify framework the last TX has completed */
		mbox_chan_txdone(chan, 0);
	}
	if (!info0_data && !info7_data)
		return IRQ_NONE;
	return IRQ_HANDLED;
}

static int a210_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct a210_mbox_con_priv *cp = chan->con_priv;
	if (cp->type == A210_MBOX_TYPE_DB)
		tasklet_schedule(&cp->txdb_tasklet);
	else
		a210_mbox_chan_wr_data(cp, data, true);
	a210_mbox_chan_rmw(cp, A210_MBOX_GEN, A210_MBOX_GEN_RX_DATA, 0,
			     true);
	return 0;
}

static int a210_mbox_startup(struct mbox_chan *chan)
{
	int ret;
	int mask_bit;
	u32 data[8] = {0};
	struct a210_mbox_con_priv *cp = chan->con_priv;
	struct a210_mbox_priv *priv = to_a210_mbox_priv(chan->mbox);
	/* clear local and remote generate and info0~info7 */
	a210_mbox_chan_rmw(cp, A210_MBOX_GEN, 0x0, 0xff, true);
	a210_mbox_chan_rmw(cp, A210_MBOX_GEN, 0x0, 0xff, false);
	a210_mbox_chan_wr_ack(cp, &data[7], true);
	a210_mbox_chan_wr_ack(cp, &data[7], false);
	a210_mbox_chan_wr_data(cp, &data[0], true);
	a210_mbox_chan_wr_data(cp, &data[0], false);
	/* enable the chan mask */
	mask_bit = a210_mbox_chan_id_to_mapbit(cp);
	a210_mbox_rmw(priv, A210_MBOX_MASK, BIT(mask_bit), 0);

	if (cp->type == A210_MBOX_TYPE_DB)
		/* tx doorbell doesn't have ACK, rx doorbell requires isr */
		tasklet_init(&cp->txdb_tasklet, a210_mbox_txdb_tasklet,
			     (unsigned long)cp);
	ret = request_irq(priv->irq, a210_mbox_isr, IRQF_SHARED |
			  IRQF_NO_SUSPEND, cp->irq_desc, chan);
	if (ret) {
		dev_err(priv->dev,
			"Unable to acquire IRQ %d\n", priv->irq);
		return ret;
	}
	return 0;
}

static void a210_mbox_shutdown(struct mbox_chan *chan)
{
	int mask_bit;
	struct a210_mbox_con_priv *cp = chan->con_priv;
	struct a210_mbox_priv *priv = to_a210_mbox_priv(chan->mbox);

	/* clear the chan mask */
	mask_bit = a210_mbox_chan_id_to_mapbit(cp);
	a210_mbox_rmw(priv, A210_MBOX_MASK, 0, BIT(mask_bit));

	free_irq(priv->irq, chan);
}

static const struct mbox_chan_ops a210_mbox_ops = {
	.send_data	= a210_mbox_send_data,
	.startup	= a210_mbox_startup,
	.shutdown	= a210_mbox_shutdown,
};

static int a210_mbox_init_generic(struct a210_mbox_priv *priv)
{
#ifdef CONFIG_PM_SLEEP
	priv->ctx = devm_kzalloc(priv->dev, sizeof(*priv->ctx), GFP_KERNEL);
	if (!priv->ctx)
		return -ENOMEM;
#endif
	/* Set default configuration */
	a210_mbox_write(priv, 0xff, A210_MBOX_CLR);
	a210_mbox_write(priv, 0x0, A210_MBOX_MASK);
	return 0;
}

static struct mbox_chan *a210_mbox_xlate(struct mbox_controller *mbox,
					   const struct of_phandle_args *sp)
{
	u32 chan, type;
	struct a210_mbox_con_priv *cp;

	if (sp->args_count != 2) {
		dev_err(mbox->dev,
			"Invalid argument count %d\n", sp->args_count);
		return ERR_PTR(-EINVAL);
	}

	chan = sp->args[0]; /* comm remote channel */
	type = sp->args[1]; /* comm channel type */
	if (chan >= mbox->num_chans) {
		dev_err(mbox->dev, "Not supported channel number: %d\n", chan);
		return ERR_PTR(-EINVAL);
	}

	if (type > A210_MBOX_TYPE_DB) {
		dev_err(mbox->dev,
			"Not supported the type for channel[%d]\n", chan);
		return ERR_PTR(-EINVAL);
	}

	cp = mbox->chans[chan].con_priv;
	cp->type = type;

	return &mbox->chans[chan];
}

static int a210_mbox_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int i;
	struct resource *res;
	struct a210_mbox_priv *priv;
	unsigned int remote_idx = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (of_property_read_u32(np, "icu_cpu_id", &priv->cur_icu_cpu_id)) {
		dev_err(dev, "icu_cpu_id is missing\n");
		return -EINVAL;
	}
	priv->dev = dev;
	
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "interrupt_addr"); 
	priv->local_icu[A210_MBOX_INTERRUPT] = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->local_icu[A210_MBOX_INTERRUPT]))						
		return PTR_ERR(priv->local_icu[A210_MBOX_INTERRUPT]);	
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "local_addr0"); 
	priv->local_icu[A210_MBOX_DATA_CH0] = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->local_icu[A210_MBOX_DATA_CH0]))						
		return PTR_ERR(priv->local_icu[A210_MBOX_DATA_CH0]);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "remote_icu0");
	priv->remote_icu[A210_MBOX_REMOTE_CH0] = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->remote_icu[A210_MBOX_REMOTE_CH0]))
		return PTR_ERR(priv->remote_icu[A210_MBOX_REMOTE_CH0]);
	
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "local_addr1"); 
	if(res!=NULL) {
		priv->local_icu[A210_MBOX_DATA_CH1] = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->local_icu[A210_MBOX_DATA_CH1]))						
			return PTR_ERR(priv->local_icu[A210_MBOX_DATA_CH1]);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "remote_icu1");
	if(res!=NULL) {
		priv->remote_icu[A210_MBOX_REMOTE_CH1] = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->remote_icu[A210_MBOX_REMOTE_CH1]))
			return PTR_ERR(priv->remote_icu[A210_MBOX_REMOTE_CH1]);
	}
	
	priv->cur_cpu_ch_base = priv->local_icu[A210_MBOX_INTERRUPT];
	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		if (PTR_ERR(priv->clk) != -ENOENT)
			return PTR_ERR(priv->clk);
		priv->clk = NULL;
	}
	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		return ret;
	}
	/* init the chans */
	for (i = 0; i < A210_MBOX_CHANS; i++) {
		struct a210_mbox_con_priv *cp = &priv->con_priv[i];
		cp->idx = i;
		cp->chan = &priv->mbox_chans[i];
		priv->mbox_chans[i].con_priv = cp;
		snprintf(cp->irq_desc, sizeof(cp->irq_desc),
			 "a210_mbox_chan[%i]", cp->idx);
		cp->comm_local_base = priv->local_icu[i];
		if (i != A210_MBOX_INTERRUPT) {
			cp->comm_remote_base = priv->remote_icu[remote_idx];
			remote_idx++;
		}
	}
	spin_lock_init(&priv->mbox_lock);

	priv->mbox.dev = dev;
	priv->mbox.ops = &a210_mbox_ops;
	priv->mbox.chans = priv->mbox_chans;
	priv->mbox.num_chans = A210_MBOX_CHANS;
	priv->mbox.of_xlate = a210_mbox_xlate;
	priv->mbox.txdone_irq = true;

	platform_set_drvdata(pdev, priv);

	ret = a210_mbox_init_generic(priv);
	if (ret) {
		dev_err(dev, "Failed to init mailbox context\n");
		return ret;
	}
	return devm_mbox_controller_register(dev, &priv->mbox);
}

static int a210_mbox_remove(struct platform_device *pdev)
{
	struct a210_mbox_priv *priv = platform_get_drvdata(pdev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id a210_mbox_dt_ids[] = {
	{ .compatible = "zhihe,a210-mbox" },
	{ },
};
MODULE_DEVICE_TABLE(of, a210_mbox_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused a210_mbox_suspend_noirq(struct device *dev)
{
	u32 i;
	struct a210_mbox_priv *priv = dev_get_drvdata(dev);
	struct a210_mbox_context *ctx = priv->ctx;

	/*
	 * ONLY interrupt mask bit should be stored and restores.
	 * INFO data all assumed to be lost.
	 */
	for (i = 0 ; i < A210_MBOX_CHANS; i++)
		ctx->intr_mask[i] = ioread32(priv->local_icu[i] +
					     A210_MBOX_MASK);

	return 0;
}

static int __maybe_unused a210_mbox_resume_noirq(struct device *dev)
{
	u32 i;
	struct a210_mbox_priv *priv = dev_get_drvdata(dev);
	struct a210_mbox_context *ctx = priv->ctx;

	for (i = 0 ; i < A210_MBOX_CHANS; i++)
		iowrite32(ctx->intr_mask[i],
			  priv->local_icu[i] + A210_MBOX_MASK);

	return 0;
}

#endif

static int __maybe_unused a210_mbox_runtime_suspend(struct device *dev)
{
	struct a210_mbox_priv *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused a210_mbox_runtime_resume(struct device *dev)
{
	struct a210_mbox_priv *priv = dev_get_drvdata(dev);
	int ret = clk_prepare_enable(priv->clk);

	if (ret)
		dev_err(dev, "failed to enable clock\n");

	return ret;
}

static const struct dev_pm_ops a210_mbox_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(a210_mbox_suspend_noirq,
				      a210_mbox_resume_noirq)
#endif
	SET_RUNTIME_PM_OPS(a210_mbox_runtime_suspend,
			   a210_mbox_runtime_resume, NULL)
};

static struct platform_driver a210_mbox_driver = {
	.probe	= a210_mbox_probe,
	.remove	= a210_mbox_remove,
	.driver	= {
		.name		= "a210_mbox",
		.of_match_table	= a210_mbox_dt_ids,
		.pm		= &a210_mbox_pm_ops,
	},
};
module_platform_driver(a210_mbox_driver);

MODULE_AUTHOR("xionglue.huang <huangxionglue@zhcomputing.com>");
MODULE_DESCRIPTION("a210 Mailbox IPC driver");
MODULE_LICENSE("GPL v2");
