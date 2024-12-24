// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include "./pcie_phy.h"

#define PHY_MAX_LANE_NUM 2

struct sophgo_pcie_phy {
	struct sophgo_pcie_data *phy_data;
	void __iomem *reg_base;
	struct phy_pcie_instance {
		struct phy *phy;
		u32 index;
	} phys[PHY_MAX_LANE_NUM];
	struct mutex pcie_mutex;
	int init_cnt;
	uint32_t lanes;
};

static struct sophgo_pcie_phy *to_pcie_phy(struct phy_pcie_instance *inst)
{
	return container_of(inst, struct sophgo_pcie_phy,
					phys[inst->index]);
}

static struct phy *sophgo_pcie_phy_of_xlate(struct device *dev,
					    const struct of_phandle_args *args)
{
	struct sophgo_pcie_phy *sg_phy = dev_get_drvdata(dev);

	if (args->args_count == 0)
		return sg_phy->phys[0].phy;

	if (WARN_ON(args->args[0] >= PHY_MAX_LANE_NUM))
		return ERR_PTR(-ENODEV);

	return sg_phy->phys[args->args[0]].phy;
}

static int sg_pcie_phy_config_ss_mode(struct sophgo_pcie_phy *sg_phy)
{
	void __iomem *base_addr = sg_phy->reg_base;
	uint32_t val = 0;
	uint32_t ss_mode;

	if (sg_phy->lanes == 4)
		ss_mode = PCIE_SERDES_MODE_X4_X4;
	else if (sg_phy->lanes == 8)
		ss_mode = PCIE_SERDES_MODE_X8;
	else {
		pr_err("not available pcie phy ss mode, default to x4 x4\n");
		ss_mode = PCIE_SERDES_MODE_X4_X4;
	}

	val = readl(base_addr + CXP_TOP_REG_RX000);
	val &= (~CXP_TOP_REG_RX000_SS_MODE_MASK);
	val |= ss_mode & CXP_TOP_REG_RX000_SS_MODE_MASK;
	writel(val, (base_addr + CXP_TOP_REG_RX000));

	return 0;
}

static int pcie_config_phy_bl_bypass(struct sophgo_pcie_phy *sg_phy, uint32_t sram_bypass_mode)
{
	uint32_t val = 0;
	uint32_t phy_id = 0;
	void __iomem  *reg_base = 0;
	uint64_t reg_addr = 0;

	// reg_base = C2C_PCIE_0_PHY_INTF_REG_BASE(c2c_id) + (wrapper_id * 0x800000);
	reg_base = sg_phy->reg_base;
	for (phy_id = 0; phy_id < PCIE_PHY_ID_BUTT; phy_id++) {
		reg_addr = CXP_TOP_REG_RX060 + (phy_id * 0x10);

		//cfg phy0 phy0_sram_bypass bypass
		val = readl(reg_base + reg_addr);
		val &= (~CXP_TOP_REG_PHY0_PHY1_SRAM_BYPASS_MASK);
		val |= (sram_bypass_mode & 0x3) << 5;
		writel(val, (reg_base + reg_addr));
	}

	return 0;
}

static int pcie_config_phy_pwr_stable(struct sophgo_pcie_phy *sg_phy)
{
	uint32_t val = 0;
	uint32_t phy_id = 0;
	void __iomem  *base_addr = 0;
	uint32_t reg_addr;

	base_addr = sg_phy->reg_base;
	//default  pciex8 need cfg
	//cfg  phy0_upcs_pwr_stable
	val = readl(base_addr + CXP_TOP_REG_RX90C);
	val |= (0x1 << CXP_TOP_REG_RX90C_UPCS_PWR_STABLE_BIT);
	writel(val, (base_addr + CXP_TOP_REG_RX90C));

	for (phy_id = 0; phy_id < PCIE_PHY_ID_BUTT; phy_id++) {
		reg_addr = CXP_TOP_REG_RX054 + (phy_id * 0x10);
		//cfg  phy_x_pcs_pwr_stable
		//cfg  phy_x_pma_pwr_stable
		val = readl(base_addr + reg_addr);
		val |= (0x1 << CXP_TOP_REG_RX054_PHY0_PCS_PWR_STABLE_BIT);
		val |= (0x1 << CXP_TOP_REG_RX054_PHY0_PMA_PWR_STABLE_BIT);
		writel(val, (base_addr + reg_addr));
	}

	return 0;
}

static void pcie_config_phy_eq_afe(struct sophgo_pcie_phy *sg_phy)
{
	uint32_t reg_off = 0;
	uint32_t reg_set = 0;
	uint64_t reg_addr;
	void __iomem *pcie_base_addr;
	uint32_t reg_base[7] = {0x974, 0x994, 0xa14, 0xa34, 0xa54, 0xa74, 0xa94};

	pcie_base_addr = sg_phy->reg_base;
	for (reg_set = 0; reg_set < 7; reg_set++) {
		for (reg_off = 0; reg_off < 4; reg_off++) {
			reg_addr = reg_base[reg_set] + (reg_off * 4);
			writel(0x20002, pcie_base_addr + reg_addr);
		}
	}
}

