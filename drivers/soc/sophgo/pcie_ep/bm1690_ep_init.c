// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/phy/phy.h>
#include <linux/of_gpio.h>
#include <linux/wordpart.h>
#include "ap_pcie_ep.h"
#include "bm1690_ep_init.h"


static void pcie_wait_core_clk(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	void __iomem *base_addr = sg_ep->sii_base;

	do {
		udelay(10);
		val = readl(base_addr + 0x5c); //GEN_CTRL_4
		val = (val >> 8) & 0x1; //bit8, pcie_rst_n
	} while (val != 1);
}

static void pcie_config_ctrl(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	uint32_t pcie_gen_slt = sg_ep->speed;
	void __iomem *sii_reg_base = sg_ep->sii_base;
	void __iomem *dbi_reg_base = sg_ep->dbi_base;
	uint32_t pcie_dev_type = 0; // 0: EP; 4: RC;

	//config device_type
	val = readl(sii_reg_base + PCIE_SII_GENERAL_CTRL1_REG);
	val &= (~PCIE_SII_GENERAL_CTRL1_DEVICE_TYPE_MASK);
	val |= (pcie_dev_type << 9);
	writel(val, (sii_reg_base + PCIE_SII_GENERAL_CTRL1_REG));

	//config Directed Speed Change	Writing '1' to this field instructs the LTSSM to initiate
	//a speed change to Gen2 or Gen3 after the link is initialized at Gen1 speed
	val = readl(dbi_reg_base + 0x80c);
	val = val | 0x20000;
	writel(val, (dbi_reg_base + 0x80c));

	//config generation_select-pcie_cap_target_link_speed
	val = readl(dbi_reg_base + 0xa0);
	val = (val & 0xfffffff0) | pcie_gen_slt;
	writel(val, (dbi_reg_base + 0xa0));

	// config ecrc generation enable
	val = readl(dbi_reg_base + 0x118);
	val = 0x3e0;
	writel(val, (dbi_reg_base + 0x118));
}

struct PCIE_EQ_COEF {
	uint32_t cursor;
	uint32_t pre_cursor;
	uint32_t post_cursor;
};

static void pcie_config_eq(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	uint32_t speed = 0;
	uint32_t pset_id = 0;
	void __iomem *pcie_dbi_base = sg_ep->dbi_base;
	struct PCIE_EQ_COEF eq_coef_tbl[11] = { //p0 ~ p10
		{36, 0, 12}, {40, 0, 8}, {38, 0, 10}, {42, 0, 6}, {48, 0, 0}, {44, 4, 0},
		{42, 6,  0}, {34, 5, 9}, {36, 6,  6}, {40, 8, 0}, {32, 0, 16}
	};

	for (speed = 0; speed < 3; speed++) {
		val = readl(pcie_dbi_base + 0x890); //set speed
		val &= 0xfcffffff;
		val |= (speed << 24);
		writel(val, (pcie_dbi_base + 0x890));

		val = readl(pcie_dbi_base + 0x894);
		val &= 0xfffff000; //bit[11, 0]
		val |= 16;
		val |= (48 << 6);
		writel(val, (pcie_dbi_base + 0x894));

		for (pset_id = 0; pset_id < 11; pset_id++) {
			val = readl(pcie_dbi_base + 0x89c);
			val &= 0xfffffff0; //bit[3, 0]
			val |= pset_id;
			writel(val, (pcie_dbi_base + 0x89c));

			val = readl(pcie_dbi_base + 0x898);
			val &= 0xfffc0000; //bit[17, 0]
			val |= eq_coef_tbl[pset_id].pre_cursor;
			val |= (eq_coef_tbl[pset_id].cursor << 6);
			val |= (eq_coef_tbl[pset_id].post_cursor << 12);
			writel(val, (pcie_dbi_base + 0x898));

			val = readl(pcie_dbi_base + 0x8a4);
			if (val & 0x1) //bit0
				pr_info("illegal coef pragrammed, speed[%d], pset[%d].\n",
						speed, pset_id);
		}
	}
}

static void pcie_config_link(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	void __iomem *base_addr = sg_ep->dbi_base;
	uint32_t pcie_ln_cnt = sg_ep->lane_num;

	//config lane_count
	val = readl(base_addr + 0x8c0);
	val = (val & 0xffffffc0) | pcie_ln_cnt;
	writel(val, (base_addr + 0x8c0));

	//config eq bypass highest rate disable
	val = readl(base_addr + 0x1c0);
	val |= 0x1;
	writel(val, (base_addr + 0x1c0));
}


