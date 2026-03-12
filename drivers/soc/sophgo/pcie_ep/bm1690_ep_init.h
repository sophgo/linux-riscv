/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __BM1690_EP_INIT_H__
#define __BM1690_EP_INIT_H__


#define PCI_MSI_CAP_ID_NEXT_CTRL_REG	0x50
#define PCI_MSI_MULTIPLE_MSG_EN_SHIFT	20
#define PCI_MSI_MULTIPLE_MSG_EN_WIDTH	3
#define PCI_MSI_MULTIPLE_MSG_EN_MASK	((1 << PCI_MSI_MULTIPLE_MSG_EN_WIDTH) - 1)
#define C2C_TOP_IRQ	0x58

//PCIE CTRL REG
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

#define PCIE_SII_GENERAL_CTRL1_REG                          0x050
#define PCIE_SII_GENERAL_CTRL3_REG                          0x058

#define PCIE_CTRL_SFT_RST_SIG_COLD_RSTN_BIT                 0
#define PCIE_CTRL_SFT_RST_SIG_PHY_RSTN_BIT                  1
#define PCIE_CTRL_SFT_RST_SIG_WARM_RSTN_BIT                 2
#define PCIE_CTRL_REMAP_EN_HNI_TO_PCIE_UP4G_EN_BIT          0
#define PCIE_CTRL_REMAP_EN_HNI_TO_PCIE_DW4G_EN_BIT          1
#define PCIE_CTRL_REMAP_EN_SN_TO_PCIE_UP4G_EN_BIT           2
#define PCIE_CTRL_REMAP_EN_SN_TO_PCIE_DW4G_EN_BIT           3
#define PCIE_CTRL_AXI_MSI_GEN_CTRL_MSI_GEN_EN_BIT           0

#define C2C_TOP_MSI_GEN_MODE_REG                            0xd8

#define C2C_PCIE_DBI2_OFFSET                    0x100000
#define SUBSYSTEM_ID_SUBSYTEM_VENDOR_DI_REG	0x2c



#define PCIE_DATA_LINK_PCIE	0
#define PCIE_DATA_LINK_C2C	1
#define PCIE_DATA_LINK_C2C_DEVICEID	0xc2c

#define PCIE_ATU_REGION_CTRL1		0x000

#define PCIE_ATU_REGION_CTRL2		0x004
#define PCIE_ATU_ENABLE			BIT(31)
#define PCIE_ATU_BAR_MODE_ENABLE	BIT(30)
#define PCIE_ATU_INHIBIT_PAYLOAD	BIT(22)
#define PCIE_ATU_FUNC_NUM_MATCH_EN      BIT(19)

#define PCIE_ATU_LOWER_BASE		0x008
#define PCIE_ATU_UPPER_BASE		0x00C
#define PCIE_ATU_LIMIT			0x010
#define PCIE_ATU_LOWER_TARGET		0x014
#define PCIE_ATU_UPPER_TARGET		0x018
#define PCIE_ATU_UPPER_LIMIT		0x020

#define PCIE_ATU_INCREASE_REGION_SIZE	BIT(13)

#define PCIE_ATU_FUNC_NUM(pf)           ((pf) << 20)

#define PCIE_ATU_TYPE_MEM		0x0

#define ATU_IB	1
#define ATU_OB	0

#define PCIE_ATU_BASE(dir, index) (((index) << 9) | (dir << 8))

#define ADDRES_MATCH	0
#define BAR_MATCH	1
struct iatu {
	int match_type;
	int index;
	int type;
	uint64_t cpu_addr;
	uint64_t pci_addr;
	uint64_t size;
	uint32_t func;
	uint32_t bar;
};

#define BM1690E_PCIE_FMT_BOARDID(id) ((id) << 52)
#define BM1690E_PCIE_FMT_CHIPID(id) ((id) << 49)
#define BM1690E_PCIE_FMT_BARID(id) ((id) << 45)

#define BM1690E_PCIE_FMT_GET_BOARDID(addr)	(((addr) & GENMASK(58, 52)) >> 52)
#define BM1690E_PCIE_FMT_GET_CHIPID(addr)	(((addr) & GENMASK(51, 49)) >> 49)
#define BM1690E_PCIE_FMT_GET_FUNC(addr)		(((addr) & GENMASK(48, 46)) >> 46)
#define BM1690E_PCIE_FMT_GET_MSI(addr)		(((addr) & GENMASK(45, 45)) >> 45)
#define BM1690E_PCIE_FMT_GET_BARID(addr)	(((addr) & GENMASK(48, 45)) >> 45)

#define C2C_IBATU(index) (0xc00 + ((index) * 0x10))
#define C2C_OBATU(index) (0x200 + ((index) * 0x10))

#define C2C_OBATU_UPPER_BASE	0x0
#define C2C_OBATU_LOWER_BASE	0x4
#define C2C_OBATU_CTRL		0x8

#define C2C_IBATU_UPPER_BASE	0x0
#define C2C_IBATU_LOWER_BASE	0x4
#define C2C_IBATU_CTRL		0x8
#define C2C_IBATU_DST_ADDR	0xC

