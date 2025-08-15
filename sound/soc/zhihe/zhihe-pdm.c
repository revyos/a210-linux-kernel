/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */
/*
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/scatterlist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/sh_fsi.h>
#include <linux/dmaengine.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>
#include <linux/mfd/syscon.h>
*/

#include <sound/sh_fsi.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>

#include "zhihe-pcm.h"
#include "zhihe-pdm.h"

#define AUDIO_PDM0		"audio_pdm0"
#define AUDIO_PDM1		"audio_pdm1"
#define AUDIO_PDM2		"audio_pdm2"
#define AUDIO_PDM3		"audio_pdm3"
#define AUDIO_PDM4		"audio_pdm4"
#define AUDIO_PDM5		"audio_pdm5"
#define AUDIO_PDM6		"audio_pdm6"
#define AUDIO_PDM7		"audio_pdm7"

#define ZHIHE_PDM_DMABUF_SIZE	(64 * 1024)

#define ZHIHE_PDM_RATES 	SNDRV_PCM_RATE_16000
#define ZHIHE_PDM_FMTS 		SNDRV_PCM_FMTBIT_S16_LE

static int pdm_probe_flag = 0;
struct zhihe_pdm_priv *host_priv;

static inline void zhihe_pdm_snd_rxctrl(struct zhihe_pdm_priv *chip, bool on)
{
	if (on) {
		regmap_update_bits(chip->regmap,
				   CP_VAD_CTRL,
				   VADCTRL_PDM_EN_MSK, 
				   VADCTRL_VAD_EN_SEL | 
				   VADCTRL_DATA_TRANS_EN_SEL);
		regmap_update_bits(chip->regmap, 
				   CP_DAI_EN, 
				   DAI_EN_MSK, 
				   DAI_EN_SEL);
		chip->state = PDM_STATE_RUNNING;
	}
	else {
		regmap_update_bits(chip->regmap, 
				   CP_VAD_CTRL, 
				   VADCTRL_PDM_EN_MSK, 
				   ~VADCTRL_VAD_EN_SEL & 
				   ~VADCTRL_DATA_TRANS_EN_SEL);
		regmap_update_bits(chip->regmap, 
				   CP_DAI_EN, 
				   DAI_EN_MSK, 
				   ~DAI_EN_SEL);
		chip->state = PDM_STATE_IDLE;
	}
}

static void zhihe_pdm_dai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct zhihe_pdm_priv *pdm_private = snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		zhihe_pdm_snd_rxctrl(pdm_private, 0);
}

/**
 * zhihe_pdm_dai_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int zhihe_pdm_dai_trigger(struct snd_pcm_substream *substream,
				 int cmd,
			         struct snd_soc_dai *dai)
{
	int ret = 0;
	struct zhihe_pdm_priv *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		zhihe_pdm_snd_rxctrl(priv, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dmaengine_terminate_async(snd_dmaengine_pcm_get_chan(substream));
		zhihe_pdm_snd_rxctrl(priv, 0);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_pause(snd_dmaengine_pcm_get_chan(substream));
		zhihe_pdm_snd_rxctrl(priv, 0);
		break;
	default:
        	return -EINVAL;
	}

	return ret;
}

static int zhihe_pdm_dai_hw_params(struct snd_pcm_substream *substream, 
				   struct snd_pcm_hw_params *params, 
				   struct snd_soc_dai *dai)
{
	struct zhihe_pdm_priv *pdm_private = snd_soc_dai_get_drvdata(dai);

	/* vad enable */
	regmap_update_bits(pdm_private->regmap, 
			   CP_VAD_CTRL, 
			   VADCTRL_PDM_EN_MSK, 
			   VADCTRL_VAD_EN_SEL | VADCTRL_DATA_TRANS_EN_SEL);
	regmap_update_bits(pdm_private->regmap, 
			   CP_DAI_EN, 
			   DAI_EN_MSK, 
			   DAI_EN_SEL);

	return 0;
}

