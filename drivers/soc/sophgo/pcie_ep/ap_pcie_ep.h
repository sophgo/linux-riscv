/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __AP_PCIE_EP__
#define __AP_PCIE_EP__

#define PCIE_DISABLE	0
#define PCIE_ENABLE	1

#define PCIE_DATA_LINK_PCIE	0
#define PCIE_DATA_LINK_C2C	1

#define PCIE_PHY_X8	0
#define PCIE_PHY_X44	1

#define PCIE_VFUN_MAX	2
#define PCIE_VFUN_SGCARD	0
#define PCIE_VFUN_VETH	1
#define VECTOR_MAX	4

enum {
	CHIP_BM1684X = 0,
	CHIP_BM1690,
	CHIP_BM1690E,
};

struct pcie_info {
	uint64_t slot_id;
	uint64_t socket_id;
	uint64_t peer_socketid;
	uint64_t pcie_id;
	uint64_t enable;
	uint64_t data_link_type;
	uint64_t link_role;
	uint64_t link_role_gpio;
	uint64_t perst_gpio;
	uint64_t phy_role;
};

struct msi_info {
	uint64_t phy_addr;
	void __iomem *msi_va;
	uint64_t msi_data;
	int irq_nr;
};

#define VECTOR_INVALID	0
#define VECTOR_EXCLUSIVE	1
#define VECTOR_SHARED	2

struct vector_info {
	int32_t vector_flag;
	uint32_t vector_index;
	void __iomem *msi_va;
	uint64_t phy_addr;
	uint64_t msi_data;
	void __iomem *share_vector_reg;
	int allocated_num;
};

struct sophgo_pcie_vfun {
	struct vector_info *vector;
	struct msi_info rcv_msi_info;
};

struct sophgo_pcie_ep {
	struct list_head pcie_ep_list;
	struct device *dev;
	struct delayed_work link_work;
	int chip_type;
	void __iomem *ctrl_reg_base;
	void __iomem *atu_base;
	void __iomem *dbi_base;
	uint64_t dbi_base_pa;
	void __iomem *c2c_top_base;
	void __iomem *sii_base;
	void __iomem *share_vector_reg;
	void __iomem *clr_irq;
	void __iomem *cdma_reg_base;
	uint64_t cdma_pa_start;
	uint64_t cdma_size;
	uint64_t clr_irq_data;
	struct phy *phy;
	uint64_t c2c_config_base;
	uint64_t c2c_config_size;
	uint32_t pcie_route_config;
	uint64_t global_chipid;
	uint64_t board_size;
	uint64_t board_id;
	char name[32];
	struct gpio_desc *perst_gpio;
	int perst_irqnr;
	struct pcie_info ep_info;
	uint64_t vector_allocated;
	uint64_t speed;
	uint64_t lane_num;
	uint64_t func_num;
	uint64_t host_ddr_addr;
	uint64_t slv_start_addr;
	uint64_t slv_end_addr;
	int c2c_enable;
	struct vector_info vector_info[VECTOR_MAX];
	struct sophgo_pcie_vfun vfun[PCIE_VFUN_MAX];
	int (*set_vector)(struct sophgo_pcie_ep *ep);
	int (*reset_vector)(struct sophgo_pcie_ep *ep);
	int (*set_ib_iatu)(struct sophgo_pcie_ep *sg_ep);
	int (*set_c2c_ib_atu)(struct sophgo_pcie_ep *sg_ep);
	int (*set_ob_iatu)(struct sophgo_pcie_ep *sg_ep);
	int (*set_c2c_ob_atu)(struct sophgo_pcie_ep *sg_ep);
	int (*set_recoder)(struct sophgo_pcie_ep *sg_ep);
	int (*set_portcode)(struct sophgo_pcie_ep *sg_ep);
	int (*set_wr_order)(struct sophgo_pcie_ep *sg_ep);
	int (*set_quirks)(struct sophgo_pcie_ep *sg_ep);
	struct delayed_work probe_delayed_work;
};


enum soc_work_mode {
	SOC_WORK_MODE_SERVER = 0,
	SOC_WORK_MODE_AI,
	SOC_WORK_MODE_BUTT
};


#define GENMASK_32(h, l) \
	(((0xFFFFFFFF) << (l)) & (0xFFFFFFFF >> (32UL - 1 - (h))))

#define PCIE_SII_GENERAL_CTRL1_DEVICE_TYPE_MASK             GENMASK_32(12, 9)
#define PCIE_CTRL_AXI_MSI_GEN_CTRL_MSI_GEN_MULTI_MSI_MASK   GENMASK_32(3, 1)


struct vector_info *sophgo_ep_alloc_vector(int pcie_id, int vector_id);
int bm1684x_ep_int(struct platform_device *pdev);
int bm1690_ep_init(struct platform_device *pdev);
void bm1690_pcie_init_link(struct sophgo_pcie_ep *sg_ep);
int sophgo_pcie_ep_config_cdma_route(struct sophgo_pcie_ep *pcie);

#endif
