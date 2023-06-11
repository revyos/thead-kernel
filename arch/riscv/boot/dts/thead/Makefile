# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_OF_OVERLAY),y)
DTC_FLAGS += -@
endif

dtb-$(CONFIG_SOC_THEAD) += ice.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu.dtb light_mpw.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu-npu-fce.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu-gpu.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu-dsp.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu-audio.dtb light-a-val-audio-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-fm-emu-dsi0-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val.dtb light-a-val-sv.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-ddr2G.dtb light-a-val-ddr1G.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-npu-fce.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-iso7816.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-nand.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-audio.dtb light-a-val-audio-i2s-8ch.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-audio-tdm.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-audio-spdif.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-dsi0.dtb light-a-val-dsi1.dtb light-a-val-hdmi.dtb light-a-val-dsi0-dsi1.dtb light-a-val-dsi0-hdmi.dtb light-a-val-dsi0-hdmi-audio.dtb light-a-val-dpi0.dtb light-a-val-dpi0-dpi1.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-wcn.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-gpio-keys.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-khv.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-sec.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-miniapp-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-product.dtb
dtb-$(CONFIG_SOC_THEAD) += light-b-product.dtb light-b-product-sec.dtb
dtb-$(CONFIG_SOC_THEAD) += light-b-product-ddr1G.dtb
dtb-$(CONFIG_SOC_THEAD) += light-b-product-miniapp-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-full.dtb
dtb-$(CONFIG_SOC_THEAD) += light-ant-ref.dtb
dtb-$(CONFIG_SOC_THEAD) += light-ant-discrete.dtb
dtb-$(CONFIG_SOC_THEAD) += light-beagle.dtb
dtb-$(CONFIG_SOC_THEAD) += light-lpi4a.dtb light-lpi4a-ddr2G.dtb light-lpi4a-16gb.dtb light-lpi4a-dsi0-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-lpi4a-cluster.dtb light-lpi4a-cluster-16gb.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-ref.dtb light-a-ref-dsi0.dtb light-a-ref-dsi0-hdmi.dtb
dtb-$(CONFIG_SOC_THEAD) += light-b-ref.dtb
dtb-$(CONFIG_SOC_THEAD) += light-a-val-crash.dtb light-b-product-crash.dtb light-ant-ref-crash.dtb light-ant-discrete-crash.dtb
dtb-$(CONFIG_SOC_THEAD) += light-b-power.dtb

dtb-$(CONFIG_SOC_THEAD) += light-a-val-android.dtb

dtb-$(CONFIG_SOC_THEAD) += fire-emu.dtb fire-emu-crash.dtb
dtb-$(CONFIG_SOC_THEAD) += fire-emu-soc-base.dtb fire-emu-soc-c910x4.dtb fire-emu-gpu-dpu-dsi0.dtb fire-emu-vi-dsp-vo.dtb fire-emu-vi-vp-vo.dtb
dtb-$(CONFIG_SOC_THEAD) += fire-emu-soc-base-sec.dtb
dtb-$(CONFIG_SOC_THEAD) += light-lpi4a-laptop.dtb

targets += dtbs dtbs_install
targets += $(dtb-y)

subdir-y	:= overlays