static void pcie_config_ep_function(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	uint32_t func = 0;
	void __iomem *pcie_dbi_base = sg_ep->dbi_base;
	uint32_t func_num = sg_ep->func_num;
	uint32_t ss_mode = sg_ep->lane_num == 8 ? 0 : 1;

	//why?
	//if ((phy_id == PCIE_PHY_ID_1) && (func_num != 1))
	//	return;

	//enable DBI_RO_WR_EN
	val = readl(pcie_dbi_base + 0x8bc);
	val = (val & 0xfffffffe) | 0x1;
	writel(val, (pcie_dbi_base + 0x8bc));


	//config subdevice id
	writel(PCIE_DATA_LINK_C2C << 16, pcie_dbi_base + SUBSYSTEM_ID_SUBSYTEM_VENDOR_DI_REG);
	pr_err("config dbi subdevice id:0x%x\n", readl(pcie_dbi_base + SUBSYSTEM_ID_SUBSYTEM_VENDOR_DI_REG));

	val = readl(pcie_dbi_base + 0xc);
	if (func_num == 0x1)
		writel((val & (~(1 << 23))), (pcie_dbi_base + 0xc));
	else
		writel((val | (1 << 23)), (pcie_dbi_base + 0xc));

	for (func = 0; func < func_num; func++) {
		val = readl((pcie_dbi_base + 0x8) + (func << 16));
		writel((val | (0x1200 << 16)), ((pcie_dbi_base + 0x8) + (func << 16)));
		writel(0x16901f1c, (pcie_dbi_base + (func << 16)));
		val = readl((pcie_dbi_base + (func << 16)) + 0x78);
		val &= 0xffff8f1f; //bit[7, 5], bit[14, 12]
		val |= 0x1 << 5;
		val |= 0x1 << 12;
		writel(val, ((pcie_dbi_base + (func << 16)) + 0x78));
		val = readl((pcie_dbi_base + (func << 16)) + 0xb0);
		val &= 0xf800ffff; //bit[26, 16]
		val |= 0x7 << 16;
		writel(val, ((pcie_dbi_base + (func << 16)) + 0xb0));
		if (ss_mode == 1) {
			val = readl((pcie_dbi_base + (func << 16)) + 0x710);
			val &= 0xffc0ffff; //bit[21, 16]
			val |= 0x7 << 16;
			writel(val, ((pcie_dbi_base + (func << 16)) + 0x710));
			val = readl((pcie_dbi_base + (func << 16)) + 0x80c);
			val &= 0xffffe0ff; //bit[12, 8]
			val |= 0x4 << 8;
			writel(val, ((pcie_dbi_base + (func << 16)) + 0x80c));
			val = readl((pcie_dbi_base + (func << 16)) + 0x7c);
			val &= 0xfffffc0f; //bit[9, 4]
			val |= 0x4 << 4;
			writel(val, ((pcie_dbi_base + (func << 16)) + 0x7c));
		}
	}

	if (func_num > 1) {
		val = readl(pcie_dbi_base + 0x718);
		val &= ~0xff;
		val |= func_num - 1;
		writel(val, (pcie_dbi_base + 0x718));
	}

	// disable DBI_RO_WR_EN
	val = readl(pcie_dbi_base + 0x8bc);
	val &= 0xfffffffe;
	writel(val, (pcie_dbi_base + 0x8bc));

}

#if 0
static void pcie_config_ep_bar(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	void __iomem *pcie_dbi_base = sg_ep->dbi_base;
	int func = 0;

	//enable DBI_RO_WR_EN
	val = readl(pcie_dbi_base + 0x8bc);
	val = (val & 0xfffffffe) | 0x1;
	writel(val, (pcie_dbi_base + 0x8bc));

	writel(0x3ffff0, ((pcie_dbi_base + 0x10) + (func << 16)));
	writel(0x3ffff0, ((pcie_dbi_base + 0x14) + (func << 16)));
	writel(0xffffc, ((pcie_dbi_base + 0x18) + (func << 16)));
	writel(0xffffc, ((pcie_dbi_base + 0x20) + (func << 16)));

	writel(0x3ffff1, ((pcie_dbi_base + C2C_PCIE_DBI2_OFFSET + 0x10) + (func << 16)));
	writel(0x3ffff1, ((pcie_dbi_base + C2C_PCIE_DBI2_OFFSET + 0x14) + (func << 16)));
	writel(0xfffff1, ((pcie_dbi_base + C2C_PCIE_DBI2_OFFSET + 0x18) + (func << 16)));
	writel(0xfffff1, ((pcie_dbi_base + C2C_PCIE_DBI2_OFFSET + 0x20) + (func << 16)));

	// disable DBI_RO_WR_EN
	val = readl(pcie_dbi_base + 0x8bc);
	val &= 0xfffffffe;
	writel(val, (pcie_dbi_base + 0x8bc));
}
#endif

static void pcie_enable_ltssm(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	void __iomem *base_addr = sg_ep->sii_base;

	//config app_ltssm_enable
	val = readl(base_addr + PCIE_SII_GENERAL_CTRL3_REG);
	val |= 0x1;
	writel(val, (base_addr + PCIE_SII_GENERAL_CTRL3_REG));
}

static void pcie_wait_link(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	void __iomem *base_addr = sg_ep->sii_base;

	do {
		udelay(10);
		val = readl(base_addr + 0xb4); //LNK_DBG_2
		val = (val >> 7) & 0x1; //bit7, RDLH_LINK_UP
	} while (val != 1);
}

