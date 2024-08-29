/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Sophgo Co., Ltd.
 *		https://www.sophgo.com
 *
 * Author: Lionel Li <fengchun.li@sophgo.com>
 */

#ifndef _PCIE_DW_SOPHGO_H
#define _PCIE_DW_SOPHGO_H

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dma/edma.h>
#include <linux/gpio/consumer.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/reset.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

#include "pcie-designware.h"

struct sophgo_dw_pcie {
	struct device		*dev;
	void __iomem		*dbi_base;
	void __iomem		*atu_base;
	size_t			atu_size;
	u32			num_ib_windows;
	u32			num_ob_windows;
	u32			region_align;
	u64			region_limit;
	struct dw_pcie_rp	pp;
	const struct dw_pcie_ops *ops;
	u32			version;
	u32			type;
	unsigned long		caps;
	int			num_lanes;
	int			link_gen;
	u8			n_fts[2];
	struct dw_edma_chip	edma;
	struct clk_bulk_data	app_clks[DW_PCIE_NUM_APP_CLKS];
	struct clk_bulk_data	core_clks[DW_PCIE_NUM_CORE_CLKS];
	struct reset_control_bulk_data	app_rsts[DW_PCIE_NUM_APP_RSTS];
	struct reset_control_bulk_data	core_rsts[DW_PCIE_NUM_CORE_RSTS];
	struct gpio_desc	*pe_rst;
};

#define to_sophgo_dw_pcie_from_pp(port) container_of((port), struct sophgo_dw_pcie, pp)

extern struct irq_domain *sophgo_dw_pcie_get_parent_irq_domain(void);

u32 sophgo_dw_pcie_read_dbi(struct sophgo_dw_pcie *pcie, u32 reg, size_t size);
void sophgo_dw_pcie_write_dbi(struct sophgo_dw_pcie *pcie, u32 reg, size_t size, u32 val);
u32 sophgo_dw_pcie_readl_atu(struct sophgo_dw_pcie *pcie, u32 dir, u32 index, u32 reg);
void sophgo_dw_pcie_writel_atu(struct sophgo_dw_pcie *pcie, u32 dir, u32 index, u32 reg, u32 val);

static inline void sophgo_dw_pcie_writel_dbi(struct sophgo_dw_pcie *pcie, u32 reg, u32 val)
{
	sophgo_dw_pcie_write_dbi(pcie, reg, 0x4, val);
}

static inline u32 sophgo_dw_pcie_readl_dbi(struct sophgo_dw_pcie *pcie, u32 reg)
{
	return sophgo_dw_pcie_read_dbi(pcie, reg, 0x4);
}

static inline void sophgo_dw_pcie_writew_dbi(struct sophgo_dw_pcie *pcie, u32 reg, u16 val)
{
	sophgo_dw_pcie_write_dbi(pcie, reg, 0x2, val);
}

static inline u16 sophgo_dw_pcie_readw_dbi(struct sophgo_dw_pcie *pcie, u32 reg)
{
	return sophgo_dw_pcie_read_dbi(pcie, reg, 0x2);
}

static inline void sophgo_dw_pcie_writeb_dbi(struct sophgo_dw_pcie *pcie, u32 reg, u8 val)
{
	sophgo_dw_pcie_write_dbi(pcie, reg, 0x1, val);
}

static inline u8 sophgo_dw_pcie_readb_dbi(struct sophgo_dw_pcie *pcie, u32 reg)
{
	return sophgo_dw_pcie_read_dbi(pcie, reg, 0x1);
}

static inline void __iomem *sophgo_dw_pcie_select_atu(struct sophgo_dw_pcie *pcie, u32 dir,
					       u32 index)
{
	if (dw_pcie_cap_is(pcie, IATU_UNROLL))
		return pcie->atu_base + PCIE_ATU_UNROLL_BASE(dir, index);

	sophgo_dw_pcie_writel_dbi(pcie, PCIE_ATU_VIEWPORT, dir | index);
	return pcie->atu_base;
}

static inline u32 sophgo_dw_pcie_readl_atu_ib(struct sophgo_dw_pcie *pcie, u32 index, u32 reg)
{
	return sophgo_dw_pcie_readl_atu(pcie, PCIE_ATU_REGION_DIR_IB, index, reg);
}

static inline void sophgo_dw_pcie_writel_atu_ib(struct sophgo_dw_pcie *pcie, u32 index, u32 reg,
					 u32 val)
{
	sophgo_dw_pcie_writel_atu(pcie, PCIE_ATU_REGION_DIR_IB, index, reg, val);
}

static inline u32 sophgo_dw_pcie_readl_atu_ob(struct sophgo_dw_pcie *pcie, u32 index, u32 reg)
{
	return sophgo_dw_pcie_readl_atu(pcie, PCIE_ATU_REGION_DIR_OB, index, reg);
}

static inline void sophgo_dw_pcie_writel_atu_ob(struct sophgo_dw_pcie *pcie, u32 index, u32 reg,
					 u32 val)
{
	sophgo_dw_pcie_writel_atu(pcie, PCIE_ATU_REGION_DIR_OB, index, reg, val);
}
static inline void sophgo_dw_pcie_dbi_ro_wr_en(struct sophgo_dw_pcie *pci)
{
	u32 reg = 0;
	u32 val = 0;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = sophgo_dw_pcie_readl_dbi(pci, reg);
	val |= PCIE_DBI_RO_WR_EN;
	sophgo_dw_pcie_writel_dbi(pci, reg, val);
}

static inline void sophgo_dw_pcie_dbi_ro_wr_dis(struct sophgo_dw_pcie *pci)
{
	u32 reg = 0;
	u32 val = 0;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = sophgo_dw_pcie_readl_dbi(pci, reg);
	val &= ~PCIE_DBI_RO_WR_EN;
	sophgo_dw_pcie_writel_dbi(pci, reg, val);
}

#if 0
struct sophgo_dw_pcie {
	struct dw_pcie			*pci;

	void __iomem			*apb_base;
	struct phy			*phy;
	struct clk_bulk_data		*clks;
	unsigned int			clk_cnt;
	struct reset_control		*rst;
	struct gpio_desc		*rst_gpio;
	struct regulator                *vpcie3v3;
	struct irq_domain		*irq_domain;

};
#endif

#endif
