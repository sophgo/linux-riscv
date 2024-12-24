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

#define PCIE_DATA_LINK_PCIE	0
#define PCIE_DATA_LINK_C2C	1

#endif
