/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */
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
#include "light-pcm.h"
#include "light-spdif.h"
#include <linux/dmaengine.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/pinctrl/light-fm-aon-pinctrl.h>

#define LIGHT_RATES SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000

static int light_spdif_dai_probe(struct snd_soc_dai *dai)
{
    struct light_spdif_priv *priv = snd_soc_dai_get_drvdata(dai);

    if (priv) {
        snd_soc_dai_init_dma_data(dai, &priv->dma_params_tx, &priv->dma_params_rx);
    } else {
        return -EIO;
    }

    return 0;
}

static int light_spdif_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
    //struct light_spdif_priv *priv = snd_soc_dai_get_drvdata(dai);

    pm_runtime_get_sync(dai->dev);

	return 0;
}

static void light_spdif_snd_txctrl(struct light_spdif_priv *priv, char on)
{
    regmap_update_bits(priv->regmap, SPDIF_TX_DMA_EN,
            SPDIF_TDMA_EN_MSK, SPDIF_TDMA_EN_SEL(on));
    regmap_update_bits(priv->regmap, SPDIF_TX_EN,
            SPDIF_TXEN_MSK, SPDIF_TXEN_SEL(on));
    regmap_read(priv->regmap, SPDIF_TX_FIFO_TH, &priv->suspend_tx_fifo_th);

    return;
}

static void light_spdif_snd_rxctrl(struct light_spdif_priv *priv, char on)
{

    regmap_update_bits(priv->regmap, SPDIF_RX_DMA_EN,
            SPDIF_RDMA_EN_MSK, SPDIF_RDMA_EN_SEL(on));
    regmap_update_bits(priv->regmap, SPDIF_RX_EN,
            SPDIF_RXEN_MSK, SPDIF_RXEN_SEL(on));

    return;
}

static void light_spdif_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct light_spdif_priv *priv = snd_soc_dai_get_drvdata(dai);

    if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
	    light_spdif_snd_rxctrl(priv, 0);
    } else {
        light_spdif_snd_txctrl(priv, 0);
    }

	pm_runtime_put(dai->dev);
}

static int light_spdif_dai_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
    int ret = 0;
    struct light_spdif_priv *priv = snd_soc_dai_get_drvdata(dai);
    bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

    switch(cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_RESUME:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
            if (tx) {
                light_spdif_snd_txctrl(priv, 1);
                priv->state |= SPDIF_STATE_TX_RUNNING;
            }
            else {
                light_spdif_snd_rxctrl(priv, 1);
                priv->state |= SPDIF_STATE_RX_RUNNING;
            }
            break;
        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
            if (tx) {
                dmaengine_terminate_async(snd_dmaengine_pcm_get_chan(substream));  // work around for DMAC stop issue
                light_spdif_snd_txctrl(priv, 0);
                priv->state &= ~SPDIF_STATE_TX_RUNNING;
            }
            else {
                light_spdif_snd_rxctrl(priv, 0);
                priv->state &= ~SPDIF_STATE_RX_RUNNING;
            }
            break;
        case SNDRV_PCM_TRIGGER_SUSPEND:
            if (tx) {
                dmaengine_pause(snd_dmaengine_pcm_get_chan(substream));  // work around for DMAC stop issue
                light_spdif_snd_txctrl(priv, 0);
            }
            else
                light_spdif_snd_rxctrl(priv, 0);
            break;
        default:
            return -EINVAL;
    }

    return ret;
}

static u32 light_special_sample_rates[] = { 44100, 88200 };
#define AUDIO_DIVCLK0  98304000 //  49152000
#define AUDIO_DIVCLK1 135475200