static void pcie_check_link_status(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	uint32_t speed = 0;
	uint32_t width = 0;
	uint32_t ltssm_state = 0;
	uint32_t pcie_gen_slt = sg_ep->speed;
	uint32_t pcie_ln_cnt = sg_ep->lane_num;
	void __iomem *pcie_sii_base = sg_ep->sii_base;
	void __iomem *pcie_dbi_base = sg_ep->dbi_base;


	val = readl(pcie_sii_base + 0xb4); //LNK_DBG_2
	ltssm_state = val & 0x3f; //bit[5,0]
	if (ltssm_state != 0x11)
		pr_err("PCIe link fail, ltssm_state = 0x%x\n", ltssm_state);

	speed = (val >> 8) & 0x7; //bit[10,8]
	if ((speed + 1) != pcie_gen_slt)
		pr_err("link speed, expect gen%d, current gen%d\n", pcie_gen_slt, (speed + 1));

	val = readl(pcie_dbi_base + 0x80);
	width = (val >> 20) & 0x3f; //bit[25:20]
	if (width != pcie_ln_cnt)
		pr_err("link width, expect x%d, current x%d\n", pcie_ln_cnt, width);

	pr_info("PCIe Link status, ltssm[0x%x], gen%d, x%d.\n", ltssm_state, (speed + 1), width);
}

static void pcie_config_axi_route(struct sophgo_pcie_ep *sg_ep)
{
	uint64_t cfg_start_addr = sg_ep->c2c_config_base;
	uint64_t cfg_end_addr = cfg_start_addr + sg_ep->c2c_config_size;
	void *c2c_top_reg = sg_ep->c2c_top_base;
	int work_mode = SOC_WORK_MODE_AI;


	if (work_mode == SOC_WORK_MODE_SERVER) {
		writel(0xffffffff, (c2c_top_reg + 0x04)); //rn.start
		writel(0xffffffff, (c2c_top_reg + 0x08));

		writel(0xffffffff, (c2c_top_reg + 0x0c)); //rn.end
		writel(0xffffffff, (c2c_top_reg + 0x10));

		writel(0x0, (c2c_top_reg + 0x14)); //rni.start
		writel(0x0, (c2c_top_reg + 0x18));

		writel(0xffffffff, (c2c_top_reg + 0x1c)); //rni.end
		writel(0xffffffff, (c2c_top_reg + 0x20));
	}

	writel((cfg_start_addr & 0xffffffff), (c2c_top_reg + 0x24));
	writel(((cfg_start_addr >> 32) & 0xffffffff), (c2c_top_reg + 0x28));

	writel((cfg_end_addr & 0xffffffff), (c2c_top_reg + 0x2c));
	writel(((cfg_end_addr >> 32) & 0xffffffff), (c2c_top_reg + 0x30));
}

static int pcie_config_soft_phy_reset(struct sophgo_pcie_ep *pcie, uint32_t rst_status)
{
	uint32_t val = 0;
	void __iomem *reg_base;

	//deassert = 1; assert = 0;
	if ((rst_status != 0) && (rst_status != 1))
		return -1;

	reg_base = pcie->ctrl_reg_base;

	//cfg soft_phy_rst_n , first cfg 1
	val = readl(reg_base + PCIE_CTRL_SFT_RST_SIG_REG);
	if (rst_status == 1)
		val |= (0x1 << PCIE_CTRL_SFT_RST_SIG_PHY_RSTN_BIT);
	else
		val &= (~PCIE_CTRL_SFT_RST_SIG_PHY_RSTN_BIT);

	writel(val, (reg_base + PCIE_CTRL_SFT_RST_SIG_REG));

	udelay(1);

	return 0;
}

static void pcie_check_radm_status(struct sophgo_pcie_ep *pcie)
{
	uint32_t val = 0;
	void __iomem *base_addr = pcie->ctrl_reg_base;

	do {
		udelay(10);
		if (pcie->lane_num == 8) {
			val = readl(base_addr + 0xfc);
			val = (val >> 29) & 0x1; //bit29, radm_idle
		} else {
			val = readl(base_addr + 0xe8);
			val = (val >> 21) & 0x1; //bit21, radm_idle
		}
	} while (val != 1);
}

static int pcie_config_soft_cold_reset(struct sophgo_pcie_ep *pcie)
{
	uint32_t val = 0;
	void __iomem  *reg_base;


	reg_base = pcie->ctrl_reg_base;

	//cfg soft_cold_rst_n , first cfg 0
	val = readl(reg_base + PCIE_CTRL_SFT_RST_SIG_REG);
	val &= (~PCIE_CTRL_SFT_RST_SIG_COLD_RSTN_BIT);
	writel(val, (reg_base + PCIE_CTRL_SFT_RST_SIG_REG));

	//cfg soft_cold_rst_n , second cfg 1
	val = readl(reg_base + PCIE_CTRL_SFT_RST_SIG_REG);
	val |= (0x1 << PCIE_CTRL_SFT_RST_SIG_COLD_RSTN_BIT);
	writel(val, (reg_base + PCIE_CTRL_SFT_RST_SIG_REG));

	return 0;
}

