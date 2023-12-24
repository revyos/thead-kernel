/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */
//#define DEBUG

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
#include "light-i2s.h"
#include "light-pcm.h"
#include "light-audio-cpr.h"
#include <linux/dmaengine.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/pinctrl/light-fm-aon-pinctrl.h>

#define IIS_SRC_CLK  294912000
#define AUDIO_IIS_SRC0_CLK  49152000
#define AUDIO_IIS_SRC1_CLK  135475200
#define IIS_MCLK_SEL 256
#define HDMI_DIV_VALUE    2
#define DIV_DEFAULT	  1
#define MONO_SOURCE	  1
#define STEREO_CHANNEL    2

#define I2S_DMA_TX_THRESHOLD    16
#define I2S_DMA_RX_THRESHOLD    16

#define AUDIO_I2S_8CH	"i2s_8ch"
#define AUDIO_I2S_8CH_SD0	"i2s_8ch_sd0"
#define AUDIO_I2S_8CH_SD1	"i2s_8ch_sd1"
#define AUDIO_I2S_8CH_SD2	"i2s_8ch_sd2"
#define AUDIO_I2S_8CH_SD3	"i2s_8ch_sd3"

#define LIGHT_I2S_DMABUF_SIZE     (64 * 1024 * 10)

#define LIGHT_RATES SNDRV_PCM_RATE_8000_192000
#define LIGHT_FMTS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8)

#define LIGHT_AUDIO_PAD_CONFIG(idx)   (priv->cfg_off + ((idx-25) >> 1) * 4)

static int i2s_8ch_probe_flag = 0;
//static u32 light_special_sample_rates[] = { 11025, 22050, 44100, 88200 };

static int light_audio_cpr_set(struct light_i2s_priv *chip, unsigned int cpr_off,
                               unsigned int mask, unsigned int val)
{
       if(!chip->audio_cpr_regmap) {
               return 0;
       }

       return regmap_update_bits(chip->audio_cpr_regmap,
                               cpr_off, mask, val);
}

static void light_i2s_8ch_set_div_sclk(struct light_i2s_priv *chip, u32 sample_rate, unsigned int div_val)
{
	u32 div;
	u32 div0;
	u32 cpr_div = (IIS_SRC_CLK/AUDIO_IIS_SRC0_CLK)-1;
	if(!chip->regs) {
		return;
	}

	div = AUDIO_IIS_SRC0_CLK / IIS_MCLK_SEL;
	div0 = (div + div % sample_rate) / sample_rate / div_val;
	writel(div0, chip->regs + I2S_DIV0_LEVEL);
	light_audio_cpr_set(chip, CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_SEL_MSK, CPR_AUDIO_DIV0_SEL(cpr_div));
}

static inline void light_snd_txctrl(struct light_i2s_priv *chip, bool on)
{
	u32 dma_en = 0;
	u32 i2s_8ch_en = 0;
	u32 i2s_8ch_imr = 0;

	if(!chip->regs) {
		return;
	}

	if (on) {
		dma_en |= DMACR_TDMAE_EN;
		i2s_8ch_en |= IISEN_I2SEN;
		writel(dma_en, chip->regs + I2S_DMACR);
		writel(i2s_8ch_en, chip->regs + I2S_IISEN);
	} else {
		dma_en &= ~DMACR_TDMAE_EN;
		i2s_8ch_en &= ~IISEN_I2SEN;
		i2s_8ch_imr  = readl(chip->regs + I2S_IMR);
		i2s_8ch_imr &= ~(IMR_TXUIRM_INTR_MSK);
		i2s_8ch_imr &= ~(IMR_TXEIM_INTR_MSK);
		writel(i2s_8ch_imr, chip->regs + I2S_IMR);
		writel(dma_en, chip->regs + I2S_DMACR);
		writel(i2s_8ch_en, chip->regs + I2S_IISEN);
	}
}

