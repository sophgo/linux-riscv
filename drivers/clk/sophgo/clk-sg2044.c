/*
 * Copyright (c) 2024 SOPHGO
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/clock/sophgo,sg2044-clock.h>

#include "clk.h"

/* fixed clocks */
struct sg2044_pll_clock sg2044_root_pll_clks[] = {
	{
		.id = MPLL0_CLK,
		.name = "mpll0_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = MPLL1_CLK,
		.name = "mpll1_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = MPLL2_CLK,
		.name = "mpll2_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = MPLL3_CLK,
		.name = "mpll3_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = MPLL4_CLK,
		.name = "mpll4_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = MPLL5_CLK,
		.name = "mpll5_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = FPLL0_CLK,
		.name = "fpll0_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
	}, {
		.id = FPLL1_CLK,
		.name = "fpll1_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
	}, {
		.id = DPLL0_CLK,
		.name = "dpll0_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL1_CLK,
		.name = "dpll1_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL2_CLK,
		.name = "dpll2_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL3_CLK,
		.name = "dpll3_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL4_CLK,
		.name = "dpll4_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL5_CLK,
		.name = "dpll5_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL6_CLK,
		.name = "dpll6_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}, {
		.id = DPLL7_CLK,
		.name = "dpll7_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = SG2044_CLK_RO,
		.status_offset = 0x98,
		.enable_offset = 0x9c,
	}
};