static int zhihe_pdm_dai_probe(struct snd_soc_dai *dai)
{
	struct zhihe_pdm_priv *pdm = snd_soc_dai_get_drvdata(dai);

	if(pdm)
		snd_soc_dai_init_dma_data(dai, NULL, &pdm->dma_params_rx);

	return 0;
}

static const struct snd_soc_dai_ops zhihe_pdm_dai_ops = {
	.probe		= zhihe_pdm_dai_probe,
	.shutdown	= zhihe_pdm_dai_shutdown,
	.trigger	= zhihe_pdm_dai_trigger,
	.hw_params	= zhihe_pdm_dai_hw_params,
};

static struct snd_soc_dai_driver zhihe_pdm_soc_dai[] = {
	{
		.name = "zhihe-pdm-dai-0",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-1",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-2",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-3",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-4",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-5",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-6",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "zhihe-pdm-dai-7",
		.capture = {
			.rates		= ZHIHE_PDM_RATES,
			.formats	= ZHIHE_PDM_FMTS,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops = &zhihe_pdm_dai_ops,
		.symmetric_rate = 1,
	},	
};


static const struct snd_soc_component_driver zhihe_pdm_soc_component = {
	.name	= "zhihe_pdm",
};

static const struct regmap_config zhihe_pdm_regmap_config = {
        .reg_bits 	= 32,
        .reg_stride 	= 4,
        .val_bits 	= 32,
        .max_register 	= CP_VAD_IP_ID,
        .cache_type 	= REGCACHE_NONE,
};

static int zhihe_pdm_runtime_suspend(struct device *dev)
{
	struct zhihe_pdm_priv *pdm_priv = dev_get_drvdata(dev);

	if (!pdm_priv->mclk_keepon) {
		regcache_cache_only(pdm_priv->regmap, true);
		clk_disable_unprepare(pdm_priv->clk);
	}

	return 0;
}

static int zhihe_pdm_runtime_resume(struct device *dev)
{
	struct zhihe_pdm_priv *pdm_priv = dev_get_drvdata(dev);
	int ret;

	if (!pdm_priv->mclk_keepon) {
		ret = clk_prepare_enable(pdm_priv->clk);
		if (ret) {
			dev_err(pdm_priv->dev, 
				"clock enable failed %d\n", 
				ret);
			return ret;
		}

		regcache_cache_only(pdm_priv->regmap, false);
	}

	return ret;
}

static const struct of_device_id zhihe_pdm_of_match[] = {
	{ .compatible = "zhihe,pdm"},
	{},
};

MODULE_DEVICE_TABLE(of, zhihe_pdm_of_match);

static int zhihe_pdm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	const char *sprop;
	const uint32_t *iprop;
	struct zhihe_pdm_priv *pdm_priv;
	struct resource *res;
	struct device *dev = &pdev->dev;
	unsigned int irq;
	int ret;

	pdm_priv = devm_kzalloc(&pdev->dev, sizeof(*pdm_priv), GFP_KERNEL);
	if (!pdm_priv)
		return -ENOMEM;

	pdm_priv->dev = dev;

	sprop = of_get_property(np, "zhihe,mode", NULL);
	if (sprop && (!strcmp(sprop, "pdm-master")))
		pdm_priv->dai_fmt = SND_SOC_DAIFMT_PDM;

	sprop = of_get_property(np, "zhihe,sel", NULL);
	if (sprop) 
		strcpy(pdm_priv->name, sprop);

	iprop = of_get_property(np, "zhihe,dma_maxburst", NULL);
	if (iprop)
	 	pdm_priv->dma_maxburst = be32_to_cpup(iprop);
	else
	 	pdm_priv->dma_maxburst = PDM_DMA_MAXBURST;

