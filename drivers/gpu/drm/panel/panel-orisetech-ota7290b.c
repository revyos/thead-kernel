// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Radxa Limited
 * Copyright (c) 2022 Edgeble AI Technologies Pvt. Ltd.
 *
 * Author:
 * - Jagan Teki <jagan@amarulasolutions.com>
 * - Stephen Chen <stephen@radxa.com>
 *
 * This file based on panel-jadard-jd9365da-h3.c
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

struct orisetech_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
};

struct orisetech {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct orisetech_panel_desc *desc;

	struct regulator *vdd;
	struct regulator *vccio;
	struct gpio_desc *reset;
};

static inline struct orisetech *panel_to_orisetech(struct drm_panel *panel)
{
	return container_of(panel, struct orisetech, panel);
}

static int orisetech_enable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct orisetech *orisetech = panel_to_orisetech(panel);
	struct mipi_dsi_device *dsi = orisetech->dsi;
	int err;

	msleep(10);

	err = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (err < 0)
		DRM_DEV_ERROR(dev, "failed to exit sleep mode ret = %d\n", err);

	err =  mipi_dsi_dcs_set_display_on(dsi);
	if (err < 0)
		DRM_DEV_ERROR(dev, "failed to set display on ret = %d\n", err);

	return 0;
}

static int orisetech_disable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct orisetech *orisetech = panel_to_orisetech(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(orisetech->dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(orisetech->dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int orisetech_prepare(struct drm_panel *panel)
{
	struct orisetech *orisetech = panel_to_orisetech(panel);
	int ret;

	ret = regulator_enable(orisetech->vccio);
	if (ret)
		return ret;

	ret = regulator_enable(orisetech->vdd);
	if (ret)
		return ret;

	gpiod_set_value(orisetech->reset, 1);
	msleep(5);

	gpiod_set_value(orisetech->reset, 0);
	msleep(120);

	gpiod_set_value(orisetech->reset, 1);
	msleep(120);

	return 0;
}

static int orisetech_unprepare(struct drm_panel *panel)
{
	struct orisetech *orisetech = panel_to_orisetech(panel);

	gpiod_set_value(orisetech->reset, 1);
	msleep(120);

	regulator_disable(orisetech->vdd);
	regulator_disable(orisetech->vccio);

	return 0;
}

static int orisetech_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct orisetech *orisetech = panel_to_orisetech(panel);
	const struct drm_display_mode *desc_mode = &orisetech->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		DRM_DEV_ERROR(&orisetech->dsi->dev, "failed to add mode %ux%ux@%u\n",
			      desc_mode->hdisplay, desc_mode->vdisplay,
			      drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs orisetech_funcs = {
	.disable = orisetech_disable,
	.unprepare = orisetech_unprepare,
	.prepare = orisetech_prepare,
	.enable = orisetech_enable,
	.get_modes = orisetech_get_modes,
};

static const struct orisetech_panel_desc radxa_display_10fhd_ad003_desc = {
	.mode = {
		.clock        = 160000,

		.hdisplay    = 1200,
		.hsync_start    = 1200 + 80,
		.hsync_end    = 1200 + 80 + 60,
		.htotal        = 1200 + 80 + 60 + 4,

		.vdisplay    = 1920,
		.vsync_start    = 1920 + 35,
		.vsync_end    = 1920 + 35 + 25,
		.vtotal        = 1920 + 35 + 25 + 4,

		.width_mm    = 135,
		.height_mm    = 217,
		.type        = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
};

static int orisetech_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct orisetech_panel_desc *desc;
	struct orisetech *orisetech;
	int ret;

	orisetech = devm_kzalloc(&dsi->dev, sizeof(*orisetech), GFP_KERNEL);
	if (!orisetech)
		return -ENOMEM;

	desc = of_device_get_match_data(dev);
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_EOT_PACKET;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	orisetech->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(orisetech->reset)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get our reset GPIO\n");
		return PTR_ERR(orisetech->reset);
	}

	orisetech->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(orisetech->vdd)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get vdd regulator\n");
		return PTR_ERR(orisetech->vdd);
	}

	orisetech->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(orisetech->vccio)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get vccio regulator\n");
		return PTR_ERR(orisetech->vccio);
	}

	drm_panel_init(&orisetech->panel, dev, &orisetech_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&orisetech->panel);
	if (ret)
		return ret;

	drm_panel_add(&orisetech->panel);

	mipi_dsi_set_drvdata(dsi, orisetech);
	orisetech->dsi = dsi;
	orisetech->desc = desc;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&orisetech->panel);

	return ret;
}

static int orisetech_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct orisetech *orisetech = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&orisetech->panel);

	return 0;
}

static const struct of_device_id orisetech_of_match[] = {
	{ .compatible = "radxa,display-10fhd-ad003", .data = &radxa_display_10fhd_ad003_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, orisetech_of_match);

static struct mipi_dsi_driver orisetech_driver = {
	.probe = orisetech_dsi_probe,
	.remove = orisetech_dsi_remove,
	.driver = {
		.name = "orisetech-ota7290b",
		.of_match_table = orisetech_of_match,
	},
};
module_mipi_dsi_driver(orisetech_driver);

MODULE_AUTHOR("Haaland Chen <haaland@milkv.io>");
MODULE_DESCRIPTION("Orise Tech OTA7290B TFT-LCD panel");
MODULE_LICENSE("GPL");
