// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Inspired by dwc3-of-simple.c
 */

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/of_clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/extcon.h>
#include <linux/of_platform.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/usb/of.h>

#include "core.h"

/* USB3_DRD registers */
#define USB_CLK_GATE_STS		0x0
#define USB_LOGIC_ANALYZER_TRACE_STS0	0x4
#define USB_LOGIC_ANALYZER_TRACE_STS1	0x8
#define USB_GPIO				0xc
#define USB_DEBUG_STS0			0x10
#define USB_DEBUG_STS1			0x14
#define USB_DEBUG_STS2			0x18
#define USBCTL_CLK_CTRL0		0x1c
#define USBPHY_CLK_CTRL1		0x20
#define USBPHY_TEST_CTRL0		0x24
#define USBPHY_TEST_CTRL1		0x28
#define USBPHY_TEST_CTRL2		0x2c
#define USBPHY_TEST_CTRL3		0x30
#define USB_SSP_EN				0x34
#define USB_HADDR_SEL			0x38
#define USB_SYS					0x3c
#define USB_HOST_STATUS			0x40
#define USB_HOST_CTRL			0x44
#define USBPHY_HOST_CTRL		0x48
#define USBPHY_HOST_STATUS		0x4c
#define USB_TEST_REG0			0x50
#define USB_TEST_REG1			0x54
#define USB_TEST_REG2			0x58
#define USB_TEST_REG3			0x5c

/* Bit fields */
/* USB_SYS */
#define TEST_POWERDOWN_SSP	BIT(2)
#define TEST_POWERDOWN_HSP	BIT(1)
#define COMMONONN			BIT(0)

/* USB_SSP_EN */
#define REF_SSP_EN			BIT(0)

/* USBPHY_HOST_CTRL */
#define HOST_U2_PORT_DISABLE	BIT(6)
#define HOST_U3_PORT_DISABLE	BIT(5)

/* MISC_SYSREG registers */
#define USB3_DRD_SWRST			0x14

/* Bit fields */
/* USB3_DRD_SWRST */
#define USB3_DRD_VCCRST		BIT(2)
#define USB3_DRD_PHYRST		BIT(1)
#define USB3_DRD_PRST		BIT(0)
#define USB3_DRD_MASK		GENMASK(2, 0)

/* USB as host or device*/
#define USB_AS_HOST         (true)
#define USB_AS_DEVICE       (false)