static void pcie_config_bar0_iatu(struct sophgo_pcie_ep *sg_ep)
{
	void __iomem  *atu_base = sg_ep->atu_base;

	writel((sg_ep->dbi_base_pa & 0xffffffff), (atu_base + 0x114));
	writel(0x6c, (atu_base + 0x118));
	writel(0x0, (atu_base + 0x100));
	writel(0xC0080000, (atu_base + 0x104));
}

void bm1690_pcie_init_link(struct sophgo_pcie_ep *sg_ep)
{
	pr_info("begin c2c ep init\n");

	phy_init(sg_ep->phy);
	pcie_config_soft_phy_reset(sg_ep, PCIE_RST_ASSERT);
	pcie_config_soft_phy_reset(sg_ep, PCIE_RST_DE_ASSERT);
	pcie_config_soft_cold_reset(sg_ep);
	phy_configure(sg_ep->phy, NULL);

	pcie_wait_core_clk(sg_ep);
	pcie_check_radm_status(sg_ep);
	pcie_config_ctrl(sg_ep);
	pcie_config_eq(sg_ep);
	pcie_config_link(sg_ep);

	pcie_config_ep_function(sg_ep);
	pcie_config_axi_route(sg_ep);
	pcie_config_bar0_iatu(sg_ep);
#ifdef PCIE_EP_HUGE_BAR
	if (link_mode == PCIE_CHIPS_C2C_LINK)
		pcie_config_ep_huge_bar(c2c_id, wrapper_id, phy_id, 0);
#endif


	pcie_enable_ltssm(sg_ep);
	pcie_wait_link(sg_ep);
	udelay(200);
	pcie_check_link_status(sg_ep);
}
EXPORT_SYMBOL_GPL(bm1690_pcie_init_link);

int sophgo_pcie_ep_config_cdma_route(struct sophgo_pcie_ep *pcie)
{
	uint32_t tmp;

	tmp = (pcie->pcie_route_config << 28) | (pcie->cdma_pa_start >> 32);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_RCV_ADDR_H32);

	tmp = (pcie->cdma_pa_start & ((1ul << 32) - 1)) >> 16;
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_RCV_ADDR_M16);

	// OS: 2
	tmp = readl(pcie->cdma_reg_base + CDMA_CSR_4) | (1 << CDMA_CSR_RCV_CMD_OS);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_4);

	tmp = (readl(pcie->cdma_reg_base + CDMA_CSR_INTER_DIE_RW) &
		~(0xff << CDMA_CSR_INTER_DIE_WRITE_ADDR_L4)) |
		(pcie->pcie_route_config << CDMA_CSR_INTER_DIE_WRITE_ADDR_H4) |
		(0b0000 << CDMA_CSR_INTER_DIE_WRITE_ADDR_L4);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_INTER_DIE_RW);

	tmp = (readl(pcie->cdma_reg_base + CDMA_CSR_INTRA_DIE_RW) &
		~(0xff << CDMA_CSR_INTRA_DIE_READ_ADDR_L4)) |
		(AXI_RN << CDMA_CSR_INTRA_DIE_READ_ADDR_H4) |
		(0b0000 << CDMA_CSR_INTRA_DIE_READ_ADDR_L4);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_INTRA_DIE_RW);

	return 0;
}
EXPORT_SYMBOL_GPL(sophgo_pcie_ep_config_cdma_route);

