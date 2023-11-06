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

struct sophgo_dw_pcie {
	struct dw_pcie			*pci;
#if 0
	void __iomem			*apb_base;
	struct phy			*phy;
	struct clk_bulk_data		*clks;
	unsigned int			clk_cnt;
	struct reset_control		*rst;
	struct gpio_desc		*rst_gpio;
	struct regulator                *vpcie3v3;
	struct irq_domain		*irq_domain;
#endif
};

#endif