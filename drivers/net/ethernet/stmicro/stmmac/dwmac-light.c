// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "stmmac_platform.h"

/* clock registers */
#define GMAC_CLK_CFG0	0x00
#define GMAC_CLK_CFG1	0x04
#define GMAC_CLK_CFG2	0x08
#define GMAC_CLK_CFG3	0x0C
#define GMAC_CLK_CFG4	0x10
#define GMAC_CLK_CFG5	0x14
#define GMAC_CLK_CFG6	0x18

/* phy interface */
#define DWMAC_PHYIF_MII_GMII	0
#define DWMAC_PHYIF_RGMII	1
#define DWMAC_PHYIF_RMII	4
/* register bit fields, bit[3]: reserved, bit[2:0]: phy interface */
#define DWMAC_PHYIF_MASK	0x7
#define DWMAC_PHYIF_BIT_WIDTH	4

/* TXCLK direction, 1:input, 0:output */
#define TXCLK_DIR_OUTPUT	0
#define TXCLK_DIR_INPUT		1

#define GMAC_CLK_PLLOUT_250M	250000000
#define GMAC_GMII_RGMII_RATE	125000000
#define GMAC_MII_RATE		25000000
/* clock divider for speed */
#define GMAC_CLKDIV_125M	(GMAC_CLK_PLLOUT_250M / GMAC_GMII_RGMII_RATE)
#define GMAC_CLKDIV_25M		(GMAC_CLK_PLLOUT_250M / GMAC_MII_RATE)
#define GMAC_PTP_CLK_RATE	50000000 //50MHz

struct thead_dwmac_ops {
	void (*set_clk_source)(struct plat_stmmacenet_data *plat_dat);
	void (*set_clk_pll)(struct plat_stmmacenet_data *plat_dat);
	void (*set_clk_div)(struct plat_stmmacenet_data *plat_dat, unsigned int speed);
	void (*enable_clk)(struct plat_stmmacenet_data *plat_dat);
	void (*set_ptp_div)(struct plat_stmmacenet_data *plat_dat,unsigned int ptp_clk_rate);
};

struct thead_dwmac_priv_data {
	int id;
	struct device *dev;
	void __iomem *phy_if_reg;
	void __iomem *txclk_dir_reg;
	void __iomem *gmac_clk_reg;
	phy_interface_t interface;
	struct clk *gmac_pll_clk;
	unsigned long gmac_pll_clk_freq;
	struct clk *gmac_axi_aclk;
	struct clk *gmac_axi_pclk;
	const struct thead_dwmac_ops *ops;
	struct plat_stmmacenet_data *plat_dat;
};

#define  pm_debug dev_dbg	// for suspend/resume interface debug info show,replace to dev_info

/* set GMAC PHY interface, 0:MII/GMII, 1:RGMII, 4:RMII */
static void thead_dwmac_set_phy_if(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	void __iomem *phy_if_reg = thead_plat_dat->phy_if_reg;
	phy_interface_t interface = thead_plat_dat->interface;
	struct device *dev = thead_plat_dat->dev;
	//int devid = thead_plat_dat->id;
	unsigned int phyif = PHY_INTERFACE_MODE_MII;
	uint32_t reg;

	if (phy_if_reg == NULL)
		return;

	switch (interface)
	{
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		phyif = DWMAC_PHYIF_MII_GMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
		phyif = DWMAC_PHYIF_RGMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		phyif = DWMAC_PHYIF_RMII;
		break;
	default:
		dev_err(dev, "phy interface %d not supported\n", interface);
		return;
	};

	reg = readl(phy_if_reg);
	//This reg defined bit not related to devid
	reg &= ~(DWMAC_PHYIF_MASK );
	reg |= (phyif & DWMAC_PHYIF_MASK) ;
	dev_info(dev,"set phy_if_reg val 0x%x \n",reg);
	writel(reg, phy_if_reg);
}

/*
 * set GMAC TXCLK direction
 *     MII        : TXCLK is input
 *     GMII/RGMII : TXCLK is output
 */