#ifndef CONFIG_SOPHGO_TX_MSIX_USED
static int setup_msi_gen(struct sophgo_pcie_ep *sg_ep)
{
	uint64_t socket_id = sg_ep->ep_info.socket_id;
	//uint64_t chip_id = socket_id + 1;
	void __iomem *pcie_ctrl_base = (void __iomem *)sg_ep->ctrl_reg_base;
	void __iomem *pcie_dbi_base = (void __iomem *)sg_ep->dbi_base;
	void __iomem *c2c_top = (void __iomem *)sg_ep->c2c_top_base;
	uint32_t val;
	uint32_t msi_addr;
	uint32_t msi_gen_multi_en = 0;
	uint32_t clr_irq;
	uint32_t msi_data;

	clr_irq = readl(c2c_top + 0x88);
	clr_irq &= ~(1 << 16);
	writel(clr_irq, c2c_top + 0x88);

	// Write to REF control and status register to enable memory and IO accesses
	// Config write TLPs are triggered via AXI writes to region 0 in DUT core axi wrapper
	val = readl(pcie_dbi_base + 0x4);
	val = (val & 0xfffffff8) | 0x7;
	writel(val, (pcie_dbi_base + 0x4));

	// *************************************************************************
	// 0. RC clear all irq
	// *************************************************************************

	//reg_pciex8_1_msi_msix_int_mask, write 1 to clear
	val = 0xffffffff;
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_MASK_IRQ_REG));

	// ****************************************************************************
	// 1. EP configure msi_gen_en enable, then assign msi_lower_addr and msi_upper_addr,
	//    keep msi_user_data all 0.
	// ****************************************************************************

	// config PCI_MSI_ENABLE
	val = readl(pcie_dbi_base + 0x50);
	val = (val & 0xfffeffff) | 0x10000;
	writel(val, (pcie_dbi_base + 0x50));

	// keep msi_user_data all 0
	msi_data = readl(pcie_dbi_base + 0x5c);
	pr_err("msi data from pcie:0x%x\n", msi_data);
	writel(msi_data, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_USER_DATA_REG));

	// First configure msi_gen_en enable
	val = readl(pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_CTRL_REG);
	val |= (0x1 << PCIE_CTRL_AXI_MSI_GEN_CTRL_MSI_GEN_EN_BIT);
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_CTRL_REG));

	if (socket_id == 0) {
		// Second assign msi_lower_addr and msi_upper_addr
		msi_addr = readl(pcie_dbi_base + 0x54);
		writel(msi_addr, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_LOWER_ADDR_REG));
		pr_err("msi_addr:0x%x\n", msi_addr);
		msi_addr = readl(pcie_dbi_base + 0x58);
		writel(msi_addr, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_UPPER_ADDR_REG));
		pr_err("msi_addr:0x%x\n", msi_addr);
	} else {
		msi_addr = readl(pcie_dbi_base + 0x54);
		pr_info("msi low addr = 0x%x\n", msi_addr);
		writel(msi_addr, pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_LOWER_ADDR_REG);

		msi_addr = 0x0;
		msi_addr |= (0x2 << 22); //chip_id
		msi_addr |= (0x7 << 25); //target
		writel(msi_addr, pcie_dbi_base + 0x58);
		pr_info("msi high addr = 0x%x\n", msi_addr);
		writel(msi_addr, pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_UPPER_ADDR_REG);
	}


	// **********************************************************************************
	// 2. EP read MSI control register, and configure msi_gen_multi_msi_en
	// **********************************************************************************
	val = readl(pcie_dbi_base + 0x50);
	msi_gen_multi_en = (val >> 20) & 0x7;

	val = readl(pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_CTRL_REG);
	val &= (~PCIE_CTRL_AXI_MSI_GEN_CTRL_MSI_GEN_MULTI_MSI_MASK);
	val |= (msi_gen_multi_en << 1);
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_CTRL_REG));

	if (msi_gen_multi_en != 0)
		writel(0xff, (c2c_top + C2C_TOP_MSI_GEN_MODE_REG));

	return 0;
}
#else
static void setup_msix_gen(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val = 0;
	uint32_t msix_addr_low = 0;
	uint32_t msix_addr_high = 0;
	uint64_t socket_id = sg_ep->ep_info.socket_id;
	void __iomem *pcie_ctrl_base = (void __iomem *)sg_ep->ctrl_reg_base;
	void __iomem *pcie_dbi_base = (void __iomem *)sg_ep->dbi_base;
	void __iomem *c2c_top = (void __iomem *)sg_ep->c2c_top_base;

	//clear 2nd intr trigger bit
	val = readl(c2c_top + 0x88);
	val &= ~(1 << 16);
	writel(val, (c2c_top + 0x88));

        //init msix high 32bit addr
	msix_addr_high  = 0x3f;

	// Write to REF control and status register to enable memory and IO accesses
	// Config write TLPs are triggered via AXI writes to region 0 in DUT core axi wrapper
	val = readl(pcie_dbi_base + 0x4);
	val = (val & 0xfffffff8) | 0x7;
	writel(val, (pcie_dbi_base + 0x4));

	//reg_pciex8_1_msi_msix_int_mask, write 1 to clear
	val = 0xffffffff;
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_MASK_IRQ_REG));

	// config PCI_MSIX_ENABLE enable
	val = readl(pcie_dbi_base + 0xb0);
	val = (val & 0x7fffffff) | 0x80000000;
	writel(val, (pcie_dbi_base + 0xb0));
	// ********************************************************************************
	// 1. EP configure MSIX_ADDRESS_MATCH_EN enable, then assign MSIX_ADDRESS_MATCH_LOW
	//    and MSIX_ADDRESS_MATCH_HIGH
	// ********************************************************************************
	// First configure MSIX_ADDRESS_MATCH_EN, and assign MSIX_ADDRESS_MATCH_LOW
	val = msix_addr_low | 0x1;
	writel(val, (pcie_dbi_base + 0x940));

	// assign MSIX_ADDRESS_MATCH_HIGH
	val = msix_addr_high;
	writel(val, (pcie_dbi_base + 0x944));

	// ********************************************************************************
	// 2. EP configure msi_gen_en enable, then assign msi_lower_addr and msi_upper_addr,
	//    last configure msi_user_data
	// *********************************************************************************
	// First configure msi_gen_en enable
	val = readl(pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_CTRL_REG);
	val = val | 0x1;
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_CTRL_REG));

	// Second assign msi_lower_addr and msi_upper_addr
	val = msix_addr_low;
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_LOWER_ADDR_REG));

	val = msix_addr_high;
	if (socket_id == 1) {
		val |= (0x2 << 22); //chip_id
		val |= (0x7 << 25); //target
	}
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_UPPER_ADDR_REG));

	// Last configure msi_user_data[TODO]
	if (socket_id == 0)
		val = 0x0;
	else
		val = 0x1 << 24;
	writel(val, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_USER_DATA_REG));

	//multi msi-x
	writel(0xff, (c2c_top + C2C_TOP_MSI_GEN_MODE_REG));

}
#endif
static int setup_msi_info(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t pci_msi_ctrl;
	int i;

	pci_msi_ctrl = readl(sg_ep->dbi_base + PCI_MSI_CAP_ID_NEXT_CTRL_REG);
	pr_err("pci_msi_ctrl = 0x%x\n", pci_msi_ctrl);
	sg_ep->vector_allocated = (pci_msi_ctrl >> PCI_MSI_MULTIPLE_MSG_EN_SHIFT) & PCI_MSI_MULTIPLE_MSG_EN_MASK;
	pr_err("vector allocated:0x%llx\n", sg_ep->vector_allocated);
	sg_ep->vector_allocated = 1 << sg_ep->vector_allocated;
	pr_err("vector allocated:0x%llx\n", sg_ep->vector_allocated);

	if (sg_ep->vector_allocated == 1) {
		for (int i = 0; i < VECTOR_MAX; i++) {
			sg_ep->vector_info[i].vector_index = i;
			sg_ep->vector_info[i].msi_va = sg_ep->c2c_top_base + C2C_TOP_IRQ;
			sg_ep->vector_info[i].msi_data = 0x1;
			sg_ep->vector_info[i].vector_flag = VECTOR_SHARED;
			sg_ep->vector_info[i].share_vector_reg = sg_ep->share_vector_reg;
			pr_err("msi:%d, va:%p, data:0x%llx\n", i, sg_ep->vector_info[i].msi_va,
				sg_ep->vector_info[i].msi_data);
		}
	} else {
		if (sg_ep->vector_allocated > VECTOR_MAX) {
			pr_err("host allocated to many vector, fix to %d\n", VECTOR_MAX);
			sg_ep->vector_allocated = VECTOR_MAX;
		}

		for (i = 0; i < sg_ep->vector_allocated; i++) {
			sg_ep->vector_info[i].vector_index = i;
			sg_ep->vector_info[i].msi_va = sg_ep->c2c_top_base + C2C_TOP_IRQ;
			sg_ep->vector_info[i].msi_data = 0x1 << i;
			sg_ep->vector_info[i].vector_flag = VECTOR_EXCLUSIVE;
			sg_ep->vector_info[i].share_vector_reg = sg_ep->share_vector_reg;
			pr_err("msi:%d, va:%p, data:0x%llx\n", i, sg_ep->vector_info[i].msi_va,
				sg_ep->vector_info[i].msi_data);
		}
		for (; i < VECTOR_MAX; i++) {
			sg_ep->vector_info[i].vector_flag = VECTOR_INVALID;
			sg_ep->vector_info[i].vector_index = i;
		}
	}

	return 0;
}