	iprop = of_get_property(np, "zhihe,mclk_keepon", NULL);
	if (iprop)
	 	pdm_priv->mclk_keepon = be32_to_cpup(iprop);
	else
	 	pdm_priv->mclk_keepon = false;

	dev_set_drvdata(&pdev->dev, pdm_priv);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (pdm_probe_flag) {
		pdm_priv->regs = host_priv->regs;
		pdm_priv->regmap = host_priv->regmap;
	} else {
		pdm_probe_flag = true;
		host_priv = pdm_priv;

		pdm_priv->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(pdm_priv->regs))
			return PTR_ERR(pdm_priv->regs);

		pdm_priv->regmap = devm_regmap_init_mmio(&pdev->dev, 
							 pdm_priv->regs,
							 &zhihe_pdm_regmap_config);

		if (IS_ERR(pdm_priv->regmap)) {
				dev_err(&pdev->dev,
					"Failed to initialise managed register map\n");
				return PTR_ERR(pdm_priv->regmap);
		}

		irq = platform_get_irq(pdev, 0);
		if (!res || (int)irq <= 0) {
			dev_err(&pdev->dev, 
				"Not enough p100-fpga platform resources.\n");
			return -ENODEV;
		}

		pm_runtime_enable(&pdev->dev);
	}

	pdm_priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	pdm_priv->dma_params_rx.maxburst = pdm_priv->dma_maxburst;
	pdm_priv->chan_num = 1;
	
	if (!strcmp(pdm_priv->name, AUDIO_PDM0))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO0_L;	
	else if (!strcmp(pdm_priv->name, AUDIO_PDM1))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO0_R;
	else if (!strcmp(pdm_priv->name, AUDIO_PDM2))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO1_L;
	else if (!strcmp(pdm_priv->name, AUDIO_PDM3))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO1_R;
	else if (!strcmp(pdm_priv->name, AUDIO_PDM4))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO2_L;
	else if (!strcmp(pdm_priv->name, AUDIO_PDM5))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO2_R;
	else if (!strcmp(pdm_priv->name, AUDIO_PDM6))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO3_L;
	else if (!strcmp(pdm_priv->name, AUDIO_PDM7))
		pdm_priv->dma_params_rx.addr = res->start + CP_FIFO3_R;
	else {
		dev_err(&pdev->dev, 
			"False audio dai link name %s !\n", 
			pdm_priv->name);
		return -ENODEV;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(dev, "zhihe_pcm_dma_init error\n");
		goto err_pm_disable;
	}

	if (pdm_priv->mclk_keepon) {
		clk_prepare_enable(pdm_priv->clk);
	}

	ret = devm_snd_soc_register_component(&pdev->dev, 
					      &zhihe_pdm_soc_component,
					      zhihe_pdm_soc_dai, 
					      ARRAY_SIZE(zhihe_pdm_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_pm_disable;
	}

	return ret;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int zhihe_pdm_remove(struct platform_device *pdev)
{
	struct zhihe_pdm_priv *pdm_priv = dev_get_drvdata(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		zhihe_pdm_runtime_suspend(&pdev->dev);
	clk_disable_unprepare(pdm_priv->clk);

	return 0;
}

static const struct dev_pm_ops zhihe_pdm_pm_ops = {
	SET_RUNTIME_PM_OPS(zhihe_pdm_runtime_suspend, 
			   zhihe_pdm_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(zhihe_pdm_suspend, zhihe_pdm_resume)
};

static struct platform_driver zhihe_pdm_driver = {
	.driver 	= {
		.name	= "zhihe-pdm-audio",
		.pm	= &zhihe_pdm_pm_ops,
		.of_match_table = of_match_ptr(zhihe_pdm_of_match),
	},
	.probe	= zhihe_pdm_probe,
	.remove	= zhihe_pdm_remove,
};

module_platform_driver(zhihe_pdm_driver);

MODULE_AUTHOR("Anonymous <anonymous@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe P100 audio driver");
MODULE_LICENSE("GPL v2");