static int light_spdif_dai_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
    struct light_spdif_priv *priv =  snd_soc_dai_get_drvdata(dai);
    u32 datawidth, chn_num, i;
    u32 sample_rate = params_rate(params);
    bool is_divclk1 = false; //audio_divclk1 for 44.1k...etc. audio_divclk0 for 48k....etc
    u32 src_clk, tx_div;

    switch (params_channels(params)) {
        case 1:
            chn_num = SPDIF_TX_DATAMODE_1CH;
            break;
        case 2:
            chn_num = SPDIF_TX_DATAMODE_2CH;
            break;
        default:
            dev_err(priv->dev, "unsupported channel num\n");
            return -EINVAL;
    }

    switch (params_format(params)) {
        case SNDRV_PCM_FORMAT_S16_LE:
            datawidth = SPDIF_TX_DATAMODE_16BIT_PACKED;
            break;
        case SNDRV_PCM_FORMAT_S20_LE:
            datawidth = SPDIF_TX_DATAMODE_20BIT;
            break;
        case SNDRV_PCM_FORMAT_S24_LE:
            datawidth = SPDIF_TX_DATAMODE_24BIT;
            break;
        default:
            dev_err(priv->dev, "unsupported data format\n");
            return -EINVAL;
    }

    for (i = 0; i < ARRAY_SIZE(light_special_sample_rates); i++) {
        if (light_special_sample_rates[i] == sample_rate) {
            is_divclk1 = true;
            break;
        }
    }

    if (is_divclk1 == false) { // audio_divclk0=98304000 for 48k
        regmap_update_bits(priv->audio_cpr_regmap,
                        CPR_PERI_CLK_SEL_REG, CPR_SPDIF_SRC_SEL_MSK, CPR_SPDIF_SRC_SEL(2));
        src_clk = AUDIO_DIVCLK0;
    } else { // audio_divclk1=135475200 for 44.1k
        regmap_update_bits(priv->audio_cpr_regmap,
                        CPR_PERI_CLK_SEL_REG, CPR_SPDIF_SRC_SEL_MSK, CPR_SPDIF_SRC_SEL(1));
        src_clk = AUDIO_DIVCLK1;
    }

    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
        tx_div = src_clk / (sample_rate * (32 * 2) * 2);
        tx_div = tx_div / 2 - 1;

        regmap_update_bits(priv->regmap, SPDIF_TX_CTL,
                SPDIF_TX_DIV_MSK, SPDIF_TX_DIV_SEL(tx_div));
        regmap_update_bits(priv->regmap, SPDIF_TX_CTL,
                SPDIF_TX_DIV_BYPASS_MSK, SPDIF_TX_DIV_BYPASS_SEL(1));
        regmap_update_bits(priv->regmap, SPDIF_TX_CTL,
                SPDIF_TX_CH_SEL_MSK, SPDIF_TX_CH_SEL_SEL(chn_num));
        regmap_update_bits(priv->regmap, SPDIF_TX_CTL,
                SPDIF_TX_DATAMODE_MSK, SPDIF_TX_DATAMODE_SEL(datawidth));
        // Channel Status
        regmap_update_bits(priv->regmap, SPDIF_TX_CS_A,
                SPDIF_TX_T_FS_SEL_MSK, SPDIF_TX_T_FS_SEL_SEL(2)); // 2 for 48k
        regmap_update_bits(priv->regmap, SPDIF_TX_CS_A,
                SPDIF_TX_T_CH_NUM_MSK, SPDIF_TX_T_CH_NUM_SEL(1)); // Left in 2 channel format
        regmap_update_bits(priv->regmap, SPDIF_TX_CS_B,
                SPDIF_TX_T_B_FS_SEL_MSK, SPDIF_TX_T_B_FS_SEL_SEL(2)); // 2 for 48k
        regmap_update_bits(priv->regmap, SPDIF_TX_CS_B,
                SPDIF_TX_T_B_CH_NUM_MSK, SPDIF_TX_T_B_CH_NUM_SEL(2)); // Right in 2 channel format
    } else {
        regmap_update_bits(priv->regmap, SPDIF_RX_CTL,
                SPDIF_RX_DIV_MSK, SPDIF_RX_DIV_SEL(0));
        regmap_update_bits(priv->regmap, SPDIF_RX_CTL,
                SPDIF_RX_DIV_BYPASS_MSK, SPDIF_RX_DIV_BYPASS_SEL(0)); // spdif_rx_clk = src_clk
        regmap_update_bits(priv->regmap, SPDIF_RX_CTL,
                SPDIF_RX_DATAMODE_MSK, SPDIF_RX_DATAMODE_SEL(datawidth));
        regmap_update_bits(priv->regmap, SPDIF_RX_DMA_TH,
                SPDIF_RDMA_TH_MSK, SPDIF_RDMA_TH_SEL(8));
    }

    return 0;
}