static int bm1690_set_vector(struct sophgo_pcie_ep *sg_ep)
{
#ifndef CONFIG_SOPHGO_TX_MSIX_USED
	setup_msi_gen(sg_ep);
#else
	setup_msix_gen(sg_ep);
#endif
	setup_msi_info(sg_ep);

	return 0;
}

static int bm1690_reset_vector(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

static inline void sg_pcie_writel_atu(struct sophgo_pcie_ep *sg_ep, uint32_t dir,
					uint32_t index, uint32_t reg, uint32_t val)
{
	void __iomem *base = sg_ep->atu_base + PCIE_ATU_BASE(dir, index);

	writel(val, base + reg);
}

static int prog_inbound_iatu(struct sophgo_pcie_ep *sg_ep, struct iatu *atu)
{
	uint64_t pci_addr = atu->pci_addr;
	uint64_t limit_addr = atu->pci_addr + atu->size - 1;
	uint64_t cpu_addr = atu->cpu_addr;
	uint32_t val = atu->type;
	uint32_t index = atu->index;

	if (atu->match_type == ADDRES_MATCH) {
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_LOWER_BASE,
					lower_32_bits(pci_addr));
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_UPPER_BASE,
					upper_32_bits(pci_addr));

		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_LIMIT,
					lower_32_bits(limit_addr));
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_UPPER_LIMIT,
					upper_32_bits(limit_addr));

		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_LOWER_TARGET,
					lower_32_bits(cpu_addr));
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_UPPER_TARGET,
					upper_32_bits(cpu_addr));

		if (upper_32_bits(limit_addr) > upper_32_bits(pci_addr))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;

		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_REGION_CTRL1, val);
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);
	} else if (atu->match_type == BAR_MATCH) {
		dev_err(sg_ep->dev, "prg inbound iatu %u, func%d bar%d -> 0x%llx\n", index, atu->func,
			atu->bar, cpu_addr);
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_LOWER_TARGET,
			lower_32_bits(cpu_addr));
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_UPPER_TARGET,
			upper_32_bits(cpu_addr));

		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_REGION_CTRL1, val |
			PCIE_ATU_FUNC_NUM(atu->func));
		sg_pcie_writel_atu(sg_ep, ATU_IB, index, PCIE_ATU_REGION_CTRL2,
			PCIE_ATU_ENABLE | PCIE_ATU_FUNC_NUM_MATCH_EN |
			PCIE_ATU_BAR_MODE_ENABLE | (atu->bar << 8));
	} else {
		pr_err("error atu match type:0x%x\n", atu->match_type);
	}

	return 0;
}

