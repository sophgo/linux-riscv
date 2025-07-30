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
	writel(PCIE_DATA_LINK_C2C_DEVICEID << 16, pcie_dbi_base + SUBSYSTEM_ID_SUBSYTEM_VENDOR_DI_REG);
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

static int get_msi_addr(struct sophgo_pcie_ep *sg_ep, uint64_t *msi_addr, int func_num)
{
	void __iomem *pcie_dbi_base = (void __iomem *)sg_ep->dbi_base;
	int i = 0;
	uint32_t msi_low;
	uint32_t msi_high;

	for (i = 0; i < func_num; i++) {
		msi_low = readl(pcie_dbi_base + 0x54 + i * 0x10000);
		msi_high = readl(pcie_dbi_base + 0x58 + i * 0x10000);

		msi_addr[i] = ((uint64_t)msi_high << 32) | msi_low;
	}

	return 0;
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

static int bm1690e_setup_msi_gen(struct sophgo_pcie_ep *sg_ep)
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
	uint32_t msi_addr_high[4] = {BM1690E_MSI_ADDR(0x71), BM1690E_MSI_ADDR(0x72),
					BM1690E_MSI_ADDR(0x73), BM1690E_MSI_ADDR(0x74)};

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

	msi_addr = readl(pcie_dbi_base + 0x58);
	if (msi_addr != 0x0)
		pr_err("msi high addr is not 0x0,now:0x%x there may be some error in msi gen module\n", msi_addr);

	msi_addr = readl(pcie_dbi_base + 0x54);
	writel(msi_addr, (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_LOWER_ADDR_REG));
	writel(msi_addr_high[socket_id], (pcie_ctrl_base + PCIE_CTRL_AXI_MSI_GEN_UPPER_ADDR_REG));
	pr_err("msi_addr:0x%x_0x%x\n", msi_addr_high[socket_id], msi_addr);

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

static inline uint32_t sg_pcie_readl_atu(struct sophgo_pcie_ep *sg_ep, uint32_t dir,
					uint32_t index, uint32_t reg)
{
	void __iomem *base = sg_ep->atu_base + PCIE_ATU_BASE(dir, index);

	return readl(base + reg);
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
		dev_err(sg_ep->dev, "prg ib iatu%2u, func%d bar%d -> 0x%llx\n", index, atu->func,
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

static int find_available_ob_atu(struct sophgo_pcie_ep *sg_ep)
{
	for (int i = 0; i < 32; i++) {
		uint32_t val = sg_pcie_readl_atu(sg_ep, ATU_OB, i, PCIE_ATU_REGION_CTRL2);
		if (!(val & PCIE_ATU_ENABLE))
			return i;
	}

	return -1;
}

static inline void sg_pcie_writel_ob_atu(struct sophgo_pcie_ep *sg_ep, u32 index, u32 reg,
	u32 val)
{
	sg_pcie_writel_atu(sg_ep, ATU_OB, index, reg, val);
}

static int prog_outbound_iatu(struct sophgo_pcie_ep *sg_ep, int index, uint32_t func, uint64_t cpu_addr,
	uint64_t pci_addr, uint64_t size)
{
	uint32_t val = 0;
	uint64_t limit_addr = 0;
	uint32_t type = PCIE_ATU_TYPE_MEM;

	pr_err("ep config ob %2datu:0x%llx -> 0x%llx, [0x%llx]\n", index, cpu_addr,
		pci_addr, size);

	limit_addr = cpu_addr + size - 1;

	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_LOWER_BASE,
	lower_32_bits(cpu_addr));
	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_UPPER_BASE,
	upper_32_bits(cpu_addr));

	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_LIMIT,
	lower_32_bits(limit_addr));
	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_UPPER_LIMIT,
	upper_32_bits(limit_addr));

	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_LOWER_TARGET,
	lower_32_bits(pci_addr));
	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_UPPER_TARGET,
	upper_32_bits(pci_addr));

	val = type | PCIE_ATU_FUNC_NUM(func);
	if (upper_32_bits(limit_addr) > upper_32_bits(cpu_addr))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;

	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_REGION_CTRL1, val);

	sg_pcie_writel_ob_atu(sg_ep, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);

	return 0;
}

static int bm1690ep_set_ib_iatu(struct sophgo_pcie_ep *sg_ep)
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