static void thead_dwmac_set_txclk_dir(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	void __iomem *txclk_dir_reg = thead_plat_dat->txclk_dir_reg;
	phy_interface_t interface = thead_plat_dat->interface;
	struct device *dev = thead_plat_dat->dev;
	unsigned int txclk_dir = TXCLK_DIR_INPUT;

	if (txclk_dir_reg == NULL)
		return;

	switch (interface)
	{
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_RMII:
		txclk_dir = TXCLK_DIR_INPUT;
		break;
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
		txclk_dir = TXCLK_DIR_OUTPUT;
		break;
	default:
		dev_err(dev, "phy interface %d not supported\n", interface);
		return;
	};

	writel(txclk_dir, txclk_dir_reg);
}

static void thead_dwmac_ice_set_clk_source(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	phy_interface_t interface = thead_plat_dat->interface;
	struct device *dev = thead_plat_dat->dev;
	uint32_t reg;

	if (gmac_clk_reg == NULL)
		return;

	reg = readl(gmac_clk_reg + GMAC_CLK_CFG0);

	/* RX clock source */
	reg |= BIT(7);  /* gmac_rx_clk_sel: extern pin */

	/* TX clock source */
	if (interface == PHY_INTERFACE_MODE_MII) {
		reg |= BIT(1);  /* gmac_tx_clk_sel: extern pin */
		reg &= ~BIT(2); /* gmac_tx_clk_gbit_sel: u_tx_clk_mux */
	} else if (interface == PHY_INTERFACE_MODE_GMII) {
		reg &= ~BIT(5); /* gmac_tx_clk_out_sel: GMAC PLL */
		reg |= BIT(2);  /* gmac_tx_clk_gbit_sel: GMAC PLL */
	} else if (interface == PHY_INTERFACE_MODE_RGMII
		|| interface == PHY_INTERFACE_MODE_RGMII_ID
		|| interface == PHY_INTERFACE_MODE_RGMII_RXID
		|| interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg &= ~BIT(5); /* gmac_tx_clk_out_sel: GMAC PLL */
		reg |= BIT(2);  /* gmac_tx_clk_gbit_sel: GMAC PLL */
	} else {
		dev_err(dev, "phy interface %d not supported\n", interface);
		return;
	}

	writel(reg, gmac_clk_reg + GMAC_CLK_CFG0);
}


/* set clock source */
static void thead_dwmac_set_clock_delay(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	struct device *dev = thead_plat_dat->dev;
	unsigned int delay;

	if (gmac_clk_reg == NULL)
		return;

	if (of_property_read_u32(dev->of_node, "rx-clk-delay",
				 &delay) == 0) {
		/* RX clk delay */
		writel(delay, gmac_clk_reg + GMAC_CLK_CFG1);
		pr_info("RX clk delay: 0x%X\n", delay);
	}

	if (of_property_read_u32(dev->of_node, "tx-clk-delay",
				 &delay) == 0) {
		/* TX clk delay */
		writel(delay, gmac_clk_reg + GMAC_CLK_CFG2);
		pr_info("TX clk delay: 0x%X\n", delay);
	}
}

/* set gmac pll divider (u_pll_clk_div) to get 250MHz clock */
static void thead_dwmac_ice_set_pll(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	phy_interface_t interface = thead_plat_dat->interface;
	unsigned int src_freq = thead_plat_dat->gmac_pll_clk_freq;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	unsigned int reg;
	unsigned int div;

	if (gmac_clk_reg == NULL)
		return;

	if (interface == PHY_INTERFACE_MODE_MII) {
		/* for mii, no internal pll is used */
		return;
	} else if (interface == PHY_INTERFACE_MODE_GMII
		|| interface == PHY_INTERFACE_MODE_RGMII
		|| interface == PHY_INTERFACE_MODE_RGMII_ID
		|| interface == PHY_INTERFACE_MODE_RGMII_RXID
		|| interface == PHY_INTERFACE_MODE_RGMII_TXID) {

		/* check clock */
		if ((src_freq == 0) || (src_freq % GMAC_CLK_PLLOUT_250M != 0)) {
			pr_err("error! invalid gmac pll freq %d\n", src_freq);
			return;
		}
		div = src_freq / GMAC_CLK_PLLOUT_250M;

		/* disable pll_clk_div */
		reg = readl(gmac_clk_reg + GMAC_CLK_CFG3);
		reg &= ~BIT(31);
		writel(reg, gmac_clk_reg + GMAC_CLK_CFG3);

		/* modify divider */
		writel(div, gmac_clk_reg + GMAC_CLK_CFG3);

		/* enable pll_clk_div */
		reg = readl(gmac_clk_reg + GMAC_CLK_CFG3);
		reg |= BIT(31);
		writel(reg, gmac_clk_reg + GMAC_CLK_CFG3);
	} else {
		pr_err("phy interface %d not supported\n", interface);
		return;
	}
}