static int bm1690ep_set_iatu(struct sophgo_pcie_ep *sg_ep)
{
	int base_index = 32 - (sg_ep->func_num - 1) * 4 - 1;
	int bm1690_dst_chipid_shift = 57;
	int bm1690_funcnum_shift = 54;
	int bar_num = 4;
	uint64_t bar_list[4] = {0, 1, 2, 4};
	uint64_t bar2addr[4] = {0x5, 0x6, 0x7, 0x0};
	uint64_t dst_chip = 2;
	struct iatu atu;
	int i;
	int j;

	atu.match_type = BAR_MATCH;

	for (i = 0; i < sg_ep->func_num; i++) {
		atu.func = i;

		if (i == 0) {
			if (sg_ep->func_num != 1) {
				atu.bar = 4;
				atu.cpu_addr = 0;
				atu.index = base_index;

				prog_inbound_iatu(sg_ep, &atu);
				base_index++;
			}
		} else {
			for (j = 0; j < bar_num; j++) {
				atu.bar = bar_list[j];
				atu.cpu_addr = (dst_chip << bm1690_dst_chipid_shift) |
							(bar2addr[j] << bm1690_funcnum_shift);
				atu.index = base_index;

				prog_inbound_iatu(sg_ep, &atu);
				base_index++;
			}

			dst_chip++;
		}
	}

	return 0;
}