static const struct snd_soc_dai_ops light_spdif_dai_ops = {
    .startup = light_spdif_dai_startup,
    .shutdown = light_spdif_dai_shutdown,
    .trigger = light_spdif_dai_trigger,
    .hw_params = light_spdif_dai_hw_params,
};

static struct snd_soc_dai_driver light_spdif_soc_dai = {
    .probe = light_spdif_dai_probe,
    .playback = {
        .stream_name = "Playback",
        .rates = LIGHT_RATES,
        .formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S20_LE | SNDRV_PCM_FMTBIT_S16_LE,
        .channels_min = 2,
        .channels_max = 2,
    },
    .capture = {
        .stream_name = "Capture",
        .rates = LIGHT_RATES,
        .formats = SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S20_LE,
        .channels_min = 2,
        .channels_max = 2,
    },
    .ops = &light_spdif_dai_ops,
    .symmetric_rates = 1,
}; 

static const struct snd_soc_component_driver light_spdif_soc_component = {
	.name		= "light_spdif",
};

#if 0
static void light_spdif_pinctrl(struct light_spdif_priv *priv)
{
    //GPIO_SEL      AUDIO_PA21~24
    regmap_update_bits(priv->audio_pin_regmap,
				0x0,0x1E00000, 0x1E00000);
    //MUX
    regmap_update_bits(priv->audio_pin_regmap,
				0x8,0x3FC00, 0x15400);    
    //strength
    regmap_update_bits(priv->audio_pin_regmap,
				0x38,0xF000F, 0x70007); // AUDIO_PA22	SPDIF0_DOUT / AUDIO_PA23	SPDIF1_DOUT
    return;
}
#endif

static const struct regmap_config light_spdif_regmap_config = {
        .reg_bits = 32,
        .reg_stride = 4,
        .val_bits = 32,
        .max_register = SPDIF_RX_USER_B5,
        .cache_type = REGCACHE_NONE,
};

static int light_spdif_runtime_suspend(struct device *dev)
{
    struct light_spdif_priv *priv = dev_get_drvdata(dev);

    regmap_update_bits(priv->regmap, SPDIF_EN,
                SPDIF_SPDIFEN_MSK, SPDIF_SPDIFEN_SEL(0));

    if (priv->id == 0) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF0_SRST_N_SEL_MSK, CPR_SPDIF0_SRST_N_SEL(0));
    } else if (priv->id == 1) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF1_SRST_N_SEL_MSK, CPR_SPDIF1_SRST_N_SEL(0));
    }

    clk_disable_unprepare(priv->clk);

	return 0;
}

static int light_spdif_runtime_resume(struct device *dev)
{
    struct light_spdif_priv *priv = dev_get_drvdata(dev);
    int ret;

    ret = clk_prepare_enable(priv->clk);
    if (ret) {
            dev_err(priv->dev, "clock enable failed %d\n", ret);
            return ret;
    }
    
    if (priv->id == 0) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF0_SRST_N_SEL_MSK, CPR_SPDIF0_SRST_N_SEL(1));
    } else if (priv->id == 1) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF1_SRST_N_SEL_MSK, CPR_SPDIF1_SRST_N_SEL(1));
    }

    regmap_update_bits(priv->regmap, SPDIF_EN,
        SPDIF_SPDIFEN_MSK, SPDIF_SPDIFEN_SEL(1));

    return ret;
}

#ifdef CONFIG_PM_SLEEP
static int light_spdif_suspend(struct device *dev)
{
    struct light_spdif_priv *priv = dev_get_drvdata(dev);

    pm_runtime_get_sync(dev);

    regmap_read(priv->regmap, SPDIF_TX_EN, &priv->suspend_tx_en);
    regmap_read(priv->regmap, SPDIF_TX_CTL, &priv->suspend_tx_ctl);
    regmap_read(priv->regmap, SPDIF_TX_DMA_EN, &priv->suspend_tx_dma_en);
    regmap_read(priv->regmap, SPDIF_RX_EN, &priv->suspend_rx_en);
    regmap_read(priv->regmap, SPDIF_RX_CTL, &priv->suspend_rx_ctl);
    regmap_read(priv->regmap, SPDIF_RX_DMA_EN, &priv->suspend_rx_dma_en);
    regmap_read(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG, &priv->cpr_peri_div_sel);
    regmap_read(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG, &priv->cpr_peri_ctrl);
    regmap_read(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG, &priv->cpr_peri_clk_sel);

    regmap_update_bits(priv->regmap, SPDIF_EN,
                SPDIF_SPDIFEN_MSK, SPDIF_SPDIFEN_SEL(0));

    if (priv->id == 0) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF0_SRST_N_SEL_MSK, CPR_SPDIF0_SRST_N_SEL(0));
    } else if (priv->id == 1) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF1_SRST_N_SEL_MSK, CPR_SPDIF1_SRST_N_SEL(0));
    }

    pm_runtime_put_sync(dev);

	return 0;
}