/* set gmac speed */
static void thead_dwmac_ice_set_clk_div(struct plat_stmmacenet_data *plat_dat, unsigned int speed)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	phy_interface_t interface = thead_plat_dat->interface;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	unsigned int reg;

	if (gmac_clk_reg == NULL)
		return;

	if (interface == PHY_INTERFACE_MODE_MII) {
		/* For MII, no internal PLL is used */
		return;
	} else if (interface == PHY_INTERFACE_MODE_GMII
		|| interface == PHY_INTERFACE_MODE_RGMII
		|| interface == PHY_INTERFACE_MODE_RGMII_ID
		|| interface == PHY_INTERFACE_MODE_RGMII_RXID
		|| interface == PHY_INTERFACE_MODE_RGMII_TXID) {

		/* disable gtx_clk_div */
		reg = readl(gmac_clk_reg + GMAC_CLK_CFG4);
		reg &= ~BIT(31);
		writel(reg, gmac_clk_reg + GMAC_CLK_CFG4);

		/*
		 * modify divider
		 */
		/* gtx_clk_div */
		if (speed == SPEED_1000) {
			writel(GMAC_CLKDIV_125M, gmac_clk_reg + GMAC_CLK_CFG4);
		} else if (speed == SPEED_100) {
			writel(GMAC_CLKDIV_25M, gmac_clk_reg + GMAC_CLK_CFG4);
		} else {
			writel(GMAC_CLKDIV_25M / 10, gmac_clk_reg + GMAC_CLK_CFG4);
		}

		/* enable gtx_clk_div */
		reg = readl(gmac_clk_reg + GMAC_CLK_CFG4);
		reg |= BIT(31);
		writel(reg, gmac_clk_reg + GMAC_CLK_CFG4);
	} else {
		pr_err("phy interface %d not supported\n", interface);
		return;
	}
}

static void thead_dwmac_light_set_clk_div(struct plat_stmmacenet_data *plat_dat, unsigned int speed)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	phy_interface_t interface = thead_plat_dat->interface;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	unsigned long src_freq = thead_plat_dat->gmac_pll_clk_freq;
	unsigned int div;
	unsigned int reg;

	if (gmac_clk_reg == NULL)
		return;

	if (interface == PHY_INTERFACE_MODE_MII) {
		/* For MII, no internal PLL is used */
		return;
	} else if (interface == PHY_INTERFACE_MODE_GMII
		|| interface == PHY_INTERFACE_MODE_RGMII
		|| interface == PHY_INTERFACE_MODE_RGMII_ID
		|| interface == PHY_INTERFACE_MODE_RGMII_RXID
		|| interface == PHY_INTERFACE_MODE_RGMII_TXID) {

		/* check clock */
		if ((src_freq == 0) || (src_freq % GMAC_GMII_RGMII_RATE != 0 ||
		    src_freq % GMAC_MII_RATE != 0)) {
			pr_err("error! invalid gmac pll freq %lu\n", src_freq);
			return;
		}

		/* disable gtx_clk_div */
		reg = readl(gmac_clk_reg + GMAC_CLK_CFG3);
		reg &= ~BIT(31);
		writel(reg, gmac_clk_reg + GMAC_CLK_CFG3);

		/*
		 * modify divider
		 */
		/* gtx_clk_div */
		if (speed == SPEED_1000)
			div = src_freq / GMAC_GMII_RGMII_RATE;
		else if (speed == SPEED_100)
			div = src_freq / GMAC_MII_RATE;
		else
			div = (src_freq * 10) / GMAC_MII_RATE;
		writel(div, gmac_clk_reg + GMAC_CLK_CFG3);

		/* enable gtx_clk_div */
		reg = div | BIT(31);
		writel(reg, gmac_clk_reg + GMAC_CLK_CFG3);
	} else {
		pr_err("phy interface %d not supported\n", interface);
		return;
	}
}

