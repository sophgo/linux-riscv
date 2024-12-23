// SPDX-License-Identifier: GPL-2.0+
/*
 * Sophgo DWMAC platform driver
 *
 * Copyright (C) 2024 Inochi Amaoto <inochiama@gmail.com>
 */

#include <linux/bits.h>
#include <linux/mod_devicetable.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#include "stmmac_platform.h"

struct sophgo_dwmac {
	struct device *dev;
	struct clk *clk_tx;
};


/*TODO: backport when v6.13 release*/
/**
 * rgmii_clock - map link speed to the clock rate
 * @speed: link speed value
 *
 * Description: maps RGMII supported link speeds
 * into the clock rates.
 *
 * Returns: clock rate or negative errno
 */
static inline long rgmii_clock(unsigned int speed)
{
	switch (speed) {
	case SPEED_10:
		return 2500000;
	case SPEED_100:
		return 25000000;
	case SPEED_1000:
		return 125000000;
	default:
		return -EINVAL;
	}
}

static void sophgo_dwmac_fix_mac_speed(void *priv, unsigned int speed, unsigned int mode)
{
	struct sophgo_dwmac *dwmac = priv;
	long rate;
	int ret;

	rate = rgmii_clock(speed);
	if (rate < 0) {
		dev_err(dwmac->dev, "invalid speed %u\n", speed);
		return;
	}

	ret = clk_set_rate(dwmac->clk_tx, rate);
	if (ret)
		dev_err(dwmac->dev, "failed to set tx rate %lu: %pe\n",
			rate, ERR_PTR(ret));
}

static int sophgo_sg2044_dwmac_init(struct platform_device *pdev,
				    struct plat_stmmacenet_data *plat_dat,
				    struct stmmac_resources *stmmac_res)
{
	struct sophgo_dwmac *dwmac;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->clk_tx = devm_clk_get_enabled(&pdev->dev, "tx");
	if (IS_ERR(dwmac->clk_tx))
		return dev_err_probe(&pdev->dev, PTR_ERR(dwmac->clk_tx),
				     "failed to get tx clock\n");

	dwmac->dev = &pdev->dev;
	plat_dat->bsp_priv = dwmac;
	plat_dat->flags |= STMMAC_FLAG_SPH_DISABLE;
	plat_dat->fix_mac_speed = sophgo_dwmac_fix_mac_speed;
	plat_dat->multicast_filter_bins = 0;
	plat_dat->unicast_filter_entries = 1;

	return 0;
}

static int sophgo_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat),
				     "dt configuration failed\n");

	ret = sophgo_sg2044_dwmac_init(pdev, plat_dat, &stmmac_res);
	if (ret)
		return ret;

	return stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
}

static const struct of_device_id sophgo_dwmac_match[] = {
	{ .compatible = "sophgo,sg2044-dwmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_dwmac_match);

static struct platform_driver sophgo_dwmac_driver = {
	.probe  = sophgo_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name = "sophgo-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = sophgo_dwmac_match,
	},
};
module_platform_driver(sophgo_dwmac_driver);

MODULE_AUTHOR("Inochi Amaoto <inochiama@gmail.com>");
MODULE_DESCRIPTION("Sophgo DWMAC platform driver");
MODULE_LICENSE("GPL");