static int light_spdif_resume(struct device *dev)
{
    struct light_spdif_priv *priv = dev_get_drvdata(dev);
    int ret;

	pm_runtime_get_sync(dev);

    regmap_write(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG, priv->cpr_peri_div_sel);
    regmap_write(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG, priv->cpr_peri_ctrl);
    regmap_write(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG, priv->cpr_peri_clk_sel);

    if (priv->id == 0) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF0_SRST_N_SEL_MSK, CPR_SPDIF0_SRST_N_SEL(1));
    } else if (priv->id == 1) {
        regmap_update_bits(priv->audio_cpr_regmap,
                                CPR_IP_RST_REG, CPR_SPDIF1_SRST_N_SEL_MSK, CPR_SPDIF1_SRST_N_SEL(1));
    }

    regmap_update_bits(priv->regmap, SPDIF_EN,
        SPDIF_SPDIFEN_MSK, SPDIF_SPDIFEN_SEL(1));

    regmap_write(priv->regmap, SPDIF_TX_EN, priv->suspend_tx_en);
    regmap_write(priv->regmap, SPDIF_TX_CTL, priv->suspend_tx_ctl);
    regmap_write(priv->regmap, SPDIF_TX_DMA_EN, priv->suspend_tx_dma_en);
    regmap_write(priv->regmap, SPDIF_RX_EN, priv->suspend_rx_en);
    regmap_write(priv->regmap, SPDIF_RX_CTL, priv->suspend_rx_ctl);
    regmap_write(priv->regmap, SPDIF_RX_DMA_EN, priv->suspend_rx_dma_en);

    pm_runtime_put_sync(dev);

    return ret;
}
#endif

static const struct of_device_id light_spdif_of_match[] = {
	{ .compatible = "light,light-spdif"},
	{},
};
MODULE_DEVICE_TABLE(of, light_spdif_of_match);

irqreturn_t spdif_interrupt(int irq, void* dev_id)
{
    //struct light_spdif_priv* priv = (struct light_spdif_priv*)dev_id;
    return IRQ_HANDLED;
}