static void thead_dwmac_light_set_ptp_clk_div(struct plat_stmmacenet_data *plat_dat,unsigned int ptp_clk_rate)
{
	unsigned int div;
	unsigned int reg;
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	unsigned long src_freq = thead_plat_dat->gmac_pll_clk_freq;

	if (gmac_clk_reg == NULL)
		return;
	if(!ptp_clk_rate || !src_freq)
	{
		pr_warn("invalid gmac pll freq %lu or ptp_clk_rate %d\n", src_freq,ptp_clk_rate);
		return;
	}
	/* disable clk_div */
	reg = readl(gmac_clk_reg + GMAC_CLK_CFG5);
	reg &= ~BIT(31);
	writel(reg, gmac_clk_reg + GMAC_CLK_CFG5);

	div = src_freq / ptp_clk_rate;
	writel(div,gmac_clk_reg + GMAC_CLK_CFG5);

	/* enable clk_div */
	reg = div | BIT(31);
	writel(reg, gmac_clk_reg + GMAC_CLK_CFG5);
	return ;
}

/* enable gmac clock */
static void thead_dwmac_ice_enable_clk(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	phy_interface_t interface = thead_plat_dat->interface;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	struct device *dev = thead_plat_dat->dev;
	unsigned int reg;

	if (gmac_clk_reg == NULL)
		return;

	reg = readl(gmac_clk_reg + GMAC_CLK_CFG0);

	/* enable gmac_hclk */
	reg |= BIT(14);

	if (interface == PHY_INTERFACE_MODE_MII) {
		reg |= BIT(8);  /* enable gmac_rx_clk */
		reg |= BIT(3);  /* enable gmac_tx_clk */
	} else if (interface == PHY_INTERFACE_MODE_GMII) {
		reg |= BIT(8);  /* enable gmac_rx_clk */
		reg |= BIT(3);  /* enable gmac_tx_clk */
		reg |= BIT(6);  /* enable gmac_tx_clk_out */
	} else if (interface == PHY_INTERFACE_MODE_RGMII
		|| interface == PHY_INTERFACE_MODE_RGMII_ID
		|| interface == PHY_INTERFACE_MODE_RGMII_RXID
		|| interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg |= BIT(8);  /* enable gmac_rx_clk */
		reg |= BIT(3);  /* enable gmac_tx_clk */
		reg |= BIT(6);  /* enable gmac_tx_clk_out */
		reg |= BIT(9);  /* enable gmac_rx_clk_n */
		reg |= BIT(4);  /* enable gmac_tx_clk_n */
	} else {
		dev_err(dev, "phy interface %d not supported\n", interface);
		return;
	}

	writel(reg, gmac_clk_reg + GMAC_CLK_CFG0);
}