static int bm1690ep_set_c2c_atu(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

static int bm1690e_set_vector(struct sophgo_pcie_ep *sg_ep)
{
#ifndef CONFIG_SOPHGO_TX_MSIX_USED
	setup_msi_gen(sg_ep);
#else
	setup_msix_gen(sg_ep);
#endif
	setup_msi_info(sg_ep);

	return 0;
}

static int bm1690e_reset_vector(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

static int bm1690eep_set_iatu(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

static int bm1690eep_set_c2c_atu(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

int bm1690_ep_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);
	struct device_node *dev_node = dev_of_node(dev);
	struct resource *regs;
	int ret;
	uint64_t start;
	uint64_t size;
	uint64_t clr_irq_pa;

	ret = of_property_read_u64(dev_node, "pcie_id", &sg_ep->ep_info.pcie_id);
	if (ret)
		return pr_err("failed get pcie id\n");
	sprintf(sg_ep->name, "pcie%d", (int)sg_ep->ep_info.pcie_id);

	ret = of_property_read_u64(dev_node, "socket_id", &sg_ep->ep_info.socket_id);
	if (ret)
		return pr_err("failed get socket id\n");

	ret = of_property_read_u64(dev_node, "speed", &sg_ep->speed);
	if (ret)
		return pr_err("failed get speed\n");

	ret = of_property_read_u64(dev_node, "lane_num", &sg_ep->lane_num);
	if (ret)
		return pr_err("failed get lane_num\n");

	ret = of_property_read_u64(dev_node, "func_num", &sg_ep->func_num);
	if (ret)
		return pr_err("failed get func_num\n");

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ctrl");
	if (!regs)
		return pr_err("no config reg find\n");

	sg_ep->sii_base = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!sg_ep->sii_base) {
		pr_err("config base ioremap failed\n");
		goto failed;
	} else {
		sg_ep->sii_base += 0x400;
		sg_ep->ctrl_reg_base = sg_ep->sii_base + 0x800;
	}

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	if (!regs) {
		dev_err(dev, "no dbi reg find\n");
		goto unmap_config;
	}
	sg_ep->dbi_base = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!sg_ep->dbi_base) {
		dev_err(dev, "dbi base ioremap failed\n");
		goto unmap_config;
	}
	sg_ep->dbi_base_pa = regs->start;

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
	if (!regs) {
		dev_err(dev, "no atu reg find\n");
		goto unmap_dbi;
	}

	sg_ep->atu_base = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!sg_ep->atu_base) {
		pr_err("atu base ioremap failed\n");
		goto unmap_dbi;
	}

	ret = of_property_read_u64_index(dev_node, "c2c_top", 0, &start);
	ret = of_property_read_u64_index(dev_node, "c2c_top", 1, &size);
	if (ret) {
		dev_err(dev, "no c2c top find\n");
	} else {
		sg_ep->c2c_top_base = devm_ioremap(dev, start, size);
		if (!sg_ep->c2c_top_base) {
			pr_err("c2c top base ioremap failed\n");
			goto unmap_atu;
		}
	}

	ret = of_property_read_u64_index(dev_node, "c2c_config_range", 0, &sg_ep->c2c_config_base);
	ret = of_property_read_u64_index(dev_node, "c2c_config_range", 1, &sg_ep->c2c_config_size);

	if (sg_ep->ep_info.link_role == PCIE_DATA_LINK_C2C) {

		sg_ep->perst_irqnr = platform_get_irq(pdev, 0);
		if (sg_ep->perst_irqnr < 0) {
			dev_err(dev, "not get mtli irq, so we try gpio irq\n");
			sg_ep->perst_gpio = devm_gpiod_get_index(dev, "perst", 0, GPIOD_IN);
			if (IS_ERR(sg_ep->perst_gpio)) {
				pr_err("failed get perst gpio\n");
				goto unmap_c2c_top;
			}
			gpiod_set_debounce(sg_ep->perst_gpio, 1000);
			sg_ep->perst_irqnr = gpiod_to_irq(sg_ep->perst_gpio);
			if (sg_ep->perst_irqnr < 0) {
				pr_err("failed get pcie%d perst irq nr\n", (int)sg_ep->ep_info.pcie_id);
				goto unmap_c2c_top;
			} else {
				dev_err(dev, "get gpio irq:%d\n", sg_ep->perst_irqnr);
			}
		} else {
			ret = of_property_read_u64_index(dev_node, "clr-irq", 0, &clr_irq_pa);
			ret = of_property_read_u64_index(dev_node, "clr-irq", 1, &sg_ep->clr_irq_data);
			if (ret == 0) {
				sg_ep->clr_irq = ioremap(clr_irq_pa, 0x4);
				dev_err(dev, "get mtli irq:%d, clr irq pa:0x%llx, va:0x%llx\n", sg_ep->perst_irqnr,
					clr_irq_pa, (uint64_t)sg_ep->clr_irq);
			}
		}

		sg_ep->phy = devm_of_phy_get(dev, dev->of_node, "pcie-phy");
	}

	if (device_property_present(dev, "c2c0_x8_1") || device_property_present(dev, "c2c1_x8_1"))
		sg_ep->pcie_route_config = C2C_PCIE_X8_1;
	else if (device_property_present(dev, "c2c0_x8_0") || device_property_present(dev, "c2c1_x8_0"))
		sg_ep->pcie_route_config = C2C_PCIE_X8_0;
	else if (device_property_present(dev, "c2c0_x4_1") || device_property_present(dev, "c2c1_x4_1"))
		sg_ep->pcie_route_config = C2C_PCIE_X4_1;
	else if (device_property_present(dev, "c2c0_x4_0") || device_property_present(dev, "c2c1_x4_0"))
		sg_ep->pcie_route_config = C2C_PCIE_X4_0;
	else if (device_property_present(dev, "cxp_x8"))
		sg_ep->pcie_route_config = CXP_PCIE_X8;
	else if (device_property_present(dev, "cxp_x4"))
		sg_ep->pcie_route_config = CXP_PCIE_X4;
	else
		dev_err(dev, "no pcie type found, this pcie may not support c2c\n");

	ret = of_property_read_u64_index(dev_node, "cdma-reg", 0, &sg_ep->cdma_pa_start);
	ret = of_property_read_u64_index(dev_node, "cdma-reg", 1, &sg_ep->cdma_size);
	if (ret)
		pr_err("cdma reg not found, this pcie is not support c2c\n");
	else {
		sg_ep->cdma_reg_base = devm_ioremap(dev, sg_ep->cdma_pa_start, sg_ep->cdma_size);
		if (!sg_ep->cdma_reg_base) {
			dev_err(dev, "failed to map cdma reg\n");
			goto unmap_c2c_top;
		}

	}

	if (sg_ep->chip_type == CHIP_BM1690) {
		sg_ep->set_iatu = bm1690ep_set_iatu;
		sg_ep->set_c2c_atu = bm1690ep_set_c2c_atu;
		sg_ep->set_vector = bm1690_set_vector;
		sg_ep->reset_vector = bm1690_reset_vector;
	} else if (sg_ep->chip_type == CHIP_BM1690E) {
		sg_ep->set_iatu = bm1690eep_set_iatu;
		sg_ep->set_c2c_atu = bm1690eep_set_c2c_atu;
		sg_ep->set_vector = bm1690e_set_vector;
		sg_ep->reset_vector = bm1690e_reset_vector;
	}

	return 0;
unmap_c2c_top:
	devm_iounmap(dev, sg_ep->c2c_top_base);
unmap_atu:
	devm_iounmap(dev, sg_ep->atu_base);
unmap_dbi:
	devm_iounmap(dev, sg_ep->dbi_base);
unmap_config:
	devm_iounmap(dev, sg_ep->ctrl_reg_base);

failed:
	return -1;
}
EXPORT_SYMBOL_GPL(bm1690_ep_init);
