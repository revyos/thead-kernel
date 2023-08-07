// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for ASoC dummy pcm dai
 * Copyright 2023 Yan Dong <nanli.yd@alibaba-inc.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static const struct snd_soc_dapm_widget light_dummy_pcm_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route light_dummy_pcm_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

static struct snd_soc_dai_driver light_dummy_pcm_dai[] = {
	{
		.name = "dummy-pcm",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S20_LE | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
		},
		.capture = {
			 .stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S20_LE | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
		},
	},
};

static const struct snd_soc_component_driver soc_component_dev_light_dummy_pcm = {
	.dapm_widgets		= light_dummy_pcm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(light_dummy_pcm_widgets),
	.dapm_routes		= light_dummy_pcm_routes,
	.num_dapm_routes	= ARRAY_SIZE(light_dummy_pcm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int light_dummy_pcm_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
				      &soc_component_dev_light_dummy_pcm,
				      light_dummy_pcm_dai, ARRAY_SIZE(light_dummy_pcm_dai));
}

static int light_dummy_pcm_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct platform_device_id light_dummy_pcm_driver_ids[] = {
	{
		.name		= "light-dummy-pcm",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, light_dummy_pcm_driver_ids);

#if defined(CONFIG_OF)
static const struct of_device_id light_dummy_pcm_codec_of_match[] = {
	{ .compatible = "thead,light-dummy-pcm", },
	{},
};
MODULE_DEVICE_TABLE(of, light_dummy_pcm_codec_of_match);
#endif

static struct platform_driver light_dummy_pcm_driver = {
	.driver = {
		.name = "light-dummy-pcm",
		.of_match_table = of_match_ptr(light_dummy_pcm_codec_of_match),
	},
	.probe = light_dummy_pcm_probe,
	.remove = light_dummy_pcm_remove,
	.id_table = light_dummy_pcm_driver_ids,
};

module_platform_driver(light_dummy_pcm_driver);

MODULE_AUTHOR("Yan Dong <nanli.yd@alibaba-inc.com>");
MODULE_DESCRIPTION("ASoC dummy pcm dai driver");
MODULE_LICENSE("GPL");