static int light_spdif_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    const uint32_t *iprop;
    struct light_spdif_priv *priv;
    struct resource *res;
    struct device *dev = &pdev->dev;
    int ret = 0;

    priv = devm_kzalloc(&pdev->dev, sizeof(struct light_spdif_priv), GFP_KERNEL);
    if (!priv) {
        return -ENOMEM;
    }

    priv->dev = dev;

	iprop = of_get_property(np, "light,dma_maxburst", NULL);
	if (iprop)
		priv->dma_maxburst = be32_to_cpup(iprop);
	else
		priv->dma_maxburst = 8;

    iprop = of_get_property(np, "id", NULL);
	if (iprop)
		priv->id = be32_to_cpup(iprop);

    dev_set_drvdata(dev, priv);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    priv->clk = devm_clk_get(&pdev->dev, "pclk");
    if (IS_ERR(priv->clk))
                return PTR_ERR(priv->clk);

    priv->regs = devm_ioremap_resource(dev, res);
    if (IS_ERR(priv->regs)) {
        return PTR_ERR(priv->regs);
    }

    priv->regmap = devm_regmap_init_mmio(dev, priv->regs,
                                                &light_spdif_regmap_config);
        if (IS_ERR(priv->regmap)) {
                dev_err(dev, "Failed to initialise managed register map\n");
                return PTR_ERR(priv->regmap);
        }

    priv->audio_pin_regmap = syscon_regmap_lookup_by_phandle(np, "audio-pin-regmap");
    if (IS_ERR(priv->audio_pin_regmap)) {
        dev_err(dev, "cannot find regmap for audio system register\n");
        return -EINVAL;
    } else {
        //light_spdif_pinctrl(priv);
    }

    priv->audio_cpr_regmap = syscon_regmap_lookup_by_phandle(np, "audio-cpr-regmap");
    if (IS_ERR(priv->audio_cpr_regmap)) {
        dev_err(dev, "cannot find regmap for audio cpr register\n");
        return -EINVAL;
    }

    pm_runtime_enable(&pdev->dev);
    pm_runtime_resume_and_get(&pdev->dev); // clk gate is enabled by hardware as default register value
    pm_runtime_put_sync(&pdev->dev);

    //AUDIO_DIV1 set to 1/6. 812.8512MHz / 6 = 135.4752MHz
    regmap_update_bits(priv->audio_cpr_regmap,
                            CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV1_SEL_MSK, CPR_AUDIO_DIV1_SEL(5));
    regmap_update_bits(priv->audio_cpr_regmap,
                            CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV1_CG_MSK, CPR_AUDIO_DIV1_CG(1));
    //AUDIO_DIV0 set to 1/3. 294.912MHz / 3 = 98304000Hz
    regmap_update_bits(priv->audio_cpr_regmap,
                            CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_SEL_MSK, CPR_AUDIO_DIV0_SEL(2)); // 2=98304000 5=49152000
    regmap_update_bits(priv->audio_cpr_regmap,
                            CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_CG_MSK, CPR_AUDIO_DIV0_CG(1));
    //enable spdif0/1 sync
    regmap_update_bits(priv->audio_cpr_regmap,
                            CPR_PERI_CTRL_REG, CPR_SPDIF_SYNC_MSK, CPR_SPDIF_SYNC_EN);

    priv->irq = platform_get_irq(pdev, 0);
    if (priv->irq== 0) {
        dev_err(dev, "could not map IRQ.\n");
        return -ENXIO;
    }

    //ret = request_irq(priv->irq , spdif_interrupt,
    //		IRQF_SHARED|IRQF_TRIGGER_RISING, "AUDIO_TDM_IRQ", (char *)priv);
    //if (ret) {
    //	dev_err(dev, "%s[%d]:request irq error!\n", __func__, __LINE__);
    //	return ret;
    //}

    priv->dma_params_tx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
    priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
    priv->dma_params_tx.maxburst = priv->dma_maxburst;
    priv->dma_params_rx.maxburst = priv->dma_maxburst;
    priv->dma_params_tx.addr = res->start + SPDIF_TX_FIFO_DR;
    priv->dma_params_rx.addr = res->start + SPDIF_RX_FIFO_DR;

    ret = light_pcm_dma_init(pdev, LIGHT_TDM_DMABUF_SIZE);
	if (ret) {
		dev_err(dev, "light_pcm_dma_init error\n");
		return -EIO;
	}

    ret = devm_snd_soc_register_component(dev, &light_spdif_soc_component,
                        &light_spdif_soc_dai, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd component register\n");
	}

    return ret;
}

static int light_spdif_remove(struct platform_device *pdev)
{
    struct light_spdif_priv *priv = dev_get_drvdata(&pdev->dev);
    pm_runtime_disable(&pdev->dev);
    if (!pm_runtime_status_suspended(&pdev->dev))
            light_spdif_runtime_suspend(&pdev->dev);
    clk_disable_unprepare(priv->clk);
    return 0;
}

static const struct dev_pm_ops light_spdif_pm_ops = {
    SET_RUNTIME_PM_OPS(light_spdif_runtime_suspend, light_spdif_runtime_resume, NULL)
    SET_SYSTEM_SLEEP_PM_OPS(light_spdif_suspend,
				     light_spdif_resume)
};

static struct platform_driver light_spdif_driver = {
    .driver = {
        .name = "light-spdif-audio",
        .pm = &light_spdif_pm_ops,
        .of_match_table = light_spdif_of_match,
    },
    .probe = light_spdif_probe,
    .remove = light_spdif_remove,
};

module_platform_driver(light_spdif_driver);

MODULE_AUTHOR("nanli.yd <nanli.yd@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead Light spdif driver");
MODULE_LICENSE("GPL v2");
