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

//PCIE CTRL REG
#define PCIE_CTRL_INT_SIG_0_REG                             0x048
#define PCIE_CTRL_SFT_RST_SIG_REG                           0x050
#define PCIE_CTRL_REMAPPING_EN_REG                          0x060
#define PCIE_CTRL_HNI_UP_START_ADDR_REG                     0x064
#define PCIE_CTRL_HNI_UP_END_ADDR_REG                       0x068
#define PCIE_CTRL_HNI_DW_ADDR_REG                           0x06c
#define PCIE_CTRL_SN_UP_START_ADDR_REG                      0x070
#define PCIE_CTRL_SN_UP_END_ADDR_REG                        0x074
#define PCIE_CTRL_SN_DW_ADDR_REG                            0x078
#define PCIE_CTRL_AXI_MSI_GEN_CTRL_REG                      0x07c
#define PCIE_CTRL_AXI_MSI_GEN_LOWER_ADDR_REG                0x088
#define PCIE_CTRL_AXI_MSI_GEN_UPPER_ADDR_REG                0x08c
#define PCIE_CTRL_AXI_MSI_GEN_USER_DATA_REG                 0x090
#define PCIE_CTRL_AXI_MSI_GEN_MASK_IRQ_REG                  0x094
#define PCIE_CTRL_IRQ_EN_REG                                0x0a0

#define PCIE_SII_GENERAL_CTRL1_REG                          0x050
#define PCIE_SII_GENERAL_CTRL3_REG                          0x058

#define PCIE_CTRL_INT_SIG_0_PCIE_INTX_SHIFT_BIT             5
#define PCIE_CTRL_SFT_RST_SIG_COLD_RSTN_BIT                 0
#define PCIE_CTRL_SFT_RST_SIG_PHY_RSTN_BIT                  1
#define PCIE_CTRL_SFT_RST_SIG_WARM_RSTN_BIT                 2
#define PCIE_CTRL_REMAP_EN_HNI_TO_PCIE_UP4G_EN_BIT          0
#define PCIE_CTRL_REMAP_EN_HNI_TO_PCIE_DW4G_EN_BIT          1
#define PCIE_CTRL_REMAP_EN_SN_TO_PCIE_UP4G_EN_BIT           2
#define PCIE_CTRL_REMAP_EN_SN_TO_PCIE_DW4G_EN_BIT           3
#define PCIE_CTRL_AXI_MSI_GEN_CTRL_MSI_GEN_EN_BIT           0
#define PCIE_CTRL_IRQ_EN_INTX_SHIFT_BIT                     1

#define CDMA_CSR_RCV_ADDR_H32				(0x1004)
#define CDMA_CSR_RCV_ADDR_M16				(0x1008)
#define CDMA_CSR_INTER_DIE_RW				(0x100c)
#define CDMA_CSR_4					(0x1010)
#define CDMA_CSR_INTRA_DIE_RW				(0x123c)

#define CDMA_CSR_RCV_CMD_OS				15

// CDMA_CSR_INTER_DIE_RW
#define CDMA_CSR_INTER_DIE_READ_ADDR_L4		0
#define CDMA_CSR_INTER_DIE_READ_ADDR_H4		4
#define CDMA_CSR_INTER_DIE_WRITE_ADDR_L4	8
#define CDMA_CSR_INTER_DIE_WRITE_ADDR_H4	12

// CDMA_CSR_INTRA_DIE_RW
#define CDMA_CSR_INTRA_DIE_READ_ADDR_L4		0
#define CDMA_CSR_INTRA_DIE_READ_ADDR_H4		4
#define CDMA_CSR_INTRA_DIE_WRITE_ADDR_L4	8
#define CDMA_CSR_INTRA_DIE_WRITE_ADDR_H4	12

#define GENMASK_32(h, l) \
	(((0xFFFFFFFF) << (l)) & (0xFFFFFFFF >> (32UL - 1 - (h))))

#define PCIE_SII_GENERAL_CTRL1_DEVICE_TYPE_MASK             GENMASK_32(12, 9)
#define PCIE_CTRL_AXI_MSI_GEN_CTRL_MSI_GEN_MULTI_MSI_MASK   GENMASK_32(3, 1)

#define CHIP_BM1690 0x1690
#define CHIP_BM1690E 0x169e

enum pcie_rst_status {
	PCIE_RST_ASSERT = 0,
	PCIE_RST_DE_ASSERT,
	PCIE_RST_STATUS_BUTT
};

enum {
	C2C_PCIE_X8_0 = 0b0101,
	C2C_PCIE_X8_1 = 0b0111,
	C2C_PCIE_X4_0 = 0b0100,
	C2C_PCIE_X4_1 = 0b0110,
	CXP_PCIE_X8 = 0b1010,
	CXP_PCIE_X4 = 0b1011,
};

enum {
	// RN: K2K; RNI: CCN
	AXI_RNI = 0b1001,
	AXI_RN = 0b1000,
};

struct PCIE_EQ_COEF {
	uint32_t cursor;
	uint32_t pre_cursor;
	uint32_t post_cursor;
};

struct sophgo_dw_pcie {
	struct device		*dev;
	void __iomem		*dbi_base;
	void __iomem		*atu_base;
	void __iomem		*sii_reg_base;
	void __iomem		*ctrl_reg_base;
	void __iomem		*c2c_top;
	void __iomem		*cdma_reg_base;
	uint64_t		cfg_start_addr;
	uint64_t		cfg_end_addr;
	uint64_t		slv_start_addr;
	uint64_t		slv_end_addr;
	uint64_t		dw_start;
	uint64_t		dw_end;
	uint64_t		up_start_addr;
	uint64_t		cdma_pa_start;
	uint64_t		cdma_size;
	uint32_t		c2c_pcie_rc;
	uint32_t		chip_type;
	size_t			atu_size;
	uint32_t		pcie_card;
	uint32_t		pcie_route_config;
	uint32_t		dst_boardid_shift;
	uint32_t 		dst_chipid;
	uint32_t		dst_chipid_shift;
	uint32_t		func_num_shift;
	uint64_t		global_chipid;
	uint64_t		board_size;
	uint64_t		board_id;
	uint64_t		board_id_shift;
	uint64_t		socket_id;
	u32			num_ib_windows;
	u32			num_ob_windows;
	u32			region_align;
	u64			region_limit;
	int			irq;
	struct irq_domain	*intx_domain;
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
	int pe_rst;
	struct phy *phy;
};

#define to_sophgo_dw_pcie_from_pp(port) container_of((port), struct sophgo_dw_pcie, pp)

extern struct irq_domain *sophgo_get_msi_irq_domain(void);
u32 sophgo_dw_pcie_read_ctrl(struct sophgo_dw_pcie *pcie, u32 reg, size_t size);
void sophgo_dw_pcie_write_ctrl(struct sophgo_dw_pcie *pcie, u32 reg, size_t size, u32 val);
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

#define PORTCODE_BOARDSIZE_SHIFT	(27)
#define PORTCODE_BOARDID_SHIFT		(20)

#define WR_ORDER_START_LOWER	(0x0)
#define WR_ORDER_START_UPPER	(0x4)
#define WR_ORDER_END_LOWER	(0x8)
#define WR_ORDER_END_UPPER	(0xc)

#define WR_ORDER_PC_MODE	(0x1)
#define WR_ORDER_CHIP_MODE	(0x2)
#define WR_ORDER_ALL_ADDR_MODE	(0x0)

struct wr_order_list {
	uint64_t start_addr;
	uint64_t size;
};


#endif