static int bm1690ep_set_ob_iatu(struct sophgo_pcie_ep *ep)
{
	void *top_base = ioremap(0x7050000000, 0x1000);
	uint64_t host_ring_buf_addr;
	void *data;
	int ava_atu = 3;

	host_ring_buf_addr = readl(top_base + 0x1fc);
	host_ring_buf_addr = host_ring_buf_addr << 32;
	host_ring_buf_addr |= readl(top_base + 0x1f8);
	pr_err("host ring buffer addr:0x%llx\n", host_ring_buf_addr);

	struct iatu socket0_ob_atu[3] = {
		[0] = {
			.pci_addr = 0x0,
			.size = 0x1ffffffffffff,
			.cpu_addr = 0x600000000000000,
			.type = PCIE_ATU_TYPE_MEM,
			.func = 0,
		},
		[1] = {
			.pci_addr = 0x0,
			.size = 0x1ffffffffffff,
			.cpu_addr = 0xe00000000000000,
			.type = PCIE_ATU_TYPE_MEM,
			.func = 1,
		},
		[2] = {
			.size = 0x3fffff,
			.type = PCIE_ATU_TYPE_MEM,
			.func = 0,
		},
	};

	struct iatu socket1_ob_atu[1] = {
		[0] = {
			.pci_addr = 0xe00000000000000,
			.size = 0x3fffff,
			.type = PCIE_ATU_TYPE_MEM,
			.func = 0,
		},
	};

	if (ep->ep_info.socket_id == 0) {
		socket0_ob_atu[2].pci_addr = host_ring_buf_addr;
		socket0_ob_atu[2].cpu_addr = ep->slv_start_addr;

		for (int i = 0; i < sizeof(socket0_ob_atu) / sizeof(struct iatu); i++) {
			socket0_ob_atu[i].index = ava_atu++;
			if (socket0_ob_atu[i].index < 0) {
				pr_err("index%d no available ob iatu\n", i);
				return -1;
			}
			prog_outbound_iatu(ep, socket0_ob_atu[i].index, socket0_ob_atu[i].func,
					socket0_ob_atu[i].cpu_addr, socket0_ob_atu[i].pci_addr,
					socket0_ob_atu[i].size);
		}
		data = ioremap(0x5800000000, 0x1000);
		writel(0xdead2260, data);
	} else if (ep->ep_info.socket_id == 1) {
		socket1_ob_atu[0].cpu_addr = ep->slv_start_addr;
		socket1_ob_atu[0].pci_addr |= host_ring_buf_addr;

		for (int i = 0; i < sizeof(socket1_ob_atu) / sizeof(struct iatu); i++) {
			socket1_ob_atu[i].index = find_available_ob_atu(ep);
			if (socket1_ob_atu[i].index < 0) {
				pr_err("index%d no available ob iatu\n", i);
				return -1;
			}
			prog_outbound_iatu(ep, socket1_ob_atu[i].index, socket1_ob_atu[i].func,
					socket1_ob_atu[i].cpu_addr, socket1_ob_atu[i].pci_addr,
					socket1_ob_atu[i].size);
		}
		data = ioremap(0x4000000000, 0x1000);
		writel(0xdead2261, data);
	} else {
		pr_err("%s error socket id %llu\n", __func__, ep->ep_info.socket_id);
	}




	return 0;
}

static int bm1690ep_set_iatu_ib(struct sophgo_pcie_ep *sg_ep)
{
	bm1690ep_set_ib_iatu(sg_ep);

	return 0;
}

static int bm1690ep_set_iatu_ob(struct sophgo_pcie_ep *sg_ep)
{
	bm1690ep_set_ob_iatu(sg_ep);

	return 0;
}