static void thead_dwmac_light_enable_clk(struct plat_stmmacenet_data *plat_dat)
{
	struct thead_dwmac_priv_data *thead_plat_dat = plat_dat->bsp_priv;
	phy_interface_t interface = thead_plat_dat->interface;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	struct device *dev = thead_plat_dat->dev;
	unsigned int reg = 0;

	if (gmac_clk_reg == NULL)
		return;

	/* use internal pll */
	writel(BIT(0), gmac_clk_reg + GMAC_CLK_CFG6);

	if (interface == PHY_INTERFACE_MODE_MII) {
		reg |= BIT(4);  /* enable gmac_rx_clk */
		reg |= BIT(1);  /* enable gmac_tx_clk */
	} else if (interface == PHY_INTERFACE_MODE_GMII) {
		reg |= BIT(4);  /* enable gmac_rx_clk */
		reg |= BIT(1);  /* enable gmac_tx_clk */
		reg |= BIT(4);  /* enable gmac_tx_clk_out */
	} else if (interface == PHY_INTERFACE_MODE_RGMII
		|| interface == PHY_INTERFACE_MODE_RGMII_ID
		|| interface == PHY_INTERFACE_MODE_RGMII_RXID
		|| interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg |= BIT(4);  /* enable gmac_rx_clk */
		reg |= BIT(1);  /* enable gmac_tx_clk */
		reg |= BIT(3);  /* enable gmac_tx_clk_out */
		reg |= BIT(5);  /* enable gmac_rx_clk_n */
		reg |= BIT(2);  /* enable gmac_tx_clk_n */
	} else {
		dev_err(dev, "phy interface %d not supported\n", interface);
		return;
	}

	reg |= BIT(6); /* ephy ref clk */
	writel(reg, gmac_clk_reg + GMAC_CLK_CFG0);
}
static int thead_dwmac_init(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac_priv_data *thead_plat_dat = bsp_priv;
	//struct plat_stmmacenet_data *plat_dat = thead_plat_dat->plat_dat;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void __iomem *ptr;
	//struct clk *clktmp;
	//int ret;

	thead_plat_dat->id = of_alias_get_id(np, "ethernet");
	if (thead_plat_dat->id < 0) {
		thead_plat_dat->id = 0;
	}
	dev_info(dev, "id: %d\n", thead_plat_dat->id);

	thead_plat_dat->interface = device_get_phy_mode(&pdev->dev);
	if (thead_plat_dat->interface < 0)
		return -ENODEV;
	dev_info(dev, "phy interface: %d\n", thead_plat_dat->interface);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_if_reg");
	if ((res != NULL) && (resource_type(res) == IORESOURCE_MEM)) {
		ptr = devm_ioremap(dev, res->start, resource_size(res));
		if (!ptr) {
			dev_err(dev, "phy interface register not exist, skipped it\n");
		} else {
			thead_plat_dat->phy_if_reg = ptr;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "txclk_dir_reg");
	ptr = devm_ioremap_resource(dev, res);
	if (IS_ERR(ptr)) {
		dev_err(dev, "txclk_dir register not exist, skipped it\n");
	} else {
		thead_plat_dat->txclk_dir_reg = ptr;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "clk_mgr_reg");
	ptr = devm_ioremap_resource(dev, res);
	if (IS_ERR(ptr)) {
		dev_err(dev, "gmac_clk register not exist, skipped it\n");
	} else {
		thead_plat_dat->gmac_clk_reg = ptr;
	}
#if 0
	/* get gmac pll clk */
	clktmp = devm_clk_get(dev, "gmac_pll_clk");
	if (IS_ERR(clktmp)) {
		dev_err(dev, "gmac_pll_clk not exist, skipped it\n");
	} else {
		thead_plat_dat->gmac_pll_clk = clktmp;

		ret = clk_prepare_enable(thead_plat_dat->gmac_pll_clk);
		if (ret) {
			dev_err(dev, "Failed to enable clk 'gmac_pll_clk'\n");
			return -1;
		}

		thead_plat_dat->gmac_pll_clk_freq =
				clk_get_rate(thead_plat_dat->gmac_pll_clk);
	}


	thead_dwmac_set_phy_if(plat_dat);
	thead_dwmac_set_txclk_dir(plat_dat);

	if (thead_plat_dat->ops->set_clk_source)
		thead_plat_dat->ops->set_clk_source(plat_dat);

	thead_dwmac_set_clock_delay(plat_dat);

	if (thead_plat_dat->ops->set_clk_pll)
		thead_plat_dat->ops->set_clk_pll(plat_dat);

	if (thead_plat_dat->ops->set_clk_div)
		thead_plat_dat->ops->set_clk_div(plat_dat, SPEED_1000);

	if (thead_plat_dat->ops->enable_clk)
		thead_plat_dat->ops->enable_clk(plat_dat);
#endif
	return 0;
}

static void thead_dwmac_fix_speed(void *bsp_priv, unsigned int speed)
{
	struct thead_dwmac_priv_data *thead_plat_dat = bsp_priv;
	struct plat_stmmacenet_data *plat_dat = thead_plat_dat->plat_dat;

	if (thead_plat_dat->ops->set_clk_div)
		thead_plat_dat->ops->set_clk_div(plat_dat, speed);
}

/**
 * dwmac1000_validate_mcast_bins - validates the number of Multicast filter bins
 * @mcast_bins: Multicast filtering bins
 * Description:
 * this function validates the number of Multicast filtering bins specified
 * by the configuration through the device tree. The Synopsys GMAC supports
 * 64 bins, 128 bins, or 256 bins. "bins" refer to the division of CRC
 * number space. 64 bins correspond to 6 bits of the CRC, 128 corresponds
 * to 7 bits, and 256 refers to 8 bits of the CRC. Any other setting is
 * invalid and will cause the filtering algorithm to use Multicast
 * promiscuous mode.
 */
static int dwmac1000_validate_mcast_bins(int mcast_bins)
{
	int x = mcast_bins;

	switch (x) {
	case HASH_TABLE_SIZE:
	case 128:
	case 256:
		break;
	default:
		x = 0;
		pr_info("Hash table entries set to unexpected value %d",
			mcast_bins);
		break;
	}
	return x;
}

/**
 * dwmac1000_validate_ucast_entries - validate the Unicast address entries
 * @ucast_entries: number of Unicast address entries
 * Description:
 * This function validates the number of Unicast address entries supported
 * by a particular Synopsys 10/100/1000 controller. The Synopsys controller
 * supports 1..32, 64, or 128 Unicast filter entries for it's Unicast filter
 * logic. This function validates a valid, supported configuration is
 * selected, and defaults to 1 Unicast address if an unsupported
 * configuration is selected.
 */
static int dwmac1000_validate_ucast_entries(int ucast_entries)
{
	int x = ucast_entries;

	switch (x) {
	case 1 ... 32:
	case 64:
	case 128:
		break;
	default:
		x = 1;
		pr_info("Unicast table entries set to unexpected value %d\n",
			ucast_entries);
		break;
	}
	return x;
}
static void __maybe_unused thead_dwmac_dump_priv_reg(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac_priv_data *thead_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	void __iomem *gmac_clk_reg = thead_plat_dat->gmac_clk_reg;
	unsigned int reg = 0;
	int i;
	dev_info(dev,"dump gmac_clk_reg %p\n",gmac_clk_reg);
	if(gmac_clk_reg == NULL)
		return ;
	for(i=0; i< 0x1c; i+=4)
	{
		reg = readl(gmac_clk_reg + GMAC_CLK_CFG0+i);
		pr_info("%08x ",reg);
	}
	pr_info("\n");
	reg = readl(thead_plat_dat->phy_if_reg);
	pr_info("phy_if_reg %08x ",reg);
	reg = readl(thead_plat_dat->txclk_dir_reg);
	pr_info("txclk_dir_reg %08x ",reg);
}

int thead_dwmac_clk_enable(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac_priv_data *thead_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	ret = clk_prepare_enable(thead_plat_dat->gmac_pll_clk);
	if (ret) {
		dev_err(dev, "Failed to enable clk 'gmac_pll_clk'\n");
		return -1;
	}
	ret = clk_prepare_enable(thead_plat_dat->gmac_axi_aclk);
	if (ret) {
		clk_disable_unprepare(thead_plat_dat->gmac_pll_clk);
		dev_err(dev, "Failed to enable clk 'gmac_axi_aclk'\n");
		return -1;
	}
	ret = clk_prepare_enable(thead_plat_dat->gmac_axi_pclk);
	if (ret) {
		clk_disable_unprepare(thead_plat_dat->gmac_axi_aclk);
		clk_disable_unprepare(thead_plat_dat->gmac_pll_clk);
		dev_err(dev, "Failed to enable clk 'gmac_axi_pclk'\n");
		return -1;
	}
	
	return ret;
}

int thead_dwmac_clk_init(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac_priv_data *thead_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	struct plat_stmmacenet_data *plat_dat = thead_plat_dat->plat_dat;
	int ret = 0;
	pm_debug(dev,"enter %s()\n",__func__);

	thead_dwmac_set_phy_if(plat_dat);
	thead_dwmac_set_txclk_dir(plat_dat);

	if (thead_plat_dat->ops->set_clk_source)
		thead_plat_dat->ops->set_clk_source(plat_dat);

	thead_dwmac_set_clock_delay(plat_dat);

	if (thead_plat_dat->ops->set_clk_pll)
		thead_plat_dat->ops->set_clk_pll(plat_dat);

	if (thead_plat_dat->ops->set_clk_div)
		thead_plat_dat->ops->set_clk_div(plat_dat, SPEED_1000);

	if (thead_plat_dat->ops->enable_clk)
		thead_plat_dat->ops->enable_clk(plat_dat);
	
	if (thead_plat_dat->ops->set_ptp_div)
		thead_plat_dat->ops->set_ptp_div(plat_dat,plat_dat->clk_ptp_rate);
	//thead_dwmac_dump_priv_reg(pdev,bsp_priv);
	return ret;
}
void thead_dwmac_clk_disable(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac_priv_data *thead_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	pm_debug(dev,"enter %s()\n",__func__);
	//thead_dwmac_dump_priv_reg(pdev,bsp_priv);
	
	clk_disable_unprepare(thead_plat_dat->gmac_pll_clk);
	clk_disable_unprepare(thead_plat_dat->gmac_axi_aclk);
	clk_disable_unprepare(thead_plat_dat->gmac_pll_clk);

	return ;
}

static int thead_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct thead_dwmac_priv_data *thead_plat_dat;
	struct device *dev = &pdev->dev;
	const struct thead_dwmac_ops *data;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	thead_plat_dat = devm_kzalloc(dev, sizeof(*thead_plat_dat), GFP_KERNEL);
	if (thead_plat_dat == NULL) {
		dev_err(&pdev->dev, "allocate memory failed\n");
		return -ENOMEM;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	if (pdev->dev.of_node) {
		plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
		if (IS_ERR(plat_dat)) {
			dev_err(&pdev->dev, "dt configuration failed\n");
			return PTR_ERR(plat_dat);
		}

		data = of_device_get_match_data(&pdev->dev);
		if (!data) {
			dev_err(&pdev->dev, "failed to get match data\n");
			ret = -EINVAL;
			return ret;
		}

		thead_plat_dat->ops = data;
	} else {
		plat_dat = dev_get_platdata(&pdev->dev);
		if (!plat_dat) {
			dev_err(&pdev->dev, "no platform data provided\n");
			return  -EINVAL;
		}

		/* Set default value for multicast hash bins */
		plat_dat->multicast_filter_bins = HASH_TABLE_SIZE;

		/* Set default value for unicast filter entries */
		plat_dat->unicast_filter_entries = 1;
	}



	/* populate bsp private data */
	thead_plat_dat->dev = &pdev->dev;
	plat_dat->bsp_priv = thead_plat_dat;
	plat_dat->fix_mac_speed = thead_dwmac_fix_speed;
	plat_dat->init = thead_dwmac_clk_init;
	of_property_read_u32(np, "max-frame-size", &plat_dat->maxmtu);
	of_property_read_u32(np, "snps,multicast-filter-bins",
			     &plat_dat->multicast_filter_bins);
	of_property_read_u32(np, "snps,perfect-filter-entries",
			     &plat_dat->unicast_filter_entries);
	plat_dat->unicast_filter_entries = dwmac1000_validate_ucast_entries(
				       plat_dat->unicast_filter_entries);
	plat_dat->multicast_filter_bins = dwmac1000_validate_mcast_bins(
				      plat_dat->multicast_filter_bins);
	plat_dat->has_gmac = 1;
	plat_dat->pmt = 1;
	thead_plat_dat->plat_dat = plat_dat;

	/* get gmac pll clk */
	thead_plat_dat->gmac_pll_clk = devm_clk_get(dev, "gmac_pll_clk");
	if (IS_ERR(thead_plat_dat->gmac_pll_clk)) {
		dev_err(dev, "gmac_pll_clk not exist, dts error\n");
		goto err_remove_config_dt;
	}
	
	thead_plat_dat->gmac_axi_aclk = devm_clk_get(dev, "axi_aclk");
	if (IS_ERR(thead_plat_dat->gmac_axi_aclk)) {
		dev_err(dev, "gmac axi_aclk not exist, skipped it\n");
	}
	thead_plat_dat->gmac_axi_pclk = devm_clk_get(dev, "axi_pclk");
	if (IS_ERR(thead_plat_dat->gmac_axi_pclk)) {
		dev_err(dev, "gmac axi_pclk not exist, skipped it\n");
	}
	

	thead_plat_dat->gmac_pll_clk_freq =
			clk_get_rate(thead_plat_dat->gmac_pll_clk);
	dev_info(dev,"get_rate gmac_pll_clk_freq %ld \n",thead_plat_dat->gmac_pll_clk_freq);

	ret = thead_dwmac_init(pdev, plat_dat->bsp_priv);
	if (ret)
		goto err_remove_config_dt;
	
	plat_dat->clk_ptp_rate = GMAC_PTP_CLK_RATE;

	ret = thead_dwmac_clk_enable(pdev, plat_dat->bsp_priv);
	if (ret)
			goto err_remove_config_dt;
	
	/* Custom initialisation (if needed) -- init clks*/
	if (plat_dat->init) {
		ret = plat_dat->init(pdev, plat_dat->bsp_priv);
		if (ret)
			goto err_exit;
	}

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_exit;

	return 0;

err_exit:
	if (plat_dat->exit)
		plat_dat->exit(pdev, plat_dat->bsp_priv);
	thead_dwmac_clk_disable(pdev, plat_dat->bsp_priv);
err_remove_config_dt:
	if (pdev->dev.of_node)
		stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

/**
 * stmmac_pltfr_suspend
 * @dev: device pointer
 * Description: this function is invoked when suspend the driver and it direcly
 * call the main suspend function and then, if required, on some platform, it
 * can call an exit helper.
 */
static int __maybe_unused thead_dwmac_suspend(struct device *dev)
{
	int ret;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev,"enter %s()\n",__func__);
	ret = stmmac_suspend(dev);
	if (priv->plat->exit)
		priv->plat->exit(pdev, priv->plat->bsp_priv);
	
	return ret;
}

/**
 * thead_dwmac_resume
 * @dev: device pointer
 * Description: this function is invoked when resume the driver before calling
 * the main resume function, on some platforms, it can call own init helper
 * if required.
 */
static int __maybe_unused thead_dwmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev,"enter %s()\n",__func__);

	if (priv->plat->init)
		priv->plat->init(pdev, priv->plat->bsp_priv);

	return stmmac_resume(dev);
}

