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

enum pcie_rst_status {
	PCIE_RST_ASSERT = 0,
	PCIE_RST_DE_ASSERT,
	PCIE_RST_STATUS_BUTT
};

#endif