static int sophgo_pcie_phy_init(struct phy *phy)
{
	struct phy_pcie_instance *inst = phy_get_drvdata(phy);
	struct sophgo_pcie_phy *sg_phy = to_pcie_phy(inst);

	mutex_lock(&sg_phy->pcie_mutex);

	if (sg_phy->init_cnt++)
		goto err_out;

	pr_err("sophgo pcie phy init:va:0x%llx, num lanes:%d\n", (uint64_t)sg_phy->reg_base, sg_phy->lanes);
	sg_pcie_phy_config_ss_mode(sg_phy);
	pcie_config_phy_bl_bypass(sg_phy, PCIE_PHY_NO_SRAM_BYPASS);
	pcie_config_phy_pwr_stable(sg_phy);
	pcie_config_phy_eq_afe(sg_phy);

err_out:
	mutex_unlock(&sg_phy->pcie_mutex);
	return 0;
}

static int sophgo_pcie_config(struct phy *phy, union phy_configure_opts *opts)
{
	struct phy_pcie_instance *inst = phy_get_drvdata(phy);
	struct sophgo_pcie_phy *sg_phy = to_pcie_phy(inst);
	uint32_t phy_num = sg_phy->lanes == 8 ? 2 : 1;
	uint32_t phy_id;
	uint64_t reg_addr;
	uint32_t val;
	int timeout = 0;

	void __iomem *reg_base = sg_phy->reg_base;

	for (phy_id = 0; phy_id < phy_num; phy_id++) {
		//wait sram init done
		reg_addr = CXP_TOP_REG_RX060;
		do {
			val = readl(reg_base + (reg_addr + phy_id * 0x10));
			val = (val >> CXP_TOP_REG_PHYX_SRAM_INIT_DONE_BIT) & 0x1;
			if (val == 1) {
				dev_err(&sg_phy->phys->phy->dev, "phy config success\n");
				break;
			} else {
				timeout++;
				msleep(100);
			}

			if (timeout == 20)
				return -1;
		} while (1);

		//cfg  phy0_sram_ext_ld_done
		reg_addr = CXP_TOP_REG_RX060 + (phy_id * 0x10);
		val = readl(reg_base + reg_addr);
		val |= (0x1 << CXP_TOP_REG_PHY0_PHY1_SRAM_EXT_LD_DONE_BIT);
		writel(val, (reg_base + reg_addr));
	}

	return 0;
}

static int sophgo_pcie_phy_exit(struct phy *phy)
{
	struct phy_pcie_instance *inst = phy_get_drvdata(phy);
	struct sophgo_pcie_phy *sg_phy = to_pcie_phy(inst);

	mutex_lock(&sg_phy->pcie_mutex);

	if (--sg_phy->init_cnt)
		goto err_init_cnt;

err_init_cnt:
	mutex_unlock(&sg_phy->pcie_mutex);
	return 0;
}

static const struct phy_ops ops = {
	.init		= sophgo_pcie_phy_init,
	.exit		= sophgo_pcie_phy_exit,
	.configure	= sophgo_pcie_config,
	.owner		= THIS_MODULE,
};

static const struct of_device_id sophgo_pcie_phy_dt_ids[] = {
	{
		.compatible = "sophgo,pcie-phy",
	},
	{}
};

MODULE_DEVICE_TABLE(of, sophgo_pcie_phy_dt_ids);

static int sophgo_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_phy *sg_phy;
	struct phy_provider *phy_provider;
	int i;
	u32 phy_num = 1;
	struct resource *regs;

	dev_err(dev, "sophgo pcie phy probe\n");
	sg_phy = devm_kzalloc(dev, sizeof(*sg_phy), GFP_KERNEL);
	if (!sg_phy)
		return -ENOMEM;


	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sg_phy->reg_base = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!sg_phy->reg_base) {
		pr_err("[%s] remap reg failed\n", pdev->name);
		return -ENOMEM;
	}

	device_property_read_u32(dev, "bus-width", &sg_phy->lanes);

	pr_err("sophgo pcie phy addr:0x%llx, va:0x%llx, bus width:%d\n", regs->start, (uint64_t)sg_phy->reg_base, sg_phy->lanes);

	mutex_init(&sg_phy->pcie_mutex);

	for (i = 0; i < phy_num; i++) {
		sg_phy->phys[i].phy = devm_phy_create(dev, dev->of_node, &ops);
		if (IS_ERR(sg_phy->phys[i].phy)) {
			dev_err(dev, "failed to create PHY%d\n", i);
			return PTR_ERR(sg_phy->phys[i].phy);
		}
		sg_phy->phys[i].index = i;
		phy_set_drvdata(sg_phy->phys[i].phy, &sg_phy->phys[i]);
		phy_set_bus_width(sg_phy->phys[i].phy, sg_phy->lanes);
	}

	platform_set_drvdata(pdev, sg_phy);
	phy_provider = devm_of_phy_provider_register(dev, sophgo_pcie_phy_of_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver sophgo_pcie_driver = {
	.probe		= sophgo_pcie_phy_probe,
	.driver		= {
		.name	= "sophgo-pcie-phy",
		.of_match_table = sophgo_pcie_phy_dt_ids,
	},
};

module_platform_driver(sophgo_pcie_driver);

MODULE_AUTHOR("tingzhu.wang <tingzhu.wang@sophgo.com>");
MODULE_DESCRIPTION("sophgo PCIe PHY driver");
MODULE_LICENSE("GPL v2");