static int __maybe_unused thead_dwmac_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev,"enter %s()\n",__func__);
	stmmac_bus_clks_config(priv, false);
	thead_dwmac_clk_disable(pdev, priv->plat->bsp_priv);
	return 0;
}

static int __maybe_unused thead_dwmac_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	ret = stmmac_bus_clks_config(priv, true);
	if(ret)
		return ret;
	ret = thead_dwmac_clk_enable(pdev, priv->plat->bsp_priv);
	if(ret)
		return ret;
	return 0;
}

static int __maybe_unused thead_dwmac_noirq_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* Disable clock in case of PWM is off */
		clk_disable_unprepare(priv->plat->clk_ptp_ref);

		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
	}

	return 0;
}

static int __maybe_unused thead_dwmac_noirq_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* enable the clk previously disabled */
		ret = pm_runtime_force_resume(dev);
		if (ret)
			return ret;

		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0) {
			netdev_warn(priv->dev,
				    "failed to enable PTP reference clock: %pe\n",
				    ERR_PTR(ret));
			return ret;
		}
	}

	return 0;
}

/*similar with stmmac_pltfr_pm_ops,but clks enable/disable add this drv need */
const struct dev_pm_ops thead_dwmac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(thead_dwmac_suspend, thead_dwmac_resume)
	SET_RUNTIME_PM_OPS(thead_dwmac_runtime_suspend, thead_dwmac_runtime_resume, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(thead_dwmac_noirq_suspend, thead_dwmac_noirq_resume)
};


