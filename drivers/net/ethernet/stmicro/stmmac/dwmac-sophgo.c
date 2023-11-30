/*
 * DWMAC specific glue layer
 *
 * Copyright (c) 2018 Bitmain Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/stmmac.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/of_net.h>
#include <linux/of_gpio.h>
#include <linux/io.h>

#include "stmmac_platform.h"

struct sg_mac {
	struct device *dev;
	struct reset_control *rst;
	struct clk *clk_tx;
	struct clk *gate_clk_tx;
	struct clk *gate_clk_ref;
	struct gpio_desc *reset;
};

static u64 sg_dma_mask = DMA_BIT_MASK(40);

static int sg_eth_reset_phy(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int phy_reset_gpio;

	if (!np)
		return 0;

	phy_reset_gpio = of_get_named_gpio(np, "phy-reset-gpios", 0);

	if (phy_reset_gpio < 0)
		return 0;

	if (gpio_request(phy_reset_gpio, "eth-phy-reset"))
		return 0;

	/* RESET_PU */
	gpio_direction_output(phy_reset_gpio, 0);
	mdelay(100);

	gpio_direction_output(phy_reset_gpio, 1);
	/* RC charging time */
	mdelay(100);

	return 0;
}

static void sg_mac_fix_speed(void *priv, unsigned int speed)
{
	struct sg_mac *bsp_priv = priv;
	unsigned long rate = 125000000;
	bool needs_calibration = false;
	int err;

	switch (speed) {
	case SPEED_1000:
		needs_calibration = true;
		rate = 125000000;
		break;

	case SPEED_100:
		needs_calibration = true;
		rate = 25000000;
		break;

	case SPEED_10:
		needs_calibration = true;
		rate = 2500000;
		break;

	default:
		dev_err(bsp_priv->dev, "invalid speed %u\n", speed);
		break;
	}

	if (needs_calibration) {
		err = clk_set_rate(bsp_priv->clk_tx, rate);
		if (err < 0)
			dev_err(bsp_priv->dev, "failed to set TX rate: %d\n"
					, err);
	}
}

void sg_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct sg_mac *bsp_priv = priv;

	clk_disable_unprepare(bsp_priv->gate_clk_tx);
	clk_disable_unprepare(bsp_priv->gate_clk_ref);
}

static int sg_validate_ucast_entries(struct device *dev, int ucast_entries)
{
	int x = ucast_entries;

	switch (x) {
	case 1 ... 32:
	case 64:
	case 128:
		break;
	default:
		x = 1;
		dev_info(dev, "Unicast table entries set to unexpected value %d\n",
			 ucast_entries);
		break;
	}
	return x;
}

static int sg_validate_mcast_bins(struct device *dev, int mcast_bins)
{
	int x = mcast_bins;

	switch (x) {
	case HASH_TABLE_SIZE:
	case 128:
	case 256:
		break;
	default:
		x = 0;
		dev_info(dev, "Hash table entries set to unexpected value %d\n",
			 mcast_bins);
		break;
	}
	return x;
}

static int sophgo_get_platform_resources(struct platform_device *pdev,
				  struct stmmac_resources *stmmac_res)
{
	char *tx_ch = "tx_ch";
	char *rx_ch = "rx_ch";
	char int_name[8];

	for (int i = 0; i < 8; i++) {
		sprintf(int_name, "%s%d", tx_ch, i);
		stmmac_res->tx_irq[i] = platform_get_irq_byname(pdev, int_name);
		if (stmmac_res->tx_irq[i] < 0)
			return stmmac_res->tx_irq[i];
	}
	for (int i = 0; i < 8; i++) {
		sprintf(int_name, "%s%d", rx_ch, i);
		stmmac_res->rx_irq[i] = platform_get_irq_byname(pdev, int_name);
		if (stmmac_res->rx_irq[i] < 0)
			return stmmac_res->rx_irq[i];
	}

	return 0;
}

static void sg_dwmac_probe_config_dt(struct platform_device *pdev, struct plat_stmmacenet_data *plat)
{
	struct device_node *np = pdev->dev.of_node;

	of_property_read_u32(np, "snps,multicast-filter-bins", &plat->multicast_filter_bins);
	of_property_read_u32(np, "snps,perfect-filter-entries", &plat->unicast_filter_entries);
	plat->unicast_filter_entries = sg_validate_ucast_entries(&pdev->dev,
								 plat->unicast_filter_entries);
	plat->multicast_filter_bins = sg_validate_mcast_bins(&pdev->dev,
							     plat->multicast_filter_bins);
	plat->sph_disable = 1;
}

