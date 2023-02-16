/*
 * Copyright (c) 2022 SOPHGO
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/clock/sophgo-mango-clock.h>

#include "clk.h"

/* fixed clocks */
struct mango_pll_clock mango_root_pll_clks[] = {
	{
		.id = FPLL_CLK,
		.name = "fpll_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = MANGO_CLK_RO,
	}, {
		.id = DPLL0_CLK,
		.name = "dpll0_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = MANGO_CLK_RO,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	}, {
		.id = DPLL1_CLK,
		.name = "dpll1_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.ini_flags = MANGO_CLK_RO,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	}, {
		.id = MPLL_CLK,
		.name = "mpll_clock",
		.parent_name = "cgi",
		.flags = CLK_GET_RATE_NOCACHE | CLK_GET_ACCURACY_NOCACHE,
		.status_offset = 0xc0,
		.enable_offset = 0xc4,
	},
};

/* divider clocks */
static const struct mango_divider_clock div_clks[] = {
	{ DIV_CLK_MPLL_RP_CPU_NORMAL_0, "clk_div_rp_cpu_normal_0", "clk_gate_rp_cpu_normal_div0",
		0, 0x2040, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL_AXI_DDR_0, "clk_div_axi_ddr_0", "clk_gate_axi_ddr_div0",
		0, 0x20a4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, 5},
	{ DIV_CLK_FPLL_DDR01_1, "clk_div_ddr01_1", "clk_gate_ddr01_div1",
		0, 0x20b0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL_DDR23_1, "clk_div_ddr23_1", "clk_gate_ddr23_div1",
		0, 0x20b8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL_RP_CPU_NORMAL_1, "clk_div_rp_cpu_normal_1", "clk_gate_rp_cpu_normal_div1",
		0, 0x2044, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_50M_A53, "clk_div_50m_a53", "fpll_clock",
		0, 0x2048, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TOP_RP_CMN_DIV2, "clk_div_top_rp_cmn_div2", "clk_div_rp_cpu_normal",
		0, 0x204c, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_UART_500M, "clk_div_uart_500m", "fpll_clock",
		0, 0x2050, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AHB_LPC, "clk_div_ahb_lpc", "fpll_clock",
		0, 0x2054, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_EFUSE, "clk_div_efuse", "fpll_clock",
		0, 0x2078, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TX_ETH0, "clk_div_tx_eth0", "fpll_clock",
		0, 0x2080, 16, 11, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_PTP_REF_I_ETH0, "clk_div_ptp_ref_i_eth0", "fpll_clock",
		0, 0x2084, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_REF_ETH0, "clk_div_ref_eth0", "fpll_clock",
		0, 0x2088, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_EMMC, "clk_div_emmc", "fpll_clock",
		0, 0x208c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_SD, "clk_div_sd", "fpll_clock",
		0, 0x2094, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TOP_AXI0, "clk_div_top_axi0", "fpll_clock",
		0, 0x209c, 16, 5, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_TOP_AXI_HSPERI, "clk_div_top_axi_hsperi", "fpll_clock",
		0, 0x20a0, 16, 5, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_AXI_DDR_1, "clk_div_axi_ddr_1", "clk_gate_axi_ddr_div1",
		0, 0x20a8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, 5},
	{ DIV_CLK_FPLL_DIV_TIMER1, "clk_div_timer1", "clk_div_50m_a53",
		0, 0x2058, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER2, "clk_div_timer2", "clk_div_50m_a53",
		0, 0x205c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER3, "clk_div_timer3", "clk_div_50m_a53",
		0, 0x2060, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER4, "clk_div_timer4", "clk_div_50m_a53",
		0, 0x2064, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER5, "clk_div_timer5", "clk_div_50m_a53",
		0, 0x2068, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER6, "clk_div_timer6", "clk_div_50m_a53",
		0, 0x206c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER7, "clk_div_timer7", "clk_div_50m_a53",
		0, 0x2070, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_DIV_TIMER8, "clk_div_timer8", "clk_div_50m_a53",
		0, 0x2074, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_100K_EMMC, "clk_div_100k_emmc", "clk_div_top_axi0",
		0, 0x2090, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_100K_SD, "clk_div_100k_sd", "clk_div_top_axi0",
		0, 0x2098, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL_GPIO_DB, "clk_div_gpio_db", "clk_div_top_axi0",
		0, 0x207c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_REG_VAL, },
	{ DIV_CLK_DPLL0_DDR01_0, "clk_div_ddr01_0", "clk_gate_ddr01_div0",
		0, 0x20ac, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL1_DDR23_0, "clk_div_ddr23_0", "clk_gate_ddr23_div0",
		0, 0x20b4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, MANGO_CLK_USE_INIT_VAL, },
};