static int bm1690ep_set_c2c_atu(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

static int bm1690e_set_vector(struct sophgo_pcie_ep *sg_ep)
{

	bm1690e_setup_msi_gen(sg_ep);

	setup_msi_info(sg_ep);

	return 0;
}

static int bm1690e_reset_vector(struct sophgo_pcie_ep *sg_ep)
{

	return 0;
}

static int bm1690eep_set_ib_iatu(struct sophgo_pcie_ep *sg_ep)
{
	int base_index = 32 - (sg_ep->func_num * 4);
	uint64_t bar_list[3] = {0, 1, 4};
	struct iatu atu;
	uint64_t barid = 0;
	uint64_t chipid;
	int func;
	int j;

	atu.match_type = BAR_MATCH;

	chipid = sg_ep->ep_info.socket_id;
	barid = 3 * chipid;
	pr_err("chipid:0x%llx, barid:0x%llx\n", chipid, barid);

	for (func = 0; func < sg_ep->func_num; func++) {
		atu.func = func;
		chipid++;

		for (j = 0; j < sizeof(bar_list) / sizeof(uint64_t); j++) {
			if ((func == 0 && bar_list[j] == 0) || (func == 0 && bar_list[j] == 1)) {
				base_index++;
				barid++;
				continue;
			}

			atu.bar = bar_list[j];

			atu.cpu_addr = BM1690E_PCIE_FMT_BOARDID(sg_ep->board_id) |
					BM1690E_PCIE_FMT_CHIPID(chipid) |
					BM1690E_PCIE_FMT_BARID(barid);
			atu.index = base_index;

			prog_inbound_iatu(sg_ep, &atu);
			base_index++;
			barid++;
		}

	}

	return 0;
}

static int bm1690eep_set_iatu_ib(struct sophgo_pcie_ep *sg_ep)
{
	bm1690eep_set_ib_iatu(sg_ep);

	return 0;
}

static int bm1690eep_set_ob_iatu(struct sophgo_pcie_ep *sg_ep)
{
	uint64_t msi_addr[4];
	uint64_t msi_addr_size = (1 << 12);
	uint64_t msi_addr_mask = ~((1UL << 12) - 1);
	uint64_t i;


	get_msi_addr(sg_ep, msi_addr, sg_ep->func_num);

	for (i = 0; i < sg_ep->func_num; i++) {
		uint32_t index = find_available_ob_atu(sg_ep);
		if (index < 0) {
			pr_err("no available ob iatu for func %lld\n", i);
			return -1;
		}

		prog_outbound_iatu(sg_ep, index, i, ((BM1690E_SOC_MSI_ADDR_FUNC_NUM(i)) | (msi_addr[i] & msi_addr_mask)),
				   msi_addr[i] & msi_addr_mask, msi_addr_size);
	}

	return 0;
}

static int bm1690eep_set_iatu_ob(struct sophgo_pcie_ep *sg_ep)
{
	if (sg_ep->ep_info.socket_id != 0) {
		pr_err("only socket0 need config c2c atu, now socket id is 0x%llx\n",
			sg_ep->ep_info.socket_id);
		return 0;
	}

	bm1690eep_set_ob_iatu(sg_ep);

	return 0;
}

static void config_obatu_delay_func(struct work_struct *p_work)
{
	struct sophgo_pcie_ep *sg_ep = container_of(p_work, struct sophgo_pcie_ep, probe_delayed_work.work);
	void *top_base = ioremap(0x7050000000, 0x1000);
	uint32_t status;
	uint32_t check_loop = 0;

	while (1) {
		status = readl(top_base + 0x1c4);
		if (status == 0x6) {
			if (sg_ep->set_c2c_ob_atu)
				sg_ep->set_c2c_ob_atu(sg_ep);
			if (sg_ep->set_ob_iatu)
				sg_ep->set_ob_iatu(sg_ep);

			return;
		}

		msleep(10);
		check_loop++;
		if (check_loop % 100 == 0) {
			pr_err("wait for top status:0x%x\n", status);
		}
	}
}

static int bm1690eep_set_quirks(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t val;

	val = readl(sg_ep->c2c_top_base + PCIE_CACHE_CTRL);
	val |= (0x1 << 4);
	writel(val, sg_ep->c2c_top_base + PCIE_CACHE_CTRL);

	INIT_DELAYED_WORK(&sg_ep->probe_delayed_work, config_obatu_delay_func);

	if (sg_ep->ep_info.socket_id == 0) {
		schedule_delayed_work(&sg_ep->probe_delayed_work, 1);
	}

	return 0;
}

static void pcie_clear_slv_mapping(struct sophgo_pcie_ep *pcie)
{
	void __iomem  *ctrl_reg_base = pcie->ctrl_reg_base;

	writel(0x0, (ctrl_reg_base + PCIE_CTRL_REMAPPING_EN_REG));
	writel(0x0, (ctrl_reg_base + PCIE_CTRL_SN_UP_START_ADDR_REG));
	writel(0x0, (ctrl_reg_base + PCIE_CTRL_SN_UP_END_ADDR_REG));
	writel(0x0, (ctrl_reg_base + PCIE_CTRL_SN_DW_ADDR_REG));
}

static void pcie_config_slv_mapping(struct sophgo_pcie_ep *pcie)
{
	uint32_t val = 0;
	void __iomem  *ctrl_reg_base = pcie->ctrl_reg_base;
	uint64_t up_start_addr = 0;
	uint64_t up_end_addr = 0;

	up_start_addr = pcie->slv_start_addr;
	up_end_addr = pcie->slv_end_addr;

	val = readl(ctrl_reg_base + PCIE_CTRL_REMAPPING_EN_REG);
	val |= 0x3 << PCIE_CTRL_REMAP_EN_SN_TO_PCIE_UP4G_EN_BIT;
	writel(val, (ctrl_reg_base + PCIE_CTRL_REMAPPING_EN_REG));

	up_start_addr = up_start_addr >> 16;
	writel((up_start_addr & 0xffffffff), (ctrl_reg_base + PCIE_CTRL_SN_UP_START_ADDR_REG));
	up_end_addr = up_end_addr >> 16;
	writel((up_end_addr & 0xffffffff), (ctrl_reg_base + PCIE_CTRL_SN_UP_END_ADDR_REG));


	pr_err("config slv mapping\n");
}

static int bm1690ep_set_quirks(struct sophgo_pcie_ep *sg_ep)
{
	pcie_clear_slv_mapping(sg_ep);
	pcie_config_slv_mapping(sg_ep);
	return sg_ep->set_ob_iatu(sg_ep);
}

static inline void *get_c2c_ibatu(struct sophgo_pcie_ep *sg_ep, uint32_t index)
{
	return sg_ep->c2c_top_base + C2C_IBATU(index);
}

static void prog_c2c_ibatu(struct sophgo_pcie_ep *sg_ep, uint32_t index,
		uint64_t match_addr, uint64_t out_addr, uint64_t ib_size)
{
	void *ib_atu = get_c2c_ibatu(sg_ep, index);
	uint32_t boardid = BM1690E_PCIE_FMT_GET_BOARDID(out_addr);
	uint32_t chipid = BM1690E_PCIE_FMT_GET_CHIPID(out_addr);
	uint32_t func = BM1690E_PCIE_FMT_GET_FUNC(out_addr);
	uint32_t msi = BM1690E_PCIE_FMT_GET_MSI(out_addr);
	uint32_t atu_ctrl = 0;

	writel(lower_32_bits(match_addr), ib_atu + C2C_IBATU_LOWER_BASE);
	writel(upper_32_bits(match_addr), ib_atu + C2C_IBATU_UPPER_BASE);

	atu_ctrl = C2C_IBATU_CTRL_SELX8(1) |
		C2C_IBATU_CTRL_BOARDID(boardid) |
		C2C_IBATU_CTRL_CHIPID(chipid) |
		C2C_IBATU_CTRL_FUNC(func) |
		C2C_IBATU_CTRL_MSI(msi) |
		C2C_IBATU_CTRL_IBSIZE(ib_size) |
		C2C_IBATU_CTRL_ENABLE;
	writel(atu_ctrl, ib_atu + C2C_IBATU_CTRL);

	pr_err("c2c ibatu %d:0x%llx -> 0x%llx, ctrl:0x%x\n", index, match_addr,
		out_addr, atu_ctrl);
}

static int bm1690eep_set_c2c_ib_atu(struct sophgo_pcie_ep *sg_ep)
{
	uint64_t bar_list[3] = {0, 1, 4};
	uint64_t barid = 0;
	uint64_t c2c_iatu_index = 0;
	uint64_t addr;
	uint64_t chipid;
	int func;
	int j;

	for (func = 0; func < sg_ep->func_num; func++) {
		for (j = 0; j < sizeof(bar_list) / sizeof(uint64_t); j++) {
			if (bar_list[j] == 4) {
				chipid = func + 1;
				addr = BM1690E_PCIE_FMT_BOARDID(sg_ep->board_id) |
					BM1690E_PCIE_FMT_CHIPID(chipid) |
					BM1690E_PCIE_FMT_BARID(barid);
				prog_c2c_ibatu(sg_ep, c2c_iatu_index, addr, addr, 36);
				c2c_iatu_index++;
			}

			barid++;
		}
	}

	return 0;
}

static int bm1690eep_set_c2c_atu_ib(struct sophgo_pcie_ep *sg_ep)
{
	if (sg_ep->ep_info.socket_id != 0) {
		pr_err("only socket0 need config c2c atu, now socket id is 0x%llx\n",
			sg_ep->ep_info.socket_id);
		return 0;
	}

	bm1690eep_set_c2c_ib_atu(sg_ep);

	return 0;
}

static inline void *get_c2c_obatu(struct sophgo_pcie_ep *sg_ep, uint32_t index)
{
	return sg_ep->c2c_top_base + C2C_OBATU(index);
}

static void prog_c2c_obatu(struct sophgo_pcie_ep *sg_ep, uint32_t index, uint32_t dst_pc,
			   uint32_t pc_msi, uint64_t match_addr, uint64_t out_addr, uint64_t ob_size)
{
	void *ob_atu = get_c2c_obatu(sg_ep, index);
	uint32_t atu_ctrl = 0;

	writel(lower_32_bits(out_addr), ob_atu + C2C_OBATU_LOWER_BASE);
	writel(upper_32_bits(out_addr), ob_atu + C2C_OBATU_UPPER_BASE);

	atu_ctrl = C2C_OBATU_CTRL_ENABLE |
		C2C_OBATU_DST_PC(dst_pc) |
		C2C_OBATU_PC_MSI(pc_msi) |
		C2C_OBATU_SELX8(1) |
		C2C_OBATU_OB_SIZE(ob_size) |
		C2C_OBATU_MATCH_ADDR(match_addr);
	writel(atu_ctrl, ob_atu + C2C_OBATU_CTRL);

	pr_err("c2c obatu %d:0x%llx -> 0x%llx, ob_size:0x%llx, ctrl:0x%x\n", index, match_addr, out_addr, ob_size, atu_ctrl);
}

static int bm1690eep_set_c2c_ob_atu(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t msi_obatu_index = 128 + 28;
	uint64_t match_addr[4] = {0x7100000000,
				0x7200000000,
				0x7300000000,
				0x7400000000};
	uint64_t pc_msi_addr[4];
	uint64_t ob_size = 32;
	uint64_t out_addr_mask = ~((1UL << 32) - 1);

	get_msi_addr(sg_ep, pc_msi_addr, sg_ep->func_num);
	for (uint64_t i = 0; i < sizeof(match_addr) / sizeof(uint64_t); i++) {
		prog_c2c_obatu(sg_ep, msi_obatu_index, 0x1, 0x1, match_addr[i],
				(BM1690E_SOC_MSI_ADDR_FUNC_NUM(i)) | (pc_msi_addr[i] & out_addr_mask), ob_size);
		msi_obatu_index++;
	}


	return 0;
}

static int bm1690eep_set_c2c_atu_ob(struct sophgo_pcie_ep *sg_ep)
{
	if (sg_ep->ep_info.socket_id != 0) {
		pr_err("only socket0 need config c2c atu, now socket id is 0x%llx\n",
			sg_ep->ep_info.socket_id);
		return 0;
	}

	bm1690eep_set_c2c_ob_atu(sg_ep);

	return 0;
}

static inline void *get_recoder_addr(struct sophgo_pcie_ep *sg_ep, uint32_t dir, uint32_t index)
{
	if (dir == OB_RECODER)
		return sg_ep->c2c_top_base + OB_RECODER_ADDR(index);
	else if (dir == IB_RECODER)
		return sg_ep->c2c_top_base + IB_RECODER_ADDR(index);
	else
		return NULL;
}

static inline void *get_recoder_en(struct sophgo_pcie_ep *sg_ep, uint32_t dir)
{
	if (dir == OB_RECODER)
		return sg_ep->c2c_top_base + 0x1100;
	else if (dir == IB_RECODER)
		return sg_ep->c2c_top_base + 0x1104;
	else
		return NULL;
}

static int find_availeble_recoder(struct sophgo_pcie_ep *sg_ep, uint32_t dir)
{
	uint32_t recoder_en_val = readl(get_recoder_en(sg_ep, dir));

	for (int i = 0; i < 32; i++) {
		if (!(recoder_en_val & (1 << i)))
			return i;
	}

	return -1;
}

static int prog_recoder(struct sophgo_pcie_ep *sg_ep, uint32_t dir, uint32_t index,
		uint64_t match_addr, uint64_t out_addr, uint64_t size)
{
	void *recoder = get_recoder_addr(sg_ep, dir, index);
	void *recoder_en = get_recoder_en(sg_ep, dir);
	uint32_t mask_size = lower_32_bits(((size - 1) >> 12));
	uint32_t recoder_en_val = readl(recoder_en);
	uint64_t addr;
	uint32_t barid;
	uint32_t st_addr;
	uint32_t recode_addr;

	barid = BM1690E_PCIE_FMT_BARID(match_addr);
	addr = PCIE_FMT_TO_RECODER_ADDR(match_addr);
	st_addr = RECODER_FMT_CFG(barid, addr);

	barid = BM1690E_PCIE_FMT_BARID(out_addr);
	addr = PCIE_FMT_TO_RECODER_ADDR(out_addr);
	recode_addr = RECODER_FMT_CFG(barid, addr);

	writel(st_addr, recoder + IB_RECODER_ST_ADDR);
	writel(recode_addr, recoder + IB_RECODER_RECODE_ADDR);
	writel(mask_size, recoder + IB_RECODER_RECODE_ADDR);


	recoder_en_val |= (1 << index);
	writel(recoder_en_val, recoder_en);
	pr_err("ib recoder %d:0x%x -> 0x%x, mask_size:0x%x\n", index, st_addr,
		recode_addr, mask_size);

	return 0;
}


static int bm1690eep_set_ib_recoder(struct sophgo_pcie_ep *sg_ep)
{
	uint64_t bar_list[3] = {0, 1, 4};
	uint64_t barid = 0;
	uint64_t recoder_index;
	uint64_t addr;
	uint64_t chipid;
	int func;
	int j;
	struct recoder_addr recoder_addr[3] = {
		[0] = {
			.match_addr = 0x0000000000,
			.out_addr = BM1690E_L2M_BASE_ADDR,
			.mask_size = 4000000,
		},
		[1] = {
			.match_addr = 0x0004000000,
			.out_addr = BM1690E_MSG_BASE_ADDR,
			.mask_size = 4000000,
		},
		[2] = {
			.match_addr = 0x0008000000,
			.out_addr = BM1690E_MTLI_BASE_ADDR,
			.mask_size = 4000000,
		},
	};

	for (func = 0; func < sg_ep->func_num; func++) {
		for (j = 0; j < sizeof(bar_list) / sizeof(uint64_t); j++) {
			if (bar_list[j] == 4) {
				chipid = func + 1;
				addr = BM1690E_PCIE_FMT_BOARDID(sg_ep->board_id) |
					BM1690E_PCIE_FMT_CHIPID(chipid) |
					BM1690E_PCIE_FMT_BARID(barid);
				for (int i = 0; i < sizeof(recoder_addr) / sizeof(struct recoder_addr); i++) {
					recoder_index = find_availeble_recoder(sg_ep, IB_RECODER);
					if (recoder_index < 0) {
						pr_err("func%d no available recoder\n", func);
						return -1;
					}

					prog_recoder(sg_ep, IB_RECODER, recoder_index,
						addr | recoder_addr[i].match_addr,
						addr | recoder_addr[i].out_addr,
						recoder_addr[i].mask_size);
				}
			}

			barid++;
		}
	}

	return 0;
}

static int bm1690eep_set_ob_recoder(struct sophgo_pcie_ep *sg_ep)
{
	uint64_t barid;
	uint64_t addr;
	int recoder_index;

	struct recoder_addr recoder_addr[3] = {
		[0] = {
			.match_addr = BM1690E_L2M_BASE_ADDR,
			.out_addr = 0x0000000000,
			.mask_size = 4000000,
		},
		[1] = {
			.match_addr = BM1690E_MSG_BASE_ADDR,
			.out_addr = 0x0004000000,
			.mask_size = 4000000,
		},
		[2] = {
			.match_addr = BM1690E_MTLI_BASE_ADDR,
			.out_addr = 0x0008000000,
			.mask_size = 4000000,
		},
	};

	// for upstream, all chip pcie fmt address [func_num = 0, msi = 0], so barid = 0
	// board_id, chipid are not matched by recoder module, so we dont care board_id and chipid
	barid = 0;
	addr = BM1690E_PCIE_FMT_BARID(barid);

	for (int i = 0; i < sizeof(recoder_addr) / sizeof(struct recoder_addr); i++) {
		recoder_index = find_availeble_recoder(sg_ep, OB_RECODER);
		if (recoder_index < 0) {
			pr_err("no available ob recoder\n");
			return -1;
		}

		prog_recoder(sg_ep, OB_RECODER, recoder_index,
			addr | recoder_addr[i].match_addr,
			addr | recoder_addr[i].out_addr,
			recoder_addr[i].mask_size);
	}

	return 0;
}

static int bm1690eep_set_recoder(struct sophgo_pcie_ep *sg_ep)
{
	if (sg_ep->ep_info.socket_id != 0) {
		pr_err("only socket0 need config recoder, now socket id is 0x%llx\n",
			sg_ep->ep_info.socket_id);
		return 0;
	}

	bm1690eep_set_ib_recoder(sg_ep);
	bm1690eep_set_ob_recoder(sg_ep);

	return 0;
}

static inline void *get_portcode_addr(struct sophgo_pcie_ep *sg_ep)
{
	return sg_ep->c2c_top_base;
}

static int bm1690eep_set_portcode(struct sophgo_pcie_ep *sg_ep)
{
	void *portcode = get_portcode_addr(sg_ep);
	uint32_t portcode_val = 0;
	__attribute__((unused)) uint32_t pld_portcode_route_bits[4] = {
		0x5ddd0,
		0x51105,
		0x5d055,
		0x50555,
	};
	uint32_t portcode_route_bits[4] = {
		0x5ccc0,
		0x51105,
		0x5c055,
		0x50555,
	};

	portcode_val = ((sg_ep->board_size << PORTCODE_BOARDSIZE_SHIFT) |
			(sg_ep->board_id << PORTCODE_BOARDID_SHIFT) |
			portcode_route_bits[sg_ep->ep_info.socket_id]);

	writel(portcode_val, portcode);
	pr_err("portcode:0x%x, board_size:0x%llx, board_id:0x%llx, route_bits:0x%x\n",
		portcode_val, sg_ep->board_size, sg_ep->board_id,
		portcode_route_bits[sg_ep->ep_info.socket_id]);

	return 0;
}

static inline void *get_wr_order_addr(struct sophgo_pcie_ep *sg_ep, uint32_t index)
{
	return sg_ep->c2c_top_base + 0x1120 + (index * 0x10);
}

static inline void *get_wr_order_en_addr(struct sophgo_pcie_ep *sg_ep)
{
	return sg_ep->c2c_top_base + 0x1320;
}

static inline void *get_wr_order_mode_addr(struct sophgo_pcie_ep *sg_ep)
{
	return sg_ep->c2c_top_base + 0x1324;
}

static inline int find_avaliable_wr_order(struct sophgo_pcie_ep *sg_ep)
{
	uint32_t wr_order_en_val = readl(get_wr_order_en_addr(sg_ep));

	for (int i = 0; i < 32; i++) {
		if (!(wr_order_en_val & (1 << i)))
			return i;
	}

	return -1;
}

static inline void enable_wr_order(struct sophgo_pcie_ep *sg_ep, uint32_t index)
{
	uint32_t wr_order_en_val = readl(get_wr_order_en_addr(sg_ep));

	wr_order_en_val |= (1 << index);
	writel(wr_order_en_val, get_wr_order_en_addr(sg_ep));
}

static inline void set_wr_order_mode(struct sophgo_pcie_ep *sg_ep, uint32_t index, uint32_t mode)
{
	void *wr_order_mode_addr = get_wr_order_mode_addr(sg_ep);
	uint32_t wr_order_mode_val;
	uint32_t atu_index = index % 16;

	wr_order_mode_addr = index < 16 ?  wr_order_mode_addr + 0x0 : wr_order_mode_addr + 0x4;
	wr_order_mode_val = readl(wr_order_mode_addr);
	wr_order_mode_val &= ~(0x3 << (atu_index * 2));
	wr_order_mode_val |= (mode << (atu_index * 2));
	writel(wr_order_mode_val, wr_order_mode_addr);
}

static int prog_wr_order(struct sophgo_pcie_ep *sg_ep, uint32_t index, uint32_t mode,
	uint64_t barid, uint64_t start_addr, uint64_t size)
{
	void *wr_order = get_wr_order_addr(sg_ep, index);
	uint64_t end_addr;

	if (mode == WR_ORDER_CHIP_MODE)
		start_addr |= (barid << 40);

	end_addr = start_addr + size - 1;

	writel(lower_32_bits(start_addr), wr_order + WR_ORDER_START_LOWER);
	writel(upper_32_bits(start_addr), wr_order + WR_ORDER_START_UPPER);
	writel(lower_32_bits(end_addr), wr_order + WR_ORDER_END_LOWER);
	writel(upper_32_bits(end_addr), wr_order + WR_ORDER_END_UPPER);

	set_wr_order_mode(sg_ep, index, mode);
	enable_wr_order(sg_ep, index);

	pr_err("wr_order %d:0x%llx -> 0x%llx, mode:0x%x\n", index, start_addr,
		end_addr, mode);

	return 0;
}

static int set_bar4_wr_order(struct sophgo_pcie_ep *sg_ep, uint32_t barid)
{
	int wr_order_index;

	struct wr_order_list wr_order[] = {
		[0] = {
			.start_addr = 0x6c00000000,
			.size = 0x1000000,
		},
		[1] = {
			.start_addr = 0x6e00000000,
			.size = 0x1000000,
		},
	};

	for (int i = 0; i < sizeof(wr_order) / sizeof(struct wr_order_list); i++) {
		wr_order_index = find_avaliable_wr_order(sg_ep);
		if (wr_order_index < 0) {
			pr_err("barid%d no available wr order\n", barid);
			return -1;
		}
		prog_wr_order(sg_ep, wr_order_index, WR_ORDER_CHIP_MODE,
			barid, wr_order[i].start_addr, wr_order[i].size);
	}

	return 0;
}

static int bm1690eep_set_wr_order(struct sophgo_pcie_ep *sg_ep)
{
	int wr_order_index;
	int barid = sg_ep->ep_info.socket_id * 3 + 2;

	for (int i = 0; i < sg_ep->func_num; i++) {
		if (i == 0) {
			wr_order_index = find_avaliable_wr_order(sg_ep);
			prog_wr_order(sg_ep, wr_order_index, WR_ORDER_ALL_ADDR_MODE, 0, DBI_AXI_ADDR, 0x200000);

			wr_order_index = find_avaliable_wr_order(sg_ep);
			prog_wr_order(sg_ep, wr_order_index, WR_ORDER_CHIP_MODE, 0, SOC_CONFIG_ADDR, SOC_CONFIG_SIZE);

			set_bar4_wr_order(sg_ep, barid++);
		} else {
			wr_order_index = find_avaliable_wr_order(sg_ep);
			prog_wr_order(sg_ep, wr_order_index, WR_ORDER_CHIP_MODE, barid++, 0x0, BAR0_SIZE);

			wr_order_index = find_avaliable_wr_order(sg_ep);
			prog_wr_order(sg_ep, wr_order_index, WR_ORDER_CHIP_MODE, barid++, 0x0, BAR1_SIZE);

			set_bar4_wr_order(sg_ep, barid++);
		}
	}

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

	ret = of_property_read_u64(dev_node, "global_chipid", &sg_ep->global_chipid);
	if (ret)
		return pr_err("failed get board_id\n");

	ret = of_property_read_u64(dev_node, "board_size", &sg_ep->board_size);
	if (ret)
		return pr_err("failed get board_size\n");

	if (sg_ep->board_size != 0)
		sg_ep->board_id = sg_ep->global_chipid / sg_ep->board_size;

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

	ret = of_property_read_u64_index(dev_node, "slv_range", 0, &sg_ep->slv_start_addr);
	ret = of_property_read_u64_index(dev_node, "slv_range", 1, &sg_ep->slv_end_addr);
	if (ret == 0)
		dev_err(dev, "slv[0x%llx-0x%llx]\n", sg_ep->slv_start_addr, sg_ep->slv_end_addr);
	else
		dev_err(dev, "dev failed get slv range\n");

	if (sg_ep->chip_type == CHIP_BM1690) {
		sg_ep->set_ib_iatu = bm1690ep_set_iatu_ib;
		sg_ep->set_ob_iatu = bm1690ep_set_iatu_ob;
		sg_ep->set_c2c_ib_atu = bm1690ep_set_c2c_atu;
		sg_ep->set_vector = bm1690_set_vector;
		sg_ep->reset_vector = bm1690_reset_vector;
		sg_ep->set_quirks = bm1690ep_set_quirks;
	} else if (sg_ep->chip_type == CHIP_BM1690E) {
		sg_ep->set_quirks = bm1690eep_set_quirks;
		sg_ep->set_ib_iatu = bm1690eep_set_iatu_ib;
		sg_ep->set_c2c_ib_atu = bm1690eep_set_c2c_atu_ib;
		sg_ep->set_ob_iatu = bm1690eep_set_iatu_ob;
		sg_ep->set_c2c_ob_atu = bm1690eep_set_c2c_atu_ob;
		sg_ep->set_recoder = bm1690eep_set_recoder;
		sg_ep->set_portcode = bm1690eep_set_portcode;
		sg_ep->set_wr_order = bm1690eep_set_wr_order;
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