static int sg_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct sg_mac *bsp_priv = NULL;
	struct device_node *np = pdev->dev.of_node;
	// struct phy_device *phydev = NULL;
	// struct stmmac_priv *priv = NULL;
	// struct net_device *ndev = NULL;
	int ret;

	pdev->dev.dma_mask = &sg_dma_mask;
	pdev->dev.coherent_dma_mask = sg_dma_mask;

	sg_eth_reset_phy(pdev);

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	plat_dat->multi_msi_en = of_property_read_bool(np, "snps,multi_msi_en");
	if (plat_dat->multi_msi_en) {
		ret = sophgo_get_platform_resources(pdev, &stmmac_res);
		if (ret)
			return ret;
	}

	sg_dwmac_probe_config_dt(pdev, plat_dat);
	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_remove_config_dt;

	bsp_priv = devm_kzalloc(&pdev->dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return PTR_ERR(bsp_priv);

	bsp_priv->dev = &pdev->dev;

	/* clock setup */
	bsp_priv->clk_tx = devm_clk_get(&pdev->dev,
					"clk_tx");
	if (IS_ERR(bsp_priv->clk_tx))
		dev_warn(&pdev->dev, "Cannot get mac tx clock!\n");
	else
		plat_dat->fix_mac_speed = sg_mac_fix_speed;

	bsp_priv->gate_clk_tx = devm_clk_get(&pdev->dev, "gate_clk_tx");
	if (IS_ERR(bsp_priv->gate_clk_tx))
		dev_warn(&pdev->dev, "Cannot get mac tx gating clock!\n");
	else
		clk_prepare_enable(bsp_priv->gate_clk_tx);

	bsp_priv->gate_clk_ref = devm_clk_get(&pdev->dev, "gate_clk_ref");
	if (IS_ERR(bsp_priv->gate_clk_ref))
		dev_warn(&pdev->dev, "Cannot get mac ref gating clock!\n");
	else
		clk_prepare_enable(bsp_priv->gate_clk_ref);

	plat_dat->bsp_priv = bsp_priv;
	plat_dat->exit = sg_dwmac_exit;
#if 0
	ndev = dev_get_drvdata(&pdev->dev);
	priv = netdev_priv(ndev);
	phydev = mdiobus_get_phy(priv->mii, 0);
	if (phydev == NULL) {
		dev_err(&pdev->dev, "Can not get phy in addr 0\n");
		goto err_remove_config_dt;
	}

	/* set green LED0 active for transmit, yellow LED1 for link*/
	ret = phy_write_paged(phydev, 0, 0x1f, 0xd04);
	if (ret < 0)
		dev_err(&pdev->dev, "Can not select page 0xd04\n");
	ret = phy_write_paged(phydev, 0xd04, 0x10, 0x617f);
	if (ret < 0)
		dev_err(&pdev->dev, "Can not alter LED Configuration\n");
	/* disable eee LED function */
	ret = phy_write_paged(phydev, 0xd04, 0x11, 0x0);
	if (ret < 0)
		dev_err(&pdev->dev, "Can not disable EEE Configuration\n");
	ret = phy_write_paged(phydev, 0, 0x1f, 0);
	if (ret < 0)
		dev_err(&pdev->dev, "Can not select page 0\n");
#endif
	return 0;

err_remove_config_dt:
	stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

static const struct of_device_id sg_dwmac_match[] = {
	{ .compatible = "sophgo,ethernet" },
	{ }
};
MODULE_DEVICE_TABLE(of, sg_dwmac_match);

static struct platform_driver sg_dwmac_driver = {
	.probe  = sg_dwmac_probe,
	.remove_new = stmmac_pltfr_remove,
	.driver = {
		.name           = "sg-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = sg_dwmac_match,
	},
};
module_platform_driver(sg_dwmac_driver);

MODULE_AUTHOR("杨东 (Yang Dong) <dong.yang@sophgo.com>");
MODULE_DESCRIPTION("Sophgo DWMAC specific glue layer");
MODULE_LICENSE("GPL");