#define C2C_IBATU_CTRL_ENABLE	BIT(31)
#define C2C_IBATU_CTRL_CHIPID(id) ((id) << 16)
#define C2C_IBATU_CTRL_BOARDID(id) ((id) << 19)
#define C2C_IBATU_CTRL_MSI(id) ((id) << 26)
#define C2C_IBATU_CTRL_FUNC(id) ((((id) & 0x3) << 29) | (((id) >> 2) << 27))
#define C2C_IBATU_CTRL_SELX8(id) ((id) << 28)
#define C2C_IBATU_CTRL_IBSIZE(id) ((id) << 0)

#define C2C_OBATU_CTRL_ENABLE	BIT(31)
#define C2C_OBATU_DST_PC(val)	((val) << 30)
#define C2C_OBATU_PC_MSI(val)	((val) << 29)
#define C2C_OBATU_SELX8(val)	((val) << 28)
#define C2C_OBATU_FUNC_NUM(val)	((val) << 24)
#define C2C_OBATU_MATCH_ADDR(addr)	(((addr >> 32) & 0xff) << 16)
#define C2C_OBATU_OB_SIZE(val)	(val)

#define OB_RECODER	0x0
#define IB_RECODER	0x1

#define OB_RECODER_ADDR(index)	(0xd00 + ((index) * 0x10))
#define IB_RECODER_ADDR(index)	(0xf00 + ((index) * 0x10))

#define IB_RECODER_ST_ADDR	0x0
#define IB_RECODER_MASK_ADDR	0x4
#define IB_RECODER_RECODE_ADDR	0x8

struct recoder_addr {
	uint64_t match_addr;
	uint64_t out_addr;
	uint64_t mask_size;
};

#define BM1690E_L2M_BASE_ADDR	0x6980000000
#define BM1690E_MSG_BASE_ADDR	0x6c00000000
#define BM1690E_MTLI_BASE_ADDR	0x6e10000000

#define RECODER_FMT_CFG(barid, addr) (((barid) << 28) | ((addr) << 0))
#define PCIE_FMT_TO_RECODER_ADDR(addr) (((addr) & GENMASK(39, 0)) >> 12)

#define PORTCODE_BOARDSIZE_SHIFT	(27)
#define PORTCODE_BOARDID_SHIFT		(20)

#define WR_ORDER_START_LOWER	(0x0)
#define WR_ORDER_START_UPPER	(0x4)
#define WR_ORDER_END_LOWER	(0x8)
#define WR_ORDER_END_UPPER	(0xc)

#define WR_ORDER_PC_MODE	(0x1)
#define WR_ORDER_CHIP_MODE	(0x2)
#define WR_ORDER_ALL_ADDR_MODE	(0x0)

#define DBI_AXI_ADDR	(0x1000000000000000)

#define SOC_CONFIG_ADDR	(0x6c00000000)
#define SOC_CONFIG_SIZE	(0x460400000)

struct wr_order_list {
	uint64_t start_addr;
	uint64_t size;
};

#define BAR0_SIZE	(0x400000)
#define BAR1_SIZE	(0x400000)
#define PCIE_CACHE_CTRL 0x1118

#define BM1690E_MSI_GEN_ADDR(val)	(val)
#define BM1690E_MSI_GEN_MSI_EN(val)	((val) << 21)
#define BM1690E_MSI_FUNC_NUM(val)	((val) << 22)
#define BM1690E_MSI_GEN_DST_CHIPID(val)	((val) << 25)

#define BM1690E_MSI_ADDR(val)	((val) | BM1690E_MSI_GEN_MSI_EN(1) | \
				 BM1690E_MSI_FUNC_NUM(0) | \
				 BM1690E_MSI_GEN_DST_CHIPID(7))

#define BM1690E_SOC_MSI_ADDR_FUNC_NUM(val)	(val << 60)

#define PCIEX8_SN_ADDR_CTRL	(0xe0)
#define PCIEX4_SN_ADDR_CTRL	(0xe4)

#define BM1690E_K2K_ROUTING(val)	((val) << 0)
#define BM1690E_DST_BOARD_ID(val)	((val) << 3)
#define BM1690E_MSI(val)		((val) << 11)
#define BM1690E_FUNC_NUM(val)		((val) << 12)
#define BM1690E_DST_CHIP_ID(val)	((val) << 15)

enum pcie_id {
	PCIE0 = 0,
	PCIE1,
	PCIE2,
	PCIE3,
	PCIE4,
	PCIE5,
	PCIE6,
	PCIE7,
	PCIE8,
	PCIE9,
	PCIE_MAX,
};

#define BM1690_SRAM_BASE	(0X7010000000)
#define CONFIG_FILE_ADDR	(0x70101ff000)
#define SYS_IF_BASE	(0x70101fe000)
#define SYS_IF_SIZE	(0x1000)
#define PER_CONFIG_STR_OFFSET	(0x1000)
#define CONFIG_STRUCT_BASE	(0x70101fe000 - PCIE_MAX * PER_CONFIG_STR_OFFSET)
#define CONFIG_STRUCT_OFFSET	(CONFIG_STRUCT_BASE - BM1690_SRAM_BASE)

#define PCIE_INFO_BASE	CONFIG_STRUCT_BASE
#define PER_INFO_SIZE	PER_CONFIG_STR_OFFSET
#define PCIE_INFO_SIZE	(10 * 1024 * 1024)




#endif