/* divider clocks */
static const struct sg2044_divider_clock div_clks[] = {
	{ DIV_CLK_MPLL0_AP_CPU_NORMAL_0, "clk_div_ap_sys_0", "clk_gate_ap_sys_div0",
		0, 0x2040, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL1_RP_SYS_0, "clk_div_rp_sys_0", "clk_gate_rp_sys_div0",
		0, 0x204c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL2_TPU_SYS_0, "clk_div_tpu_sys_0", "clk_gate_tpu_sys_div0",
		0, 0x2054, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL3_CC_GRP_SYS_0, "clk_div_ccgrp_sys_0", "clk_gate_ccgrp_sys_div0",
		0, 0x206c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL4_SRC0_0, "clk_div_src0_0", "clk_gate_src0_div0",
		0, 0x2074, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL5_SRC1_0, "clk_div_src1_0", "clk_gate_src1_div0",
		0, 0x207c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL1_CXP_MAC_0, "clk_div_cxp_mac_0", "clk_gate_cxp_mac_div0",
		0, 0x2084, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_CC_GRP_SYS_1, "clk_div_ccgrp_sys_1", "clk_gate_cc_grp_sys_div1",
		0, 0x2070, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TPU_SYS_1, "clk_div_tpu_sys_1", "clk_gate_tpu_sys_div1",
		0, 0x2058, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_RP_SYS_1, "clk_div_rp_sys_1", "clk_gate_rp_sys_div1",
		0, 0x2050, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_AP_CPU_NORMAL_1, "clk_div_ap_sys_1", "clk_gate_ap_sys_div1",
		0, 0x2044, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_SRC0_1, "clk_div_src0_1", "clk_gate_src0_div1",
		0, 0x2078, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_SRC1_1, "clk_div_src1_1", "clk_gate_src1_div1",
		0, 0x2080, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_CXP_MAC_1, "clk_div_cxp_mac_1", "clk_gate_cxp_mac_div1",
		0, 0x2088, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_CXP_TEST_PHY, "clk_div_cxp_test_phy", "fpll0_clock",
		0, 0x2064, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_C2C1_TEST_PHY, "clk_div_c2c1_test_phy", "fpll0_clock",
		0, 0x2060, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_C2C0_TEST_PHY, "clk_div_c2c0_test_phy", "fpll0_clock",
		0, 0x205c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TOP_50M, "clk_div_top_50m", "fpll0_clock",
		0, 0x2048, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER1, "clk_div_timer1", "clk_div_top_50m",
		0, 0x20d0, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER2, "clk_div_timer2", "clk_div_top_50m",
		0, 0x20d4, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER3, "clk_div_timer3", "clk_div_top_50m",
		0, 0x20d8, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER4, "clk_div_timer4", "clk_div_top_50m",
		0, 0x20dc, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER5, "clk_div_timer5", "clk_div_top_50m",
		0, 0x20e0, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER6, "clk_div_timer6", "clk_div_top_50m",
		0, 0x20e4, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER7, "clk_div_timer7", "clk_div_top_50m",
		0, 0x20e8, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER8, "clk_div_timer8", "clk_div_top_50m",
		0, 0x20ec, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_PKA, "clk_div_pka", "fpll0_clock",
		0, 0x20f0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_UART_500M, "clk_div_uart_500m", "fpll0_clock",
		0, 0x20cc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_EFUSE, "clk_div_efuse", "fpll0_clock",
		0, 0x20f4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TX_ETH0, "clk_div_tx_eth0", "fpll0_clock",
		0, 0x20fc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_PTP_REF_I_ETH0, "clk_div_ptp_ref_i_eth0", "fpll0_clock",
		0, 0x2100, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, 5},
	{ DIV_CLK_FPLL0_REF_ETH0, "clk_div_ref_eth0", "fpll0_clock",
		0, 0x2104, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_EMMC, "clk_div_emmc", "fpll0_clock",
		0, 0x2108, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_SD, "clk_div_sd", "fpll0_clock",
		0, 0x2110, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TOP_AXI0, "clk_div_top_axi0", "fpll0_clock",
		0, 0x2118, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_100K_SD, "clk_div_100k_sd", "clk_div_top_axi0",
		0, 0x2114, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_100K_EMMC, "clk_div_100k_emmc", "clk_div_top_axi0",
		0, 0x210c, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_GPIO_DB, "clk_div_gpio_db", "clk_div_top_axi0",
		0, 0x20f8, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TOP_AXI_HSPERI, "clk_div_top_axi_hsperi", "fpll0_clock",
		0, 0x211c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL1_PCIE_1G, "clk_div_pcie_1g", "fpll1_clock",
		0, 0x2160, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_DPLL0_DDR0_0, "clk_div_ddr0_0", "dpll0_clock",
		0, 0x2120, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR0_1, "clk_div_ddr0_1", "fpll0_clock",
		0, 0x2124, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL1_DDR1_0, "clk_div_ddr1_0", "dpll1_clock",
		0, 0x2128, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR1_1, "clk_div_ddr1_1", "fpll0_clock",
		0, 0x212c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL2_DDR2_0, "clk_div_ddr2_0", "dpll2_clock",
		0, 0x2130, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR2_1, "clk_div_ddr2_1", "fpll0_clock",
		0, 0x2134, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL3_DDR3_0, "clk_div_ddr3_0", "dpll3_clock",
		0, 0x2138, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR3_1, "clk_div_ddr3_1", "fpll0_clock",
		0, 0x213c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL4_DDR4_0, "clk_div_ddr4_0", "dpll4_clock",
		0, 0x2140, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR4_1, "clk_div_ddr4_1", "fpll0_clock",
		0, 0x2144, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL5_DDR5_0, "clk_div_ddr5_0", "dpll5_clock",
		0, 0x2148, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR5_1, "clk_div_ddr5_1", "fpll0_clock",
		0, 0x214c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL6_DDR6_0, "clk_div_ddr6_0", "dpll6_clock",
		0, 0x2150, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR6_1, "clk_div_ddr6_1", "fpll0_clock",
		0, 0x2154, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL7_DDR7_0, "clk_div_ddr7_0", "dpll7_clock",
		0, 0x2158, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR7_1, "clk_div_ddr7_1", "fpll0_clock",
		0, 0x215c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
};

/* gate clocks */
static const struct sg2044_gate_clock gate_clks[] = {
	{ GATE_CLK_AP_CPU_NORMAL_DIV0, "clk_gate_ap_sys_div0", "mpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2040, 4, 0 },
	{ GATE_CLK_RP_SYS_DIV0, "clk_gate_rp_sys_div0", "mpll1_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x204c, 4, 0 },
	{ GATE_CLK_TPU_SYS_DIV0, "clk_gate_tpu_sys_div0", "mpll2_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2054, 4, 0 },
	{ GATE_CLK_CC_GRP_SYS_DIV0, "clk_gate_ccgrp_sys_div0", "mpll3_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x206c, 4, 0 },
	{ GATE_CLK_SRC0_DIV0, "clk_gate_src0_div0", "mpll4_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2074, 4, 0 },
	{ GATE_CLK_SRC1_DIV0, "clk_gate_src1_div0", "mpll5_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x207c, 4, 0 },
	{ GATE_CLK_CXP_MAC_DIV0, "clk_gate_cxp_mac_div0", "fpll1_clock",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x2084, 4, 0 },

	{ GATE_CLK_AP_CPU_NORMAL_DIV1, "clk_gate_ap_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2044, 4, 0 },
	{ GATE_CLK_RP_SYS_DIV1, "clk_gate_rp_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2050, 4, 0 },
	{ GATE_CLK_TPU_SYS_DIV1, "clk_gate_tpu_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2058, 4, 0 },
	{ GATE_CLK_CC_GRP_SYS_DIV1, "clk_gate_cc_grp_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2070, 4, 0 },
	{ GATE_CLK_SRC0_DIV1, "clk_gate_src0_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2078, 4, 0 },
	{ GATE_CLK_SRC1_DIV1, "clk_gate_src1_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2080, 4, 0 },
	{ GATE_CLK_CXP_MAC_DIV1, "clk_gate_cxp_mac_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x2088, 4, 0 },

	{ GATE_CLK_AP_CPU_NORMAL, "clk_gate_ap_sys", "clk_mux_ap_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 0, 0 },
	{ GATE_CLK_RP_SYS, "clk_gate_rp_sys", "clk_mux_rp_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 2, 0 },
	{ GATE_CLK_TPU_SYS, "clk_gate_tpu_sys", "clk_mux_tpu_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 3, 0 },
	{ GATE_CLK_CC_GRP_SYS, "clk_gate_ccgrp_sys", "clk_mux_ccgrp_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 8, 0 },
	{ GATE_CLK_SRC0, "clk_gate_src0", "clk_mux_src0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 9, 0 },
	{ GATE_CLK_SRC1, "clk_gate_src1", "clk_mux_src1",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 10, 0 },
	{ GATE_CLK_CXP_MAC, "clk_gate_cxp_mac", "clk_mux_cxp_mac",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x2000, 14, 0 },
	{ GATE_CLK_DDR0, "clk_gate_ddr0", "clk_mux_ddr0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 7, 0 },
	{ GATE_CLK_DDR1, "clk_gate_ddr1", "clk_mux_ddr1",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 8, 0 },
	{ GATE_CLK_DDR2, "clk_gate_ddr2", "clk_mux_ddr2",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 9, 0 },
	{ GATE_CLK_DDR3, "clk_gate_ddr3", "clk_mux_ddr3",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 10, 0 },
	{ GATE_CLK_DDR4, "clk_gate_ddr4", "clk_mux_ddr4",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 11, 0 },
	{ GATE_CLK_DDR5, "clk_gate_ddr5", "clk_mux_ddr5",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 12, 0 },
	{ GATE_CLK_DDR6, "clk_gate_ddr6", "clk_mux_ddr6",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 13, 0 },
	{ GATE_CLK_DDR7, "clk_gate_ddr7", "clk_mux_ddr7",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 14, 0 },
	{ GATE_CLK_CXP_TEST_PHY, "clk_gate_cxp_test_phy", "clk_div_cxp_test_phy",
		CLK_SET_RATE_PARENT, 0x2000, 6, 0 },
	{ GATE_CLK_C2C1_TEST_PHY, "clk_gate_c2c1_test_phy", "clk_div_c2c1_test_phy",
		CLK_SET_RATE_PARENT, 0x2000, 5, 0 },
	{ GATE_CLK_C2C0_TEST_PHY, "clk_gate_c2c0_test_phy", "clk_div_c2c0_test_phy",
		CLK_SET_RATE_PARENT, 0x2000, 4, 0 },
	{ GATE_CLK_TOP_50M, "clk_gate_top_50m", "clk_div_top_50m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 1, 0 },
	{ GATE_CLK_SC_RX, "clk_gate_sc_rx", "clk_div_top_50m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 12, 0 },
	{ GATE_CLK_SC_RX_X0Y1, "clk_gate_sc_rx_x0y1", "clk_div_top_50m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 13, 0 },
	{ GATE_CLK_TIMER1, "clk_gate_timer1", "clk_div_timer1",
		CLK_SET_RATE_PARENT, 0x2004, 8, 0 },
	{ GATE_CLK_TIMER2, "clk_gate_timer2", "clk_div_timer2",
		CLK_SET_RATE_PARENT, 0x2004, 9, 0 },
	{ GATE_CLK_TIMER3, "clk_gate_timer3", "clk_div_timer3",
		CLK_SET_RATE_PARENT, 0x2004, 10, 0 },
	{ GATE_CLK_TIMER4, "clk_gate_timer4", "clk_div_timer4",
		CLK_SET_RATE_PARENT, 0x2004, 11, 0 },
	{ GATE_CLK_TIMER5, "clk_gate_timer5", "clk_div_timer5",
		CLK_SET_RATE_PARENT, 0x2004, 12, 0 },
	{ GATE_CLK_TIMER6, "clk_gate_timer6", "clk_div_timer6",
		CLK_SET_RATE_PARENT, 0x2004, 13, 0 },
	{ GATE_CLK_TIMER7, "clk_gate_timer7", "clk_div_timer7",
		CLK_SET_RATE_PARENT, 0x2004, 14, 0 },
	{ GATE_CLK_TIMER8, "clk_gate_timer8", "clk_div_timer8",
		CLK_SET_RATE_PARENT, 0x2004, 15, 0 },
	{ GATE_CLK_PKA, "clk_gate_pka", "clk_div_pka",
		CLK_SET_RATE_PARENT, 0x2004, 16, 0 },
	{ GATE_CLK_UART_500M, "clk_gate_uart_500m", "clk_div_uart_500m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 1, 0 },
	{ GATE_CLK_EFUSE, "clk_gate_efuse", "clk_div_efuse",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 17, 0 },
	{ GATE_CLK_TX_ETH0, "clk_gate_tx_eth0", "clk_div_tx_eth0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 27, 0 },
	{ GATE_CLK_PTP_REF_I_ETH0, "clk_gate_ptp_ref_i_eth0", "clk_div_ptp_ref_i_eth0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 29, 0 },
	{ GATE_CLK_REF_ETH0, "clk_gate_ref_eth0", "clk_div_ref_eth0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 30, 0 },
	{ GATE_CLK_EMMC, "clk_gate_emmc", "clk_div_emmc",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 0, 0 },
	{ GATE_CLK_SD, "clk_gate_sd", "clk_div_sd",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 3, 0 },
	{ GATE_CLK_TOP_AXI0, "clk_gate_top_axi0", "clk_div_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 5, 0 },
	{ GATE_CLK_100K_SD, "clk_gate_100k_sd", "clk_div_100k_sd",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 4, 0 },
	{ GATE_CLK_100K_EMMC, "clk_gate_100k_emmc", "clk_div_100k_emmc",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 1, 0 },
	{ GATE_CLK_APB_RTC, "clk_gate_apb_rtc", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 26, 0 },
	{ GATE_CLK_APB_PWM, "clk_gate_apb_pwm", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 25, 0 },
	{ GATE_CLK_APB_WDT, "clk_gate_apb_wdt", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 24, 0 },
	{ GATE_CLK_APB_I2C, "clk_gate_apb_i2c", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 23, 0 },
	{ GATE_CLK_GPIO_DB, "clk_gate_gpio_db", "clk_div_gpio_db",
		CLK_IS_CRITICAL, 0x2004, 21, 0 },
	{ GATE_CLK_APB_GPIO_INTR, "clk_gate_apb_gpio_intr", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 20, 0 },
	{ GATE_CLK_APB_GPIO, "clk_gate_apb_gpio", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 19, 0 },
	{ GATE_CLK_APB_EFUSE, "clk_gate_apb_efuse", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 18, 0 },
	{ GATE_CLK_APB_TIMER, "clk_gate_apb_timer", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 7, 0 },
	{ GATE_CLK_AXI_SRAM, "clk_gate_axi_sram", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 6, 0 },
	{ GATE_CLK_AHB_SF, "clk_gate_ahb_sf", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 5, 0 },
	{ GATE_CLK_APB_ROM, "clk_gate_apb_rom", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 4, 0 },
	{ GATE_CLK_MAILBOX0, "clk_gate_mailbox0", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 16, 0 },
	{ GATE_CLK_MAILBOX1, "clk_gate_mailbox1", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 17, 0 },
	{ GATE_CLK_MAILBOX2, "clk_gate_mailbox2", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 18, 0 },
	{ GATE_CLK_MAILBOX3, "clk_gate_mailbox3", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 19, 0 },
	{ GATE_CLK_INTC0, "clk_gate_intc0", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 20, 0 },
	{ GATE_CLK_INTC1, "clk_gate_intc1", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 21, 0 },
	{ GATE_CLK_INTC2, "clk_gate_intc2", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 22, 0 },
	{ GATE_CLK_INTC3, "clk_gate_intc3", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x20, 23, 0 },
	{ GATE_CLK_CXP_CFG, "clk_gate_cxp_cfg", "clk_gate_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x2000, 15, 0 },
	{ GATE_CLK_TOP_AXI_HSPERI, "clk_gate_top_axi_hsperi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 6, 0 },
	{ GATE_CLK_AXI_SD, "clk_gate_axi_sd", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2008, 2, 0 },
	{ GATE_CLK_AXI_EMMC, "clk_gate_axi_emmc", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 31, 0 },
	{ GATE_CLK_AXI_ETH0, "clk_gate_axi_eth0", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 28, 0 },
	{ GATE_CLK_APB_SPI, "clk_gate_apb_spi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 22, 0 },
	{ GATE_CLK_AXI_DBG_I2C, "clk_gate_axi_dbg_i2c", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 3, 0 },
	{ GATE_CLK_APB_UART, "clk_gate_apb_uart", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 2, 0 },
	{ GATE_CLK_SYSDMA_AXI, "clk_gate_sysdma_axi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 0, 0 },
};

/* mux clocks */
static const char *const clk_mux_ddr0_p[] = {
			"clk_div_ddr0_1", "clk_div_ddr0_0"};
static const char *const clk_mux_ddr1_p[] = {
			"clk_div_ddr1_1", "clk_div_ddr1_0"};
static const char *const clk_mux_ddr2_p[] = {
			"clk_div_ddr2_1", "clk_div_ddr2_0"};
static const char *const clk_mux_ddr3_p[] = {
			"clk_div_ddr3_1", "clk_div_ddr3_0"};
static const char *const clk_mux_ddr4_p[] = {
			"clk_div_ddr4_1", "clk_div_ddr4_0"};
static const char *const clk_mux_ddr5_p[] = {
			"clk_div_ddr5_1", "clk_div_ddr5_0"};
static const char *const clk_mux_ddr6_p[] = {
			"clk_div_ddr6_1", "clk_div_ddr6_0"};
static const char *const clk_mux_ddr7_p[] = {
			"clk_div_ddr7_1", "clk_div_ddr7_0"};
static const char *const clk_mux_cc_grp_sys_p[] = {
			"clk_div_ccgrp_sys_1", "clk_div_ccgrp_sys_0"};
static const char *const clk_mux_tpu_sys_p[] = {
			"clk_div_tpu_sys_1", "clk_div_tpu_sys_0"};
static const char *const clk_mux_rp_sys_p[] = {
			"clk_div_rp_sys_1", "clk_div_rp_sys_0"};
static const char *const clk_mux_ap_cpu_normal_p[] = {
			"clk_div_ap_sys_1", "clk_div_ap_sys_0"};
static const char *const clk_mux_src0_p[] = {
			"clk_div_src0_1", "clk_div_src0_0"};
static const char *const clk_mux_src1_p[] = {
			"clk_div_src1_1", "clk_div_src1_0"};
static const char *const clk_mux_cxp_mac_p[] = {
			"clk_div_cxp_mac_1", "clk_div_cxp_mac_0"};

struct sg2044_mux_clock mux_clks[] = {
	{
		MUX_CLK_DDR0, "clk_mux_ddr0", clk_mux_ddr0_p,
		ARRAY_SIZE(clk_mux_ddr0_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 7, 1, 0,
	}, {
		MUX_CLK_DDR1, "clk_mux_ddr1", clk_mux_ddr1_p,
		ARRAY_SIZE(clk_mux_ddr1_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 8, 1, 0,
	}, {
		MUX_CLK_DDR2, "clk_mux_ddr2", clk_mux_ddr2_p,
		ARRAY_SIZE(clk_mux_ddr2_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 9, 1, 0,
	}, {
		MUX_CLK_DDR3, "clk_mux_ddr3", clk_mux_ddr3_p,
		ARRAY_SIZE(clk_mux_ddr3_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 10, 1, 0,
	}, {
		MUX_CLK_DDR4, "clk_mux_ddr4", clk_mux_ddr4_p,
		ARRAY_SIZE(clk_mux_ddr4_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 11, 1, 0,
	}, {
		MUX_CLK_DDR5, "clk_mux_ddr5", clk_mux_ddr5_p,
		ARRAY_SIZE(clk_mux_ddr5_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 12, 1, 0,
	}, {
		MUX_CLK_DDR6, "clk_mux_ddr6", clk_mux_ddr6_p,
		ARRAY_SIZE(clk_mux_ddr6_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 13, 1, 0,
	}, {
		MUX_CLK_DDR7, "clk_mux_ddr7", clk_mux_ddr7_p,
		ARRAY_SIZE(clk_mux_ddr7_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 14, 1, 0,
	}, {
		MUX_CLK_CC_GRP_SYS, "clk_mux_ccgrp_sys", clk_mux_cc_grp_sys_p,
		ARRAY_SIZE(clk_mux_cc_grp_sys_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 3, 1, 0,
	}, {
		MUX_CLK_TPU_SYS, "clk_mux_tpu_sys", clk_mux_tpu_sys_p,
		ARRAY_SIZE(clk_mux_tpu_sys_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 2, 1, 0,
	}, {
		MUX_CLK_RP_SYS, "clk_mux_rp_sys", clk_mux_rp_sys_p,
		ARRAY_SIZE(clk_mux_rp_sys_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 1, 1, 0,
	}, {
		MUX_CLK_AP_CPU_NORMAL, "clk_mux_ap_sys", clk_mux_ap_cpu_normal_p,
		ARRAY_SIZE(clk_mux_ap_cpu_normal_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 0, 1, 0,
	}, {
		MUX_CLK_SRC0, "clk_mux_src0", clk_mux_src0_p,
		ARRAY_SIZE(clk_mux_src0_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 4, 1, 0,
	}, {
		MUX_CLK_SRC1, "clk_mux_src1", clk_mux_src1_p,
		ARRAY_SIZE(clk_mux_src1_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 5, 1, 0,
	}, {
		MUX_CLK_CXP_MAC, "clk_mux_cxp_mac", clk_mux_cxp_mac_p,
		ARRAY_SIZE(clk_mux_cxp_mac_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 6, 1, 0,
	},
};

struct sg2044_clk_table pll_clk_tables = {
	.pll_clks_num = ARRAY_SIZE(sg2044_root_pll_clks),
	.pll_clks = sg2044_root_pll_clks,
};

struct sg2044_clk_table div_clk_tables[] = {
	{
		.id = DIV_CLK_TABLE,
		.div_clks_num = ARRAY_SIZE(div_clks),
		.div_clks = div_clks,
		.gate_clks_num = ARRAY_SIZE(gate_clks),
		.gate_clks = gate_clks,
	},
};

struct sg2044_clk_table mux_clk_tables[] = {
	{
		.id = MUX_CLK_TABLE,
		.mux_clks_num = ARRAY_SIZE(mux_clks),
		.mux_clks = mux_clks,
	},
};

static const struct of_device_id sg2044_clk_match_ids_tables[] = {
	{
		.compatible = "sg2044, pll-clock",
		.data = &pll_clk_tables,
	},
	{
		.compatible = "sg2044, pll-child-clock",
		.data = div_clk_tables,
	},
	{
		.compatible = "sg2044, pll-mux-clock",
		.data = mux_clk_tables,
	},
	{
		.compatible = "sg2044, clk-default-rates",
	},
	{}
};

static void __init sg2044_clk_init(struct device_node *node)
{
	struct device_node *np_top;
	struct sg2044_clk_data *clk_data = NULL;
	const struct sg2044_clk_table *dev_data;
	static struct regmap *syscon;
	static void __iomem *base;
	int i, ret = 0;
	unsigned int id;
	const struct of_device_id *match = NULL;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data) {
		ret = -ENOMEM;
		goto out;
	}
	match = of_match_node(sg2044_clk_match_ids_tables, node);
	if (match) {
		dev_data = (struct sg2044_clk_table *)match->data;
	} else {
		pr_err("%s did't match node data\n", __func__);
		ret = -ENODEV;
		goto no_match_data;
	}

	spin_lock_init(&clk_data->lock);
	if (of_device_is_compatible(node, "sg2044, pll-clock")) {
		np_top = of_parse_phandle(node, "subctrl-syscon", 0);
		if (!np_top) {
			pr_err("%s can't get subctrl-syscon node\n",
				__func__);
			ret = -EINVAL;
			goto no_match_data;
		}

		if (!dev_data->pll_clks_num) {
			ret = -EINVAL;
			goto no_match_data;
		}

		syscon = syscon_node_to_regmap(np_top);
		if (IS_ERR_OR_NULL(syscon)) {
			pr_err("%s cannot get regmap %ld\n", __func__, PTR_ERR(syscon));
			ret = -ENODEV;
			goto no_match_data;
		}

		base = of_iomap(np_top, 0);
		clk_data->table = dev_data;
		clk_data->base = base;
		clk_data->syscon_top = syscon;

		if (of_property_read_u32(node, "id", &id)) {
			pr_err("%s cannot get pll id for %s\n",
				__func__, node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}
		ret = sg2044_register_pll_clks(node, clk_data, id);
	}

	if (of_device_is_compatible(node, "sg2044, pll-child-clock")) {
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			pr_err("not assigned id for %s\n", node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}

		/* Below brute-force to check dts property "id"
		 * whether match id of array
		 */
		for (i = 0; i < ARRAY_SIZE(div_clk_tables); i++) {
			if (id == dev_data[i].id)
				break; /* found */
		}
		clk_data->table = &dev_data[i];
		clk_data->base = base;
		clk_data->syscon_top = syscon;
		ret = sg2044_register_div_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "sg2044, pll-mux-clock")) {
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			pr_err("not assigned id for %s\n", node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}

		/* Below brute-force to check dts property "id"
		 * whether match id of array
		 */
		for (i = 0; i < ARRAY_SIZE(mux_clk_tables); i++) {
			if (id == dev_data[i].id)
				break; /* found */
		}
		clk_data->table = &dev_data[i];
		clk_data->base = base;
		clk_data->syscon_top = syscon;
		ret = sg2044_register_mux_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "sg2044, clk-default-rates"))
		ret = set_default_clk_rates(node);

	if (!ret)
		return;

no_match_data:
	kfree(clk_data);

out:
	pr_err("%s failed error number %d\n", __func__, ret);
}

CLK_OF_DECLARE(sg2044_clk_pll, "sg2044, pll-clock", sg2044_clk_init);
CLK_OF_DECLARE(sg2044_clk_pll_child, "sg2044, pll-child-clock", sg2044_clk_init);
CLK_OF_DECLARE(sg2044_clk_pll_mux, "sg2044, pll-mux-clock", sg2044_clk_init);
CLK_OF_DECLARE(sg2044_clk_default_rate, "sg2044, clk-default-rates", sg2044_clk_init);
