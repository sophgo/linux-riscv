/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SG_PCIE_PHY__
#define __SG_PCIE_PHY__

#include <linux/types.h>

enum pcie_serdes_mode {
	PCIE_SERDES_MODE_X8 = 0,
	PCIE_SERDES_MODE_X4_X4,
	PCIE_SERDES_MODE_BUTT
};

enum {
	PCIE_PHY_NO_SRAM_BYPASS = 0,
	PCIE_PHY_SRAM_BYPASS,
	PCIE_PHY_SRAM_BL_BYPASS,
	PCIE_PHY_BYPASS_MODE_BUTT
};

enum pcie_phy_id {
	PCIE_PHY_ID_0 = 0,   // 4lanes
	PCIE_PHY_ID_1,       // 4lanes
	PCIE_PHY_ID_BUTT
};

#define GENMASK_32(h, l) \
	(((0xFFFFFFFF) << (l)) & (0xFFFFFFFF >> (32UL - 1 - (h))))

#define CXP_TOP_REG_RX000                                   0x0
#define CXP_TOP_REG_RX054                                   0x54
#define CXP_TOP_REG_RX060                                   0x60
#define CXP_TOP_REG_RX064                                   0x64
#define CXP_TOP_REG_RX070                                   0x70
#define CXP_TOP_REG_RX094                                   0x94
#define CXP_TOP_REG_RX098                                   0x98
#define CXP_TOP_REG_RX0F0                                   0xf0
#define CXP_TOP_REG_RX39C                                   0x39c
#define CXP_TOP_REG_RX508                                   0x508
#define CXP_TOP_REG_RX90C                                   0x90c
#define CXP_TOP_REG_RXD70                                   0xd70
#define CXP_TOP_REG_RXCF0                                   0xcf0

#define CXP_TOP_REG_RX054_PHY0_PCS_PWR_STABLE_BIT           26
#define CXP_TOP_REG_RX054_PHY0_PMA_PWR_STABLE_BIT           28
#define CXP_TOP_REG_RX060_PHY0_SRAM_EXT_LD_DONE_BIT         9
#define CXP_TOP_REG_RX064_PHY1_PCS_PWR_STABLE_BIT           26
#define CXP_TOP_REG_RX064_PHY1_PMA_PWR_STABLE_BIT           28
#define CXP_TOP_REG_RX070_PHY1_SRAM_EXT_LD_DONE_BIT         9
#define CXP_TOP_REG_RX094_PHY_EXT_CTRL_SEL_BIT              0
#define CXP_TOP_REG_RX098_PHY_LANE0_CNTX_EN_BIT             0
#define CXP_TOP_REG_RX90C_UPCS_PWR_STABLE_BIT               23

#define CXP_TOP_REG_PHY0_PHY1_SRAM_BL_BYPASS_BIT            5
#define CXP_TOP_REG_PHY0_PHY1_SRAM_BYPASS_BIT               6
#define CXP_TOP_REG_PHY0_PHY1_SRAM_EXT_LD_DONE_BIT          9
#define CXP_TOP_REG_PHYX_SRAM_INIT_DONE_BIT                 10

#define CXP_TOP_REG_RX000_SS_MODE_MASK                      GENMASK_32(2, 0)
#define CXP_TOP_REG_DCC_CTRL_CM_RANGE_R1_MASK               GENMASK_32(7, 4)
#define CXP_TOP_REG_PHY0_PHY1_SRAM_BYPASS_MASK              GENMASK_32(6, 5)
#define CXP_TOP_REG_PROTOCOL0_EXT_TX_RX_DCC_CTRL_CM_RANGE_G1_MASK  GENMASK_32(3, 0)
#define CXP_TOP_REG_PHY_LANE0_CNTX_EN
#define CXP_TOP_PHY_TX0_RX0_CNTX_SEL_G1_MASK                GENMASK_64(23, 16)
#define CXP_TOP_PHY_TX0_RX0_CNTX_SEL_G2_MASK                GENMASK_64(31, 24)
#define CXP_TOP_PHY_TX0_RX0_CNTX_SEL_G3_MASK                GENMASK_64(39, 32)
#define CXP_TOP_PHY_TX0_RX0_CNTX_SEL_G4_MASK                GENMASK_64(47, 40)
#define CXP_TOP_PHY_TX0_RX0_CNTX_SEL_G5_MASK                GENMASK_64(55, 48)

#endif