static inline void light_snd_rxctrl(struct light_i2s_priv *chip, bool on)
{
	u32 dma_en = 0;
	u32 i2s_8ch_en = 0;

	if(!chip->regs) {
		return;
	}

	if (on) {
		dma_en |= DMACR_RDMAE_EN;
		i2s_8ch_en |= IISEN_I2SEN;
		writel(I2S_DMA_RX_THRESHOLD, chip->regs + I2S_DMARDLR);
	} else {
		dma_en &= ~DMACR_RDMAE_EN;
		i2s_8ch_en &= ~IISEN_I2SEN;
	}

	writel(dma_en, chip->regs + I2S_DMACR);
	writel(i2s_8ch_en, chip->regs + I2S_IISEN);
}

static int light_i2s_8ch_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	return 0;
}

static void light_i2s_8ch_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct light_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);


	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		light_snd_rxctrl(priv, 0);

	clk_disable_unprepare(priv->clk);
}

/**
 * light_i2s_8ch_dai_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int light_i2s_8ch_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	int ret = 0;

	struct light_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if(!priv->regmap) {
		return 0;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (tx) {
			light_snd_txctrl(priv, 1);
			priv->state |= I2S_STATE_TX_RUNNING;
		}
		else {
			light_snd_rxctrl(priv, 1);
			priv->state |= I2S_STATE_RX_RUNNING;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (tx) {
			dmaengine_terminate_async(snd_dmaengine_pcm_get_chan(substream));  // work around for DMAC stop issue
			light_snd_txctrl(priv, 0);
			priv->state &= ~I2S_STATE_TX_RUNNING;
		} else {
			light_snd_rxctrl(priv, 0);
			priv->state &= ~I2S_STATE_RX_RUNNING;
		}
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (tx) {
			dmaengine_pause(snd_dmaengine_pcm_get_chan(substream));  // work around for DMAC stop issue
			light_snd_txctrl(priv, 0);
		} else {
			light_snd_rxctrl(priv, 0);
		}
		break;
    default:
        return -EINVAL;

	}

	return ret;
}

static int light_i2s_8ch_set_fmt_dai(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct light_i2s_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	u32 cnfout = 0;
	u32 cnfin = 0;

	if(!priv->regmap) {
		return 0;
	}

	pm_runtime_resume_and_get(priv->dev);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		cnfout |= IISCNFOUT_TSAFS_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		cnfout |= IISCNFOUT_TSAFS_RIGHT_JUSTIFIED;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		cnfout |= IISCNFOUT_TSAFS_LEFT_JUSTIFIED;
		break;
	default:
		pr_err("Unknown fmt dai\n");
		return -EINVAL;
	}

	regmap_update_bits(priv->regmap, I2S_IISCNF_OUT,
			IISCNFOUT_TSAFS_MSK,
			cnfout);

	cnfin |= CNFIN_I2S_RXMODE_MASTER_MODE;
	regmap_update_bits(priv->regmap, I2S_IISCNF_IN,
			CNFIN_I2S_RXMODE_Msk,
			cnfin);

	pm_runtime_put_sync(priv->dev);

	return 0;
}

static int light_i2s_8ch_dai_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct light_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	u32 val;
	u32 len = 0;
	u32 sclk_sel = 0;
	u32 rate;
	u32 funcmode;
	u32 iiscnf_out;
	u32 iiscnf_in;
	u32 i2s_8ch_en;

	u32 channels = params_channels(params);

	if(!priv->regs || !priv->regmap) {
		return 0;
	}

	rate = params_rate(params);

	iiscnf_out = readl(priv->regs + I2S_IISCNF_OUT);
	iiscnf_in = readl(priv->regs + I2S_IISCNF_IN);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		val |= I2S_DATA_8BIT_WIDTH_32BIT;
		len = 32;
                break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= I2S_DATA_WIDTH_16BIT;
		len = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= I2S_DATA_WIDTH_24BIT;
		len = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= I2S_DATA_WIDTH_32BIT;
		len = 32;
		break;
	default:
		pr_err("Unknown data format\n");
		return -EINVAL;
	}

	sclk_sel = len*STEREO_CHANNEL;

	switch (sclk_sel) {
	case 16:
		val |= FSSTA_SCLK_SEL_16;
		break;
	case 32:
		val |= FSSTA_SCLK_SEL_32;
		break;
	case 48:
		val |= FSSTA_SCLK_SEL_48;
		break;
	case 64:
		val |= FSSTA_SCLK_SEL_64;
		break;
	default:
		pr_err("Not support channel num %d\n", channels);
		return -EINVAL;
	}

	i2s_8ch_en &= ~IISEN_I2SEN;
	writel(i2s_8ch_en, priv->regs + I2S_IISEN);

	regmap_update_bits(priv->regmap, I2S_FSSTA,
			FSSTA_DATAWTH_Msk | FSSTA_SCLK_SEL_Msk,
			val);
	funcmode = readl(priv->regs + I2S_FUNCMODE);
	if (tx) {
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_CH1_ENABLE;
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode |= FUNCMODE_TMODE;
	} else {
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode |= FUNCMODE_CH0_ENABLE;
		funcmode |= FUNCMODE_CH1_ENABLE;
		funcmode |= FUNCMODE_CH2_ENABLE;
		funcmode |= FUNCMODE_CH3_ENABLE;
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode |= FUNCMODE_RMODE;
	}

	writel(funcmode, priv->regs + I2S_FUNCMODE);

	if (channels == MONO_SOURCE) {
		iiscnf_out |= IISCNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in |= CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in |= CNFIN_RVOICEEN_MONO;
	} else {
		iiscnf_out &= ~IISCNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in &= ~CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in &= ~CNFIN_RVOICEEN_MONO;
	}
	iiscnf_in |= CNFIN_I2S_RXMODE_MASTER_MODE;

	if (tx)
		writel(iiscnf_out, priv->regs + I2S_IISCNF_OUT);
	else
		writel(iiscnf_in, priv->regs + I2S_IISCNF_IN);

	light_i2s_8ch_set_div_sclk(priv, rate, DIV_DEFAULT);

	return 0;
}

static int light_hdmi_dai_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct light_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	u32 val;
	u32 len = 0;
	u32 rate;
	u32 funcmode;
	u32 iiscnf_out;
	u32 i2s_8ch_en;

	u32 channels = params_channels(params);

	if(!priv->regs || !priv->regmap) {
		return 0;
	}

	rate = params_rate(params);

	switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			val |= I2S_DATA_WIDTH_16BIT;
			len = 16;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			val |= I2S_DATA_WIDTH_24BIT;
			len = 24;
			break;
		default:
			pr_err("Unknown data format\n");
			return -EINVAL;
	}

	val |= FSSTA_SCLK_SEL_64;

	i2s_8ch_en &= ~IISEN_I2SEN;
	writel(i2s_8ch_en, priv->regs + I2S_IISEN);

	regmap_update_bits(priv->regmap, I2S_FSSTA,
			FSSTA_DATAWTH_Msk | FSSTA_SCLK_SEL_Msk,
			val);
	funcmode = readl(priv->regs + I2S_FUNCMODE);
	if (tx) {
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode |= FUNCMODE_TMODE;
	} else {
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode |= FUNCMODE_RMODE;
	}

	writel(funcmode, priv->regs + I2S_FUNCMODE);

	iiscnf_out = readl(priv->regs + I2S_IISCNF_OUT);
	if (channels == MONO_SOURCE)
		iiscnf_out |= IISCNFOUT_TX_VOICE_EN_MONO;
	else
		iiscnf_out &= ~IISCNFOUT_TX_VOICE_EN_MONO;

	writel(iiscnf_out, priv->regs + I2S_IISCNF_OUT);

	light_i2s_8ch_set_div_sclk(priv, rate, DIV_DEFAULT);

	return 0;
}

static int light_i2s_8ch_dai_probe(struct snd_soc_dai *dai)
{
	struct light_i2s_priv *i2s_8ch = snd_soc_dai_get_drvdata(dai);

	if(i2s_8ch)
		snd_soc_dai_init_dma_data(dai, &i2s_8ch->dma_params_tx,
				&i2s_8ch->dma_params_rx);

	return 0;
}

static const struct snd_soc_dai_ops light_i2s_8ch_dai_ops = {
	.startup	= light_i2s_8ch_dai_startup,
	.shutdown	= light_i2s_8ch_dai_shutdown,
	.trigger	= light_i2s_8ch_dai_trigger,
	.set_fmt	= light_i2s_8ch_set_fmt_dai,
	.hw_params	= light_i2s_8ch_dai_hw_params,
};

static const struct snd_soc_dai_ops light_hdmi_dai_ops = {
	.startup        = light_i2s_8ch_dai_startup,
	.shutdown       = light_i2s_8ch_dai_shutdown,
	.trigger        = light_i2s_8ch_dai_trigger,
	.set_fmt        = light_i2s_8ch_set_fmt_dai,
	.hw_params      = light_hdmi_dai_hw_params,
};

static struct snd_soc_dai_driver light_i2s_8ch_soc_dai[] = {
	{
		.probe = light_i2s_8ch_dai_probe,
		.playback = {
			.rates		= LIGHT_RATES,
			.formats	= LIGHT_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.capture = {
			.rates		= LIGHT_RATES,
			.formats	= LIGHT_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &light_i2s_8ch_dai_ops,
	},
};


static const struct snd_soc_component_driver light_i2s_8ch_soc_component = {
	.name		= "light_i2s_8ch",
};

static int light_pcm_probe(struct platform_device *pdev,struct light_i2s_priv *i2s_8ch)
{
	int ret;

	ret = light_pcm_dma_init(pdev, LIGHT_I2S_DMABUF_SIZE);

	if (ret) {
		pr_err("light_pcm_dma_init error\n");
		return 0;
	}
	return 0;
}

static bool light_i2s_8ch_wr_reg(struct device *dev, unsigned int reg)
{
    return true;
}

static bool light_i2s_8ch_rd_reg(struct device *dev, unsigned int reg)
{
    return true;
}

static const struct regmap_config light_i2s_8ch_regmap_config = {
        .reg_bits = 32,
        .reg_stride = 4,
        .val_bits = 32,
        .max_register = I2S_DR4,
        .writeable_reg = light_i2s_8ch_wr_reg,
        .readable_reg = light_i2s_8ch_rd_reg,
        .cache_type = REGCACHE_NONE,
};

#if 0
static int light_audio_pinconf_set(struct device *dev, unsigned int pin_id, unsigned int val)
{
	struct light_i2s_priv *priv = dev_get_drvdata(dev);
	unsigned int shift;
	unsigned int mask = 0;

	if(!priv->audio_pin_regmap) {
		return 0;
	}

	priv->cfg_off = 0xC;

	shift = (((pin_id-25) % 2) << 4);
	mask |= (0xFFFF << shift);
	val = (val << shift);

	return regmap_update_bits(priv->audio_pin_regmap,
				LIGHT_AUDIO_PAD_CONFIG(pin_id),mask, val);
}

static int light_audio_pinctrl(struct device *dev)
{
	return 0;
}
#endif

static int light_i2s_8ch_runtime_suspend(struct device *dev)
{
	struct light_i2s_priv *priv = dev_get_drvdata(dev);

	if(!priv->regmap) {
		return 0;
	}

	regcache_cache_only(priv->regmap, true);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int light_i2s_8ch_runtime_resume(struct device *dev)
{
	struct light_i2s_priv *priv = dev_get_drvdata(dev);
	int ret;

	if(!priv->regmap) {
		return 0;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(priv->dev, "clock enable failed %d\n", ret);
		return ret;
	}

	regcache_cache_only(priv->regmap, false);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int light_i2s_8ch_suspend(struct device *dev)
{
    struct light_i2s_priv *priv = dev_get_drvdata(dev);

	if(!priv->regmap) {
		return 0;
	}

	pm_runtime_get_sync(dev);

	regmap_read(priv->regmap, I2S_DIV0_LEVEL, &priv->suspend_div0_level);
	regmap_read(priv->regmap, I2S_DIV3_LEVEL, &priv->suspend_div3_level);
	regmap_read(priv->regmap, I2S_IISCNF_IN, &priv->suspend_iiscnf_in);
	regmap_read(priv->regmap, I2S_FSSTA, &priv->suspend_fssta);
	regmap_read(priv->regmap, I2S_IISCNF_OUT, &priv->suspend_ii2cnf_out);
	regmap_read(priv->regmap, I2S_FADTLR, &priv->suspend_fadtlr);
	regmap_read(priv->regmap, I2S_SCCR, &priv->suspend_sccr);
	regmap_read(priv->regmap, I2S_TXFTLR, &priv->suspend_txftlr);
	regmap_read(priv->regmap, I2S_RXFTLR, &priv->suspend_rxftlr);
	regmap_read(priv->regmap, I2S_IMR, &priv->suspend_imr);
	regmap_read(priv->regmap, I2S_DMATDLR, &priv->suspend_dmatdlr);
	regmap_read(priv->regmap, I2S_DMARDLR, &priv->suspend_dmardlr);
	regmap_read(priv->regmap, I2S_FUNCMODE, &priv->suspend_funcmode);

    regmap_read(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG, &priv->cpr_peri_div_sel);
    regmap_read(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG, &priv->cpr_peri_ctrl);
    regmap_read(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG, &priv->cpr_peri_clk_sel);
	
	regmap_update_bits(priv->audio_cpr_regmap,
							CPR_IP_RST_REG, CPR_I2S8CH_SRST_N_SEL_MSK, CPR_I2S8CH_SRST_N_SEL(0));

    pm_runtime_put_sync(dev);

	return 0;
}

static int light_i2s_8ch_resume(struct device *dev)
{
    struct light_i2s_priv *priv = dev_get_drvdata(dev);
    int ret;

	if(!priv->regmap) {
		return 0;
	}

    pm_runtime_get_sync(dev);

    regmap_update_bits(priv->audio_cpr_regmap,
                        CPR_IP_RST_REG, CPR_I2S8CH_SRST_N_SEL_MSK, CPR_I2S8CH_SRST_N_SEL(1));

    regmap_write(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG, priv->cpr_peri_ctrl);


	regmap_write(priv->regmap, I2S_IISEN, 0);
	regmap_write(priv->regmap, I2S_FSSTA, priv->suspend_fssta);
	regmap_write(priv->regmap, I2S_FUNCMODE, priv->suspend_funcmode | FUNCMODE_TMODE_WEN | FUNCMODE_RMODE_WEN);
	regmap_write(priv->regmap, I2S_IISCNF_IN, priv->suspend_iiscnf_in);
	regmap_write(priv->regmap, I2S_IISCNF_OUT, priv->suspend_ii2cnf_out);
    regmap_write(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG, priv->cpr_peri_clk_sel);
	regmap_write(priv->regmap, I2S_DIV0_LEVEL, priv->suspend_div0_level);
	regmap_write(priv->regmap, I2S_DIV3_LEVEL, priv->suspend_div3_level);
    regmap_write(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG, priv->cpr_peri_div_sel);

	pm_runtime_put_sync(dev);

    return ret;
}
#endif

static const struct of_device_id light_i2s_8ch_of_match[] = {
	{ .compatible = "light,light-i2s-8ch"},
	{},
};
MODULE_DEVICE_TABLE(of, light_i2s_8ch_of_match);

struct light_i2s_priv *host_priv;

static int light_audio_i2s_8ch_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	const char *sprop;
	const uint32_t *iprop;
	struct light_i2s_priv *priv;
	struct resource *res;
	struct device *dev = &pdev->dev;
	unsigned int irq;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv),
		GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	sprop = of_get_property(np, "light,mode", NULL);
	if (sprop) {
		if (!strcmp(sprop, "i2s-master"))
			priv->dai_fmt = SND_SOC_DAIFMT_I2S;
		else
			printk("mode is not i2s-master");
	}

	sprop = of_get_property(np, "light,sel", NULL);
	if (sprop) {
		strcpy(priv->name, sprop);
	}

	iprop = of_get_property(np, "light,dma_maxburst", NULL);
	if (iprop)
		priv->dma_maxburst = be32_to_cpup(iprop);
	else
		priv->dma_maxburst = 8;

	dev_set_drvdata(&pdev->dev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if(i2s_8ch_probe_flag) {
		printk("i2s_8ch already probe\n");
		priv->regs = NULL;
		priv->regmap = NULL;
		priv->audio_cpr_regmap = NULL;
	} else {
		printk("i2s_8ch probing\n");
		i2s_8ch_probe_flag = 1;
		host_priv = priv;

		priv->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->regs))
			return PTR_ERR(priv->regs);

		priv->regmap = devm_regmap_init_mmio(&pdev->dev, priv->regs,
								&light_i2s_8ch_regmap_config);
		if (IS_ERR(priv->regmap)) {
			dev_err(&pdev->dev,
						"Failed to initialise managed register map\n");
			return PTR_ERR(priv->regmap);
		}

		priv->audio_cpr_regmap = syscon_regmap_lookup_by_phandle(np, "audio-cpr-regmap");
		if (IS_ERR(priv->audio_cpr_regmap)) {
			dev_err(dev, "cannot find regmap for audio cpr register\n");
			return -EINVAL;
		}

		// enable i2s sync
		light_audio_cpr_set(priv, CPR_PERI_CTRL_REG, CPR_VAD_I2SIN_SYNC_MSK, CPR_VAD_I2SIN_SYNC_EN);

		priv->clk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(priv->clk))
			return PTR_ERR(priv->clk);

		pm_runtime_enable(&pdev->dev);
		pm_runtime_resume_and_get(&pdev->dev); // clk gate is enabled by hardware as default register value
		pm_runtime_put_sync(&pdev->dev);

		irq = platform_get_irq(pdev, 0);

		if (!res || (int)irq <= 0) {
			dev_err(&pdev->dev, "Not enough light platform resources.\n");
			return -ENODEV;
		}
	}

	priv->audio_pin_regmap = NULL;

	priv->dma_params_tx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	priv->dma_params_tx.maxburst = priv->dma_maxburst;
	priv->dma_params_rx.maxburst = priv->dma_maxburst;

	if (!strcmp(priv->name, AUDIO_I2S_8CH_SD0)) {
		priv->dma_params_tx.addr = res->start + I2S_DR;
		priv->dma_params_rx.addr = res->start + I2S_DR;
	} else if (!strcmp(priv->name, AUDIO_I2S_8CH_SD1)) {
		priv->dma_params_tx.addr = res->start + I2S_DR1;
		priv->dma_params_rx.addr = res->start + I2S_DR1;
	} else if (!strcmp(priv->name, AUDIO_I2S_8CH_SD2)) {
		priv->dma_params_tx.addr = res->start + I2S_DR2;
		priv->dma_params_rx.addr = res->start + I2S_DR2;
	} else if (!strcmp(priv->name, AUDIO_I2S_8CH_SD3)) {
		priv->dma_params_tx.addr = res->start + I2S_DR3;
		priv->dma_params_rx.addr = res->start + I2S_DR3;
	}

	light_pcm_probe(pdev, priv);

	ret = devm_snd_soc_register_component(&pdev->dev, &light_i2s_8ch_soc_component,
				    light_i2s_8ch_soc_dai, ARRAY_SIZE(light_i2s_8ch_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd component register\n");
		goto err_pm_disable;
	}

	return ret;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int light_i2s_8ch_remove(struct platform_device *pdev)
{
        struct light_i2s_priv *priv = dev_get_drvdata(&pdev->dev);

        pm_runtime_disable(&pdev->dev);
        if (!pm_runtime_status_suspended(&pdev->dev))
                light_i2s_8ch_runtime_suspend(&pdev->dev);

        clk_disable_unprepare(priv->clk);

        return 0;
}

static const struct dev_pm_ops light_i2s_8ch_pm_ops = {
        SET_RUNTIME_PM_OPS(light_i2s_8ch_runtime_suspend, light_i2s_8ch_runtime_resume,
                           NULL)
		SET_SYSTEM_SLEEP_PM_OPS(light_i2s_8ch_suspend,
				     light_i2s_8ch_resume)
};

static struct platform_driver light_i2s_8ch_driver = {
	.driver 	= {
		.name	= "light-pcm-audio-8ch",
		.pm	= &light_i2s_8ch_pm_ops,
		.of_match_table = light_i2s_8ch_of_match,
	},
	.probe		= light_audio_i2s_8ch_probe,
	.remove	= light_i2s_8ch_remove,
};

module_platform_driver(light_i2s_8ch_driver);

MODULE_AUTHOR("shuofeng.ren <shuofeng.rsf@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead Light audio driver");
MODULE_LICENSE("GPL v2");