static struct thead_dwmac_ops thead_ice_dwmac_data = {
	.set_clk_source = thead_dwmac_ice_set_clk_source,
	.set_clk_pll = thead_dwmac_ice_set_pll,
	.set_clk_div = thead_dwmac_ice_set_clk_div,
	.enable_clk = thead_dwmac_ice_enable_clk,
};

static struct thead_dwmac_ops thead_light_dwmac_data = {
	.set_clk_div = thead_dwmac_light_set_clk_div,
	.enable_clk = thead_dwmac_light_enable_clk,
	.set_ptp_div = thead_dwmac_light_set_ptp_clk_div,
};

static const struct of_device_id thead_dwmac_match[] = {
	{ .compatible = "thead,ice-dwmac", .data = &thead_ice_dwmac_data },
	{ .compatible = "thead,light-dwmac", .data = &thead_light_dwmac_data },
	{ }
};
MODULE_DEVICE_TABLE(of, thead_dwmac_match);

static struct platform_driver thead_dwmac_driver = {
	.probe  = thead_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "light_dwmac_eth",
		.pm		= &thead_dwmac_pm_ops,
		.of_match_table = of_match_ptr(thead_dwmac_match),
	},
};
module_platform_driver(thead_dwmac_driver);

MODULE_AUTHOR("THEAD");
MODULE_DESCRIPTION("T-HEAD dwmac driver");
MODULE_LICENSE("GPL v2");