/* gate clocks */
static const struct mango_gate_clock gate_clks[] = {
	{ GATE_CLK_RP_CPU_NORMAL_DIV0, "clk_gate_rp_cpu_normal_div0", "mpll_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2020, 0, 0 },
	{ GATE_CLK_AXI_DDR_DIV0, "clk_gate_axi_ddr_div0", "mpll_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2020, 1, 0 },
	{ GATE_CLK_DDR01_DIV0, "clk_gate_ddr01_div0", "dpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2020, 2, 0 },
	{ GATE_CLK_DDR23_DIV0, "clk_gate_ddr23_div0", "dpll1_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2020, 3, 0 },

	{ GATE_CLK_RP_CPU_NORMAL_DIV1, "clk_gate_rp_cpu_normal_div1", "fpll_clock",
		CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2020, 0, 0 },
	{ GATE_CLK_AXI_DDR_DIV1, "clk_gate_axi_ddr_div1", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x2020, 1, 0 },
	{ GATE_CLK_DDR01_DIV1, "clk_gate_ddr01_div1", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x2020, 2, 0 },
	{ GATE_CLK_DDR23_DIV1, "clk_gate_ddr23_div1", "fpll_clock",
		CLK_IGNORE_UNUSED, 0x2020, 3, 0 },

	{ GATE_CLK_A53_50M, "clk_gate_a53_50m", "clk_div_50m_a53",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 1, 0 },
	{ GATE_CLK_TOP_RP_CMN_DIV2, "clk_gate_top_rp_cmn_div2", "clk_gate_rp_cpu_normal",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 2, 0 },
	{ GATE_CLK_AXI_PCIE0, "clk_gate_axi_pcie0", "clk_gate_rp_cpu_normal",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 8, 0 },
	{ GATE_CLK_AXI_PCIE1, "clk_gate_axi_pcie1", "clk_gate_rp_cpu_normal",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 9, 0 },
	{ GATE_CLK_HSDMA, "clk_gate_hsdma", "clk_gate_top_rp_cmn_div2",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 10, 0 },

	{ GATE_CLK_EMMC_100M, "clk_gate_emmc", "clk_div_emmc",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 3, 0 },
	{ GATE_CLK_SD_100M, "clk_gate_sd", "clk_div_sd",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 6, 0 },
	{ GATE_CLK_TX_ETH0, "clk_gate_tx_eth0", "clk_div_tx_eth0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 30, 0 },
	{ GATE_CLK_PTP_REF_I_ETH0, "clk_gate_ptp_ref_i_eth0", "clk_div_ptp_ref_i_eth0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 0, 0 },
	{ GATE_CLK_REF_ETH0, "clk_gate_ref_eth0", "clk_div_ref_eth0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 1, 0 },
	{ GATE_CLK_UART_500M, "clk_gate_uart_500m", "clk_div_uart_500m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 4, 0 },
	{ GATE_CLK_AHB_LPC, "clk_gate_ahb_lpc", "clk_div_ahb_lpc",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 7, 0 },
	{ GATE_CLK_EFUSE, "clk_gate_efuse", "clk_div_efuse",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 20, 0},

	{ GATE_CLK_TOP_AXI0, "clk_gate_top_axi0", "clk_div_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x2004, 11, 0 },
	{ GATE_CLK_TOP_AXI_HSPERI, "clk_gate_top_axi_hsperi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x2004, 12, 0 },

	{ GATE_CLK_AHB_ROM, "clk_gate_ahb_rom", "clk_gate_top_axi0",
		 CLK_IGNORE_UNUSED, 0x2000, 8, 0 },
	{ GATE_CLK_AHB_SF, "clk_gate_ahb_sf", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 9, 0 },
	{ GATE_CLK_AXI_SRAM, "clk_gate_axi_sram", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 10, 0 },
	{ GATE_CLK_APB_TIMER, "clk_gate_apb_timer", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 11, 0 },
	{ GATE_CLK_APB_EFUSE, "clk_gate_apb_efuse", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 21, 0 },
	{ GATE_CLK_APB_GPIO, "clk_gate_apb_gpio", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 22, 0 },
	{ GATE_CLK_APB_GPIO_INTR, "clk_gate_apb_gpio_intr", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 23, 0 },
	{ GATE_CLK_APB_I2C, "clk_gate_apb_i2c", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 26, 0 },
	{ GATE_CLK_APB_WDT, "clk_gate_apb_wdt", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 27, 0 },
	{ GATE_CLK_APB_PWM, "clk_gate_apb_pwm", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 28, 0 },
	{ GATE_CLK_APB_RTC, "clk_gate_apb_rtc", "clk_gate_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 29, 0 },

	{ GATE_CLK_SYSDMA_AXI, "clk_gate_sysdma_axi", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 3, 0 },
	{ GATE_CLK_APB_UART, "clk_gate_apb_uart", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 5, 0 },
	{ GATE_CLK_AXI_DBG_I2C, "clk_gate_axi_dbg_i2c", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 6, 0 },
	{ GATE_CLK_APB_SPI, "clk_gate_apb_spi", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 25, 0 },
	{ GATE_CLK_AXI_ETH0, "clk_gate_axi_eth0", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 31, 0 },
	{ GATE_CLK_AXI_EMMC, "clk_gate_axi_emmc", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 2, 0 },
	{ GATE_CLK_AXI_SD, "clk_gate_axi_sd", "clk_gate_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 5, 0 },

	{ GATE_CLK_TIMER1, "clk_gate_timer1", "clk_div_timer1",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 12, 0 },
	{ GATE_CLK_TIMER2, "clk_gate_timer2", "clk_div_timer2",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 13, 0 },
	{ GATE_CLK_TIMER3, "clk_gate_timer3", "clk_div_timer3",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 14, 0 },
	{ GATE_CLK_TIMER4, "clk_gate_timer4", "clk_div_timer4",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 15, 0 },
	{ GATE_CLK_TIMER5, "clk_gate_timer5", "clk_div_timer5",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 16, 0 },
	{ GATE_CLK_TIMER6, "clk_gate_timer6", "clk_div_timer6",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 17, 0 },
	{ GATE_CLK_TIMER7, "clk_gate_timer7", "clk_div_timer7",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 18, 0 },
	{ GATE_CLK_TIMER8, "clk_gate_timer8", "clk_div_timer8",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 19, 0 },
	{ GATE_CLK_100K_EMMC, "clk_gate_100k_emmc", "clk_div_100k_emmc",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 4, 0 },
	{ GATE_CLK_100K_SD, "clk_gate_100k_sd", "clk_div_100k_sd",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 7, 0 },
	{ GATE_CLK_GPIO_DB, "clk_gate_gpio_db", "clk_div_gpio_db",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 24, 0 },

	{ GATE_CLK_DDR01, "clk_gate_ddr01", "clk_mux_ddr01",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2004, 14, 0 },
	{ GATE_CLK_DDR23, "clk_gate_ddr23", "clk_mux_ddr23",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2004, 15, 0 },
	{ GATE_CLK_RP_CPU_NORMAL, "clk_gate_rp_cpu_normal", "clk_mux_rp_cpu_normal",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 0, 0 },
	{ GATE_CLK_AXI_DDR, "clk_gate_axi_ddr", "clk_mux_axi_ddr",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2004, 13, 0 },

	/* rxu0-31 */
	{ GATE_CLK_RXU0, "clk_gate_rxu0", "clk_gate_rp_cpu_normal",
		0, 0x368, 0, 0 },
	{ GATE_CLK_RXU1, "clk_gate_rxu1", "clk_gate_rp_cpu_normal",
		0, 0x368, 1, 0 },
	{ GATE_CLK_RXU2, "clk_gate_rxu2", "clk_gate_rp_cpu_normal",
		0, 0x368, 2, 0 },
	{ GATE_CLK_RXU3, "clk_gate_rxu3", "clk_gate_rp_cpu_normal",
		0, 0x368, 3, 0 },
	{ GATE_CLK_RXU4, "clk_gate_rxu4", "clk_gate_rp_cpu_normal",
		0, 0x368, 4, 0 },
	{ GATE_CLK_RXU5, "clk_gate_rxu5", "clk_gate_rp_cpu_normal",
		0, 0x368, 5, 0 },
	{ GATE_CLK_RXU6, "clk_gate_rxu6", "clk_gate_rp_cpu_normal",
		0, 0x368, 6, 0 },
	{ GATE_CLK_RXU7, "clk_gate_rxu7", "clk_gate_rp_cpu_normal",
		0, 0x368, 7, 0 },
	{ GATE_CLK_RXU8, "clk_gate_rxu8", "clk_gate_rp_cpu_normal",
		0, 0x368, 8, 0 },
	{ GATE_CLK_RXU9, "clk_gate_rxu9", "clk_gate_rp_cpu_normal",
		0, 0x368, 9, 0 },
	{ GATE_CLK_RXU10, "clk_gate_rxu10", "clk_gate_rp_cpu_normal",
		0, 0x368, 10, 0 },
	{ GATE_CLK_RXU11, "clk_gate_rxu11", "clk_gate_rp_cpu_normal",
		0, 0x368, 11, 0 },
	{ GATE_CLK_RXU12, "clk_gate_rxu12", "clk_gate_rp_cpu_normal",
		0, 0x368, 12, 0 },
	{ GATE_CLK_RXU13, "clk_gate_rxu13", "clk_gate_rp_cpu_normal",
		0, 0x368, 13, 0 },
	{ GATE_CLK_RXU14, "clk_gate_rxu14", "clk_gate_rp_cpu_normal",
		0, 0x368, 14, 0 },
	{ GATE_CLK_RXU15, "clk_gate_rxu15", "clk_gate_rp_cpu_normal",
		0, 0x368, 15, 0 },
	{ GATE_CLK_RXU16, "clk_gate_rxu16", "clk_gate_rp_cpu_normal",
		0, 0x368, 16, 0 },
	{ GATE_CLK_RXU17, "clk_gate_rxu17", "clk_gate_rp_cpu_normal",
		0, 0x368, 17, 0 },
	{ GATE_CLK_RXU18, "clk_gate_rxu18", "clk_gate_rp_cpu_normal",
		0, 0x368, 18, 0 },
	{ GATE_CLK_RXU19, "clk_gate_rxu19", "clk_gate_rp_cpu_normal",
		0, 0x368, 19, 0 },
	{ GATE_CLK_RXU20, "clk_gate_rxu20", "clk_gate_rp_cpu_normal",
		0, 0x368, 20, 0 },
	{ GATE_CLK_RXU21, "clk_gate_rxu21", "clk_gate_rp_cpu_normal",
		0, 0x368, 21, 0 },
	{ GATE_CLK_RXU22, "clk_gate_rxu22", "clk_gate_rp_cpu_normal",
		0, 0x368, 22, 0 },
	{ GATE_CLK_RXU23, "clk_gate_rxu23", "clk_gate_rp_cpu_normal",
		0, 0x368, 23, 0 },
	{ GATE_CLK_RXU24, "clk_gate_rxu24", "clk_gate_rp_cpu_normal",
		0, 0x368, 24, 0 },
	{ GATE_CLK_RXU25, "clk_gate_rxu25", "clk_gate_rp_cpu_normal",
		0, 0x368, 25, 0 },
	{ GATE_CLK_RXU26, "clk_gate_rxu26", "clk_gate_rp_cpu_normal",
		0, 0x368, 26, 0 },
	{ GATE_CLK_RXU27, "clk_gate_rxu27", "clk_gate_rp_cpu_normal",
		0, 0x368, 27, 0 },
	{ GATE_CLK_RXU28, "clk_gate_rxu28", "clk_gate_rp_cpu_normal",
		0, 0x368, 28, 0 },
	{ GATE_CLK_RXU29, "clk_gate_rxu29", "clk_gate_rp_cpu_normal",
		0, 0x368, 29, 0 },
	{ GATE_CLK_RXU30, "clk_gate_rxu30", "clk_gate_rp_cpu_normal",
		0, 0x368, 30, 0 },
	{ GATE_CLK_RXU31, "clk_gate_rxu31", "clk_gate_rp_cpu_normal",
		0, 0x368, 31, 0 },

	/* rv cluster0-15 */
	{ GATE_CLK_MP0, "clk_gate_mp0", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x384, 0, 0 },
	{ GATE_CLK_MP1, "clk_gate_mp1", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x38c, 0, 0 },
	{ GATE_CLK_MP2, "clk_gate_mp2", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x394, 0, 0 },
	{ GATE_CLK_MP3, "clk_gate_mp3", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x39c, 0, 0 },
	{ GATE_CLK_MP4, "clk_gate_mp4", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3a4, 0, 0 },
	{ GATE_CLK_MP5, "clk_gate_mp5", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3ac, 0, 0 },
	{ GATE_CLK_MP6, "clk_gate_mp6", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3b4, 0, 0 },
	{ GATE_CLK_MP7, "clk_gate_mp7", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3bc, 0, 0 },
	{ GATE_CLK_MP8, "clk_gate_mp8", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3c4, 0, 0 },
	{ GATE_CLK_MP9, "clk_gate_mp9", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3cc, 0, 0 },
	{ GATE_CLK_MP10, "clk_gate_mp10", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3d4, 0, 0 },
	{ GATE_CLK_MP11, "clk_gate_mp11", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3dc, 0, 0 },
	{ GATE_CLK_MP12, "clk_gate_mp12", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3e4, 0, 0 },
	{ GATE_CLK_MP13, "clk_gate_mp13", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3ec, 0, 0 },
	{ GATE_CLK_MP14, "clk_gate_mp14", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3f4, 0, 0 },
	{ GATE_CLK_MP15, "clk_gate_mp15", "clk_gate_rp_cpu_normal",
		CLK_IS_CRITICAL, 0x3fc, 0, 0 },
};

/* mux clocks */
static const char *const clk_mux_ddr01_p[] = {
			"clk_div_ddr01_0", "clk_div_ddr01_1"};
static const char *const clk_mux_ddr23_p[] = {
			"clk_div_ddr23_0", "clk_div_ddr23_1"};
static const char *const clk_mux_rp_cpu_normal_p[] = {
			"clk_div_rp_cpu_normal_0", "clk_div_rp_cpu_normal_1"};
static const char *const clk_mux_axi_ddr_p[] = {
			"clk_div_axi_ddr_0", "clk_div_axi_ddr_1"};

struct mango_mux_clock mux_clks[] = {
	{
		MUX_CLK_DDR01, "clk_mux_ddr01", clk_mux_ddr01_p,
		ARRAY_SIZE(clk_mux_ddr01_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 2, 1, 0,
	},
	{
		MUX_CLK_DDR23, "clk_mux_ddr23", clk_mux_ddr23_p,
		ARRAY_SIZE(clk_mux_ddr23_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 3, 1, 0,
	},
	{
		MUX_CLK_RP_CPU_NORMAL, "clk_mux_rp_cpu_normal", clk_mux_rp_cpu_normal_p,
		ARRAY_SIZE(clk_mux_rp_cpu_normal_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 0, 1, 0,
	},
	{
		MUX_CLK_AXI_DDR, "clk_mux_axi_ddr", clk_mux_axi_ddr_p,
		ARRAY_SIZE(clk_mux_axi_ddr_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 1, 1, 0,
	},
};

struct mango_clk_table pll_clk_tables = {
	.pll_clks_num = ARRAY_SIZE(mango_root_pll_clks),
	.pll_clks = mango_root_pll_clks,
};

struct mango_clk_table div_clk_tables[] = {
	{
		.id = DIV_CLK_TABLE,
		.div_clks_num = ARRAY_SIZE(div_clks),
		.div_clks = div_clks,
		.gate_clks_num = ARRAY_SIZE(gate_clks),
		.gate_clks = gate_clks,
	},
};

struct mango_clk_table mux_clk_tables[] = {
	{
		.id = MUX_CLK_TABLE,
		.mux_clks_num = ARRAY_SIZE(mux_clks),
		.mux_clks = mux_clks,
	},
};

static const struct of_device_id mango_clk_match_ids_tables[] = {
	{
		.compatible = "mango, pll-clock",
		.data = &pll_clk_tables,
	},
	{
		.compatible = "mango, pll-child-clock",
		.data = div_clk_tables,
	},
	{
		.compatible = "mango, pll-mux-clock",
		.data = mux_clk_tables,
	},
	{
		.compatible = "mango, clk-default-rates",
	},
	{
		.compatible = "mango, dm-pll-clock",
		.data = &pll_clk_tables,
	},
	{
		.compatible = "mango, dm-pll-child-clock",
		.data = div_clk_tables,
	},
	{
		.compatible = "mango, dm-pll-mux-clock",
		.data = mux_clk_tables,
	},
	{
		.compatible = "mango, dm-clk-default-rates",
	},
	{}
};

static void __init mango_clk_init(struct device_node *node)
{
	struct device_node *np_top;
	struct mango_clk_data *clk_data = NULL;
	const struct mango_clk_table *dev_data;
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
	match = of_match_node(mango_clk_match_ids_tables, node);
	if (match) {
		dev_data = (struct mango_clk_table *)match->data;
	} else {
		pr_err("%s did't match node data\n", __func__);
		ret = -ENODEV;
		goto no_match_data;
	}

	spin_lock_init(&clk_data->lock);
	if (of_device_is_compatible(node, "mango, pll-clock") ||
	    of_device_is_compatible(node, "mango, dm-pll-clock")) {
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
		if (of_device_is_compatible(node, "mango, pll-clock"))
			ret = mango_register_pll_clks(node, clk_data, id);
		else
			ret = dm_mango_register_pll_clks(node, clk_data, id);
	}

	if (of_device_is_compatible(node, "mango, pll-child-clock") ||
	    of_device_is_compatible(node, "mango, dm-pll-child-clock")) {
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
		if (of_device_is_compatible(node, "mango, pll-child-clock"))
			ret = mango_register_div_clks(node, clk_data);
		else
			ret = dm_mango_register_div_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "mango, pll-mux-clock") ||
	    of_device_is_compatible(node, "mango, dm-pll-mux-clock")) {
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
		if (of_device_is_compatible(node, "mango, pll-mux-clock"))
			ret = mango_register_mux_clks(node, clk_data);
		else
			ret = dm_mango_register_mux_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "mango, clk-default-rates"))
		ret = set_default_clk_rates(node);

	if (of_device_is_compatible(node, "mango, dm-clk-default-rates"))
		ret = dm_set_default_clk_rates(node);

	if (!ret)
		return;

no_match_data:
	kfree(clk_data);

out:
	pr_err("%s failed error number %d\n", __func__, ret);
}

CLK_OF_DECLARE(mango_clk_pll, "mango, pll-clock", mango_clk_init);
CLK_OF_DECLARE(mango_clk_pll_child, "mango, pll-child-clock", mango_clk_init);
CLK_OF_DECLARE(mango_clk_pll_mux, "mango, pll-mux-clock", mango_clk_init);
CLK_OF_DECLARE(mango_clk_default_rate, "mango, clk-default-rates", mango_clk_init);
CLK_OF_DECLARE(dm_mango_clk_pll, "mango, dm-pll-clock", mango_clk_init);
CLK_OF_DECLARE(dm_mango_clk_pll_child, "mango, dm-pll-child-clock", mango_clk_init);
CLK_OF_DECLARE(dm_mango_clk_pll_mux, "mango, dm-pll-mux-clock", mango_clk_init);
CLK_OF_DECLARE(dm_mango_clk_default_rate, "mango, dm-clk-default-rates", mango_clk_init);