static bool usb_role = USB_AS_HOST;
module_param(usb_role, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(usb_role, "USB role");

struct dwc3_thead {
	struct device		*dev;
	struct clk_bulk_data	*clks;
	int			num_clocks;

	struct regmap		*usb3_drd;
	struct regmap		*misc_sysreg;

	struct gpio_desc	*hubswitch;
	struct regulator	*hub1v2;
	struct regulator	*hub5v;
	struct regulator	*vbus;
};

static void dwc3_thead_deassert(struct dwc3_thead *thead)
{
	/* 1. reset assert */
	regmap_update_bits(thead->misc_sysreg, USB3_DRD_SWRST,
				USB3_DRD_MASK, USB3_DRD_PRST);

	/* 
	 *	2. Common Block Power-Down Control.
	 *	Controls the power-down signals in the PLL block
	 *	when the USB 3.0 femtoPHY is in Suspend or Sleep mode.
	 */
	regmap_update_bits(thead->usb3_drd, USB_SYS,
				COMMONONN, COMMONONN);

	/*
	 *	3. Reference Clock Enable for SS function.
	 *	Enables the reference clock to the prescaler. 
	 *	The ref_ssp_en signal must remain de-asserted until
	 *	the reference clock is running at the appropriate frequency,
	 *	at which point ref_ssp_en can be asserted.
	 *	For lower power states, ref_ssp_en can also be de-asserted.
	 */
	regmap_update_bits(thead->usb3_drd, USB_SSP_EN,
				REF_SSP_EN, REF_SSP_EN);

	/* 4. set host ctrl */
	regmap_write(thead->usb3_drd, USB_HOST_CTRL, 0x1101);

	/* 5. reset deassert */
	regmap_update_bits(thead->misc_sysreg, USB3_DRD_SWRST,
				USB3_DRD_MASK, USB3_DRD_MASK);

	/* 6. wait deassert complete */
	udelay(10);
}

static void dwc3_thead_assert(struct dwc3_thead *thead)
{
	/* close ssp */
	regmap_update_bits(thead->usb3_drd, USB_SSP_EN,
				REF_SSP_EN, 0);

	/* reset assert usb */
	regmap_update_bits(thead->misc_sysreg, USB3_DRD_SWRST,
				USB3_DRD_MASK, 0);

}

static int dwc3_thead_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;
	struct dwc3_thead	*thead;
	int			ret;

	thead = devm_kzalloc(&pdev->dev, sizeof(*thead), GFP_KERNEL);
	if (!thead)
		return -ENOMEM;

	platform_set_drvdata(pdev, thead);
	thead->dev = &pdev->dev;

	thead->hubswitch = devm_gpiod_get(dev, "hubswitch", (usb_role == USB_AS_DEVICE) ? GPIOD_OUT_LOW : GPIOD_OUT_HIGH);
	if (IS_ERR(thead->hubswitch))
		dev_dbg(dev, "no need to get hubswitch GPIO\n");
	dev_info(dev, "hubswitch usb_role = %d\n", usb_role);

	thead->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(thead->vbus))
		dev_dbg(dev, "no need to get vbus\n");
	else {
		ret = regulator_enable(thead->vbus);

		if (ret) {
			dev_err(dev, "failed to enable regulator vbus %d\n", ret);
		}
	}

	thead->hub1v2 = devm_regulator_get(dev, "hub1v2");
	if (IS_ERR(thead->hub1v2))
		dev_dbg(dev, "no need to set hub1v2\n");
	else {
		ret = regulator_enable(thead->hub1v2);

		if (ret) {
			dev_err(dev, "failed to enable regulator hub1v2 %d\n", ret);
		}
	}

	thead->hub5v = devm_regulator_get(dev, "hub5v");
	if (IS_ERR(thead->hub5v))
		dev_dbg(dev, "no need to set hub5v\n");
	else {
		ret = regulator_enable(thead->hub5v);

		if (ret) {
			dev_err(dev, "failed to enable regulator hub1v2 %d\n", ret);
		}
	}

	thead->misc_sysreg = syscon_regmap_lookup_by_phandle(np, "usb3-misc-regmap");
	if (IS_ERR(thead->misc_sysreg))
		return PTR_ERR(thead->misc_sysreg);

	thead->usb3_drd = syscon_regmap_lookup_by_phandle(np, "usb3-drd-regmap");
	if (IS_ERR(thead->usb3_drd))
		return PTR_ERR(thead->usb3_drd);

	ret = clk_bulk_get_all(thead->dev, &thead->clks);
	if (ret < 0)
		goto err;

	thead->num_clocks = ret;

	ret = clk_bulk_prepare_enable(thead->num_clocks, thead->clks);
	if (ret)
		goto err;

	dwc3_thead_deassert(thead);

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to register dwc3 core - %d\n", ret);
		goto err_clk_put;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	dev_info(dev, "light dwc3 probe ok!\n");

	return 0;

err_clk_put:
	clk_bulk_disable_unprepare(thead->num_clocks, thead->clks);
	clk_bulk_put_all(thead->num_clocks, thead->clks);
err:
	return ret;
}

static int dwc3_thead_remove(struct platform_device *pdev)
{
	struct dwc3_thead	*thead = platform_get_drvdata(pdev);

	dwc3_thead_assert(thead);

	of_platform_depopulate(thead->dev);

	clk_bulk_disable_unprepare(thead->num_clocks, thead->clks);
	clk_bulk_put_all(thead->num_clocks, thead->clks);

	pm_runtime_disable(thead->dev);
	pm_runtime_set_suspended(thead->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwc3_thead_pm_suspend(struct device *dev)
{
	struct dwc3_thead *thead = dev_get_drvdata(dev);

	dwc3_thead_assert(thead);

	clk_bulk_disable(thead->num_clocks, thead->clks);

	return 0;
}


static int dwc3_thead_pm_resume(struct device *dev)
{
	struct dwc3_thead *thead = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(thead->num_clocks, thead->clks);
	if (ret) {
		dev_err(dev, "failed to enable clk ret=%d\n", ret);
		return ret;
	}

	dwc3_thead_deassert(thead);

	return ret;
}

static const struct dev_pm_ops dwc3_thead_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_thead_pm_suspend, dwc3_thead_pm_resume)
};
#define DEV_PM_OPS	(&dwc3_thead_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id dwc3_thead_of_match[] = {
	{ .compatible = "thead,dwc3" },
	{ },
};
MODULE_DEVICE_TABLE(of, dwc3_thead_of_match);

static struct platform_driver dwc3_thead_driver = {
	.probe		= dwc3_thead_probe,
	.remove		= dwc3_thead_remove,
	.driver		= {
		.name	= "dwc3-thead",
		.pm	= DEV_PM_OPS,
		.of_match_table	= dwc3_thead_of_match,
	},
};

module_platform_driver(dwc3_thead_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 Thead Glue Driver");
