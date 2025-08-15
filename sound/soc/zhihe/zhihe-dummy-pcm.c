/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Zhihe Driver for ASoC dummy pcm dai
 *
 * Copyright (C) 2024 Zhihe Computing Technology (Shenzhen) Co., Ltd..
 * 
 * Author: Anonymous <anonymous@zhcomputing.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

#define ZHIHE_DUMMY_FMTS (SNDRV_PCM_FMTBIT_FLOAT_LE | \
			  SNDRV_PCM_FMTBIT_S32_LE | \
			  SNDRV_PCM_FMTBIT_S24_LE | \
			  SNDRV_PCM_FMTBIT_S20_LE | \
			  SNDRV_PCM_FMTBIT_S16_LE | \
			  SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dapm_widget zhihe_dummy_pcm_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route zhihe_dummy_pcm_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

static u64 zhihe_dummy_pcm_dai_formats =
	SND_SOC_POSSIBLE_DAIFMT_I2S	|
	SND_SOC_POSSIBLE_DAIFMT_RIGHT_J	|
	SND_SOC_POSSIBLE_DAIFMT_LEFT_J	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_A	|
	SND_SOC_POSSIBLE_DAIFMT_DSP_B	|
	SND_SOC_POSSIBLE_DAIFMT_AC97	|
	SND_SOC_POSSIBLE_DAIFMT_PDM	|
	SND_SOC_POSSIBLE_DAIFMT_GATED	|
	SND_SOC_POSSIBLE_DAIFMT_CONT	|
	SND_SOC_POSSIBLE_DAIFMT_NB_NF	|
	SND_SOC_POSSIBLE_DAIFMT_NB_IF	|
	SND_SOC_POSSIBLE_DAIFMT_IB_NF	|
	SND_SOC_POSSIBLE_DAIFMT_IB_IF	|
	SND_SOC_POSSIBLE_DAIFMT_CBP_CFP	|
	SND_SOC_POSSIBLE_DAIFMT_CBP_CFC	|
	SND_SOC_POSSIBLE_DAIFMT_CBC_CFP	|
	SND_SOC_POSSIBLE_DAIFMT_CBC_CFC;

static const struct snd_soc_dai_ops zhihe_dummy_dai_ops = {
	.auto_selectable_formats	= &zhihe_dummy_pcm_dai_formats,
	.num_auto_selectable_formats	= 1,
};

static struct snd_soc_dai_driver zhihe_dummy_pcm_dai[] = {
	{
		.name = "dummy-pcm-dai",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = ZHIHE_DUMMY_FMTS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_384000,
			.formats = ZHIHE_DUMMY_FMTS,
		},
		.ops = &zhihe_dummy_dai_ops,
	},
};

static const struct snd_soc_component_driver zhihe_dummy_pcm_component = {
	.dapm_widgets		= zhihe_dummy_pcm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(zhihe_dummy_pcm_widgets),
	.dapm_routes		= zhihe_dummy_pcm_routes,
	.num_dapm_routes	= ARRAY_SIZE(zhihe_dummy_pcm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int zhihe_dummy_pcm_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					       &zhihe_dummy_pcm_component,
					       zhihe_dummy_pcm_dai,
					       ARRAY_SIZE(zhihe_dummy_pcm_dai));
}

static int zhihe_dummy_pcm_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct platform_device_id zhihe_dummy_pcm_driver_ids[] = {
	{ .name = "dummy-pcm", },
	{},
};
MODULE_DEVICE_TABLE(platform, zhihe_dummy_pcm_driver_ids);

#if defined(CONFIG_OF)
static const struct of_device_id zhihe_dummy_pcm_codec_of_match[] = {
	{ .compatible = "zhihe,dummy-pcm-i2s", },
	{ .compatible = "zhihe,dummy-pcm-i2s-8ch", },
	{ .compatible = "zhihe,dummy-pcm-tdm", },
	{ .compatible = "zhihe,dummy-pcm-pdm", },
	{},
};
MODULE_DEVICE_TABLE(of, zhihe_dummy_pcm_codec_of_match);
#endif

static struct platform_driver zhihe_dummy_pcm_driver = {
	.driver = {
		.name = "dummy-pcm",
		.of_match_table = of_match_ptr(zhihe_dummy_pcm_codec_of_match),
	},
	.probe		= zhihe_dummy_pcm_probe,
	.remove		= zhihe_dummy_pcm_remove,
	.id_table	= zhihe_dummy_pcm_driver_ids,
};
module_platform_driver(zhihe_dummy_pcm_driver);

MODULE_AUTHOR("Anonymous <anonymous@zhcomputing.com>");
MODULE_DESCRIPTION("ASoC dummy pcm dai driver");
MODULE_LICENSE("GPL");
