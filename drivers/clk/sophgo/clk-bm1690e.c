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
#include <dt-bindings/clock/sophgo,bm1690e-clock.h>

#include "clk.h"

/* fixed clocks */
struct sg2044_pll_clock bm1690e_root_pll_clks[] = {
	{
		.id = MPLL0_CLK,
		.name = "mpll0_clock",
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
		.id = FPLL0_CLK,
		.name = "fpll0_clock",
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
	},
};

/* divider clocks */
static const struct sg2044_divider_clock bm1690e_div_clks[] = {
	{ DIV_CLK_MPLL0_AP_CPU_NORMAL_0, "clk_div_ap_sys_0", "clk_gate_ap_sys_div0",
		0, 0x2040, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL2_TPU_SYS_0, "clk_div_tpu_sys_0", "clk_gate_tpu_sys_div0",
		0, 0x204c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_MPLL3_CC_GRP_SYS_0, "clk_div_ccgrp_sys_0", "clk_gate_ccgrp_sys_div0",
		0, 0x2068, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_CC_GRP_SYS_1, "clk_div_ccgrp_sys_1", "clk_gate_cc_grp_sys_div1",
		0, 0x206c, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TPU_SYS_1, "clk_div_tpu_sys_1", "clk_gate_tpu_sys_div1",
		0, 0x2050, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_AP_CPU_NORMAL_1, "clk_div_ap_sys_1", "clk_gate_ap_sys_div1",
		0, 0x2044, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_TOP_50M, "clk_div_top_50m", "fpll0_clock",
		0, 0x2048, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER1, "clk_div_timer1", "clk_div_top_50m",
		0, 0x2094, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER2, "clk_div_timer2", "clk_div_top_50m",
		0, 0x2098, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER3, "clk_div_timer3", "clk_div_top_50m",
		0, 0x209c, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER4, "clk_div_timer4", "clk_div_top_50m",
		0, 0x20a0, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER5, "clk_div_timer5", "clk_div_top_50m",
		0, 0x20a4, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER6, "clk_div_timer6", "clk_div_top_50m",
		0, 0x20a8, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER7, "clk_div_timer7", "clk_div_top_50m",
		0, 0x20ac, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_DIV_TMIER8, "clk_div_timer8", "clk_div_top_50m",
		0, 0x20b0, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_UART_500M, "clk_div_uart_500m", "fpll0_clock",
		0, 0x2090, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_EFUSE, "clk_div_efuse", "fpll0_clock",
		0, 0x20b8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_TOP_AXI0, "clk_div_top_axi0", "fpll0_clock",
		0, 0x20dc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_FPLL0_DIV_GPIO_DB, "clk_div_gpio_db", "clk_div_top_axi0",
		0, 0x20bc, 16, 16, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },
	{ DIV_CLK_FPLL0_TOP_AXI_HSPERI, "clk_div_top_axi_hsperi", "fpll0_clock",
		0, 0x20e0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_REG_VAL, },

	{ DIV_CLK_DPLL0_DDR0_0, "clk_div_ddr0_0", "dpll0_clock",
		0, 0x20e4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR0_1, "clk_div_ddr0_1", "fpll0_clock",
		0, 0x20e8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL1_DDR1_0, "clk_div_ddr1_0", "dpll1_clock",
		0, 0x20ec, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR1_1, "clk_div_ddr1_1", "fpll0_clock",
		0, 0x20f0, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL2_DDR2_0, "clk_div_ddr2_0", "dpll2_clock",
		0, 0x20f4, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR2_1, "clk_div_ddr2_1", "fpll0_clock",
		0, 0x20f8, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_DPLL3_DDR3_0, "clk_div_ddr3_0", "dpll3_clock",
		0, 0x20fc, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
	{ DIV_CLK_FPLL0_DDR3_1, "clk_div_ddr3_1", "fpll0_clock",
		0, 0x2100, 16, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, SG2044_CLK_USE_INIT_VAL, },
};

/* gate clocks */
static const struct sg2044_gate_clock bm1690e_gate_clks[] = {
	{ GATE_CLK_AP_CPU_NORMAL_DIV0, "clk_gate_ap_sys_div0", "mpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2040, 4, 0 },

	{ GATE_CLK_TPU_SYS_DIV0, "clk_gate_tpu_sys_div0", "mpll2_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x204c, 4, 0 },
	{ GATE_CLK_CC_GRP_SYS_DIV0, "clk_gate_ccgrp_sys_div0", "mpll3_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2068, 4, 0 },

	{ GATE_CLK_AP_CPU_NORMAL_DIV1, "clk_gate_ap_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2044, 4, 0 },

	{ GATE_CLK_TPU_SYS_DIV1, "clk_gate_tpu_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2050, 4, 0 },
	{ GATE_CLK_CC_GRP_SYS_DIV1, "clk_gate_cc_grp_sys_div1", "fpll0_clock",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x206c, 4, 0 },

	{ GATE_CLK_AP_CPU_NORMAL, "clk_gate_ap_sys", "clk_mux_ap_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 0, 0 },

	{ GATE_CLK_TPU_SYS, "clk_gate_tpu_sys", "clk_mux_tpu_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 2, 0 },
	{ GATE_CLK_CC_GRP_SYS, "clk_gate_ccgrp_sys", "clk_mux_ccgrp_sys",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0x2000, 8, 0 },

	{ GATE_CLK_DDR0, "clk_gate_ddr0", "clk_mux_ddr0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 26, 0 },
	{ GATE_CLK_DDR1, "clk_gate_ddr1", "clk_mux_ddr1",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 27, 0 },
	{ GATE_CLK_DDR2, "clk_gate_ddr2", "clk_mux_ddr2",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 28, 0 },
	{ GATE_CLK_DDR3, "clk_gate_ddr3", "clk_mux_ddr3",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 29, 0 },

	{ GATE_CLK_TOP_50M, "clk_gate_top_50m", "clk_div_top_50m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 1, 0 },

	{ GATE_CLK_TIMER1, "clk_gate_timer1", "clk_div_timer1",
		CLK_SET_RATE_PARENT, 0x2000, 27, 0 },
	{ GATE_CLK_TIMER2, "clk_gate_timer2", "clk_div_timer2",
		CLK_SET_RATE_PARENT, 0x2000, 28, 0 },
	{ GATE_CLK_TIMER3, "clk_gate_timer3", "clk_div_timer3",
		CLK_SET_RATE_PARENT, 0x2000, 29, 0 },
	{ GATE_CLK_TIMER4, "clk_gate_timer4", "clk_div_timer4",
		CLK_SET_RATE_PARENT, 0x2000, 30, 0 },
	{ GATE_CLK_TIMER5, "clk_gate_timer5", "clk_div_timer5",
		CLK_SET_RATE_PARENT, 0x2000, 31, 0 },
	{ GATE_CLK_TIMER6, "clk_gate_timer6", "clk_div_timer6",
		CLK_SET_RATE_PARENT, 0x2004, 0, 0 },
	{ GATE_CLK_TIMER7, "clk_gate_timer7", "clk_div_timer7",
		CLK_SET_RATE_PARENT, 0x2004, 1, 0 },
	{ GATE_CLK_TIMER8, "clk_gate_timer8", "clk_div_timer8",
		CLK_SET_RATE_PARENT, 0x2004, 2, 0 },
	{ GATE_CLK_UART_500M, "clk_gate_uart_500m", "clk_div_uart_500m",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 20, 0 },
	{ GATE_CLK_EFUSE, "clk_gate_efuse", "clk_div_efuse",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 4, 0 },

	{ GATE_CLK_TOP_AXI0, "clk_gate_top_axi0", "clk_div_top_axi0",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 24, 0 },

	{ GATE_CLK_APB_PWM, "clk_gate_apb_pwm", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 12, 0 },
	{ GATE_CLK_APB_WDT, "clk_gate_apb_wdt", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 11, 0 },
	{ GATE_CLK_APB_I2C, "clk_gate_apb_i2c", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 10, 0 },
	{ GATE_CLK_GPIO_DB, "clk_gate_gpio_db", "clk_div_gpio_db",
		CLK_IS_CRITICAL, 0x2004, 8, 0 },
	{ GATE_CLK_APB_GPIO_INTR, "clk_gate_apb_gpio_intr", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 7, 0 },
	{ GATE_CLK_APB_GPIO, "clk_gate_apb_gpio", "clk_div_top_axi0",
		CLK_IS_CRITICAL, 0x2004, 6, 0 },
	{ GATE_CLK_APB_EFUSE, "clk_gate_apb_efuse", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2004, 4, 0 },
	{ GATE_CLK_APB_TIMER, "clk_gate_apb_timer", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 26, 0 },
	{ GATE_CLK_AXI_SRAM, "clk_gate_axi_sram", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 25, 0 },
	{ GATE_CLK_AHB_SF, "clk_gate_ahb_sf", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 24, 0 },
	{ GATE_CLK_APB_ROM, "clk_gate_apb_rom", "clk_div_top_axi0",
		CLK_IGNORE_UNUSED, 0x2000, 23, 0 },
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

	{ GATE_CLK_TOP_AXI_HSPERI, "clk_gate_top_axi_hsperi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 25, 0 },

	{ GATE_CLK_APB_SPI, "clk_gate_apb_spi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2004, 9, 0 },
	{ GATE_CLK_AXI_DBG_I2C, "clk_gate_axi_dbg_i2c", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 22, 0 },
	{ GATE_CLK_APB_UART, "clk_gate_apb_uart", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 21, 0 },
	{ GATE_CLK_SYSDMA_AXI, "clk_gate_sysdma_axi", "clk_div_top_axi_hsperi",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2000, 19, 0 },
};

/* mux clocks */
static const char *const bm1690e_clk_mux_ddr0_p[] = {
			"clk_div_ddr0_1", "clk_div_ddr0_0"};
static const char *const bm1690e_clk_mux_ddr1_p[] = {
			"clk_div_ddr1_1", "clk_div_ddr1_0"};
static const char *const bm1690e_clk_mux_ddr2_p[] = {
			"clk_div_ddr2_1", "clk_div_ddr2_0"};
static const char *const bm1690e_clk_mux_ddr3_p[] = {
			"clk_div_ddr3_1", "clk_div_ddr3_0"};

static const char *const bm1690e_clk_mux_cc_grp_sys_p[] = {
			"clk_div_ccgrp_sys_1", "clk_div_ccgrp_sys_0"};
static const char *const bm1690e_clk_mux_tpu_sys_p[] = {
			"clk_div_tpu_sys_1", "clk_div_tpu_sys_0"};

static const char *const bm1690e_clk_mux_ap_cpu_normal_p[] = {
			"clk_div_ap_sys_1", "clk_div_ap_sys_0"};

struct sg2044_mux_clock bm1690e_mux_clks[] = {
	{
		MUX_CLK_DDR0, "clk_mux_ddr0", bm1690e_clk_mux_ddr0_p,
		ARRAY_SIZE(bm1690e_clk_mux_ddr0_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 3, 1, 0,
	}, {
		MUX_CLK_DDR1, "clk_mux_ddr1", bm1690e_clk_mux_ddr1_p,
		ARRAY_SIZE(bm1690e_clk_mux_ddr1_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 4, 1, 0,
	}, {
		MUX_CLK_DDR2, "clk_mux_ddr2", bm1690e_clk_mux_ddr2_p,
		ARRAY_SIZE(bm1690e_clk_mux_ddr2_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 5, 1, 0,
	}, {
		MUX_CLK_DDR3, "clk_mux_ddr3", bm1690e_clk_mux_ddr3_p,
		ARRAY_SIZE(bm1690e_clk_mux_ddr3_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT |
			CLK_MUX_READ_ONLY,
		0x2020, 6, 1, 0,
	}, {
		MUX_CLK_CC_GRP_SYS, "clk_mux_ccgrp_sys", bm1690e_clk_mux_cc_grp_sys_p,
		ARRAY_SIZE(bm1690e_clk_mux_cc_grp_sys_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 2, 1, 0,
	}, {
		MUX_CLK_TPU_SYS, "clk_mux_tpu_sys", bm1690e_clk_mux_tpu_sys_p,
		ARRAY_SIZE(bm1690e_clk_mux_tpu_sys_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 1, 1, 0,
	}, {
		MUX_CLK_AP_CPU_NORMAL, "clk_mux_ap_sys", bm1690e_clk_mux_ap_cpu_normal_p,
		ARRAY_SIZE(bm1690e_clk_mux_ap_cpu_normal_p),
		CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		0x2020, 0, 1, 0,
	},
};

struct sg2044_clk_table bm1690e_pll_clk_tables = {
	.pll_clks_num = ARRAY_SIZE(bm1690e_root_pll_clks),
	.pll_clks = bm1690e_root_pll_clks,
};

struct sg2044_clk_table bm1690e_div_clk_tables[] = {
	{
		.id = DIV_CLK_TABLE,
		.div_clks_num = ARRAY_SIZE(bm1690e_div_clks),
		.div_clks = bm1690e_div_clks,
		.gate_clks_num = ARRAY_SIZE(bm1690e_gate_clks),
		.gate_clks = bm1690e_gate_clks,
	},
};

struct sg2044_clk_table bm1690e_mux_clk_tables[] = {
	{
		.id = MUX_CLK_TABLE,
		.mux_clks_num = ARRAY_SIZE(bm1690e_mux_clks),
		.mux_clks = bm1690e_mux_clks,
	},
};

static const struct of_device_id bm1690e_clk_match_ids_tables[] = {
	{
		.compatible = "bm1690e, pll-clock",
		.data = &bm1690e_pll_clk_tables,
	},
	{
		.compatible = "bm1690e, pll-child-clock",
		.data = bm1690e_div_clk_tables,
	},
	{
		.compatible = "bm1690e, pll-mux-clock",
		.data = bm1690e_mux_clk_tables,
	},
	{
		.compatible = "bm1690e, clk-default-rates",
	},
	{}
};

static void __init bm1690e_clk_init(struct device_node *node)
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
	
	match = of_match_node(bm1690e_clk_match_ids_tables, node);
	if (match) {
		dev_data = (struct sg2044_clk_table *)match->data;
	} else {
		pr_err("%s did't match bm1690e node data\n", __func__);
		ret = -ENODEV;
		goto no_match_data;
	}

	spin_lock_init(&clk_data->lock);
	if (of_device_is_compatible(node, "bm1690e, pll-clock")) {
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

	if (of_device_is_compatible(node, "bm1690e, pll-child-clock")) {
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			pr_err("not assigned id for %s\n", node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}

		/* Below brute-force to check dts property "id"
		 * whether match id of array
		 */
		for (i = 0; i < ARRAY_SIZE(bm1690e_div_clk_tables); i++) {
			if (id == dev_data[i].id)
				break; /* found */
		}
		clk_data->table = &dev_data[i];
		clk_data->base = base;
		clk_data->syscon_top = syscon;
		ret = sg2044_register_div_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "bm1690e, pll-mux-clock")) {
		ret = of_property_read_u32(node, "id", &id);
		if (ret) {
			pr_err("not assigned id for %s\n", node->full_name);
			ret = -ENODEV;
			goto no_match_data;
		}

		/* Below brute-force to check dts property "id"
		 * whether match id of array
		 */
		for (i = 0; i < ARRAY_SIZE(bm1690e_mux_clk_tables); i++) {
			if (id == dev_data[i].id)
				break; /* found */
		}
		clk_data->table = &dev_data[i];
		clk_data->base = base;
		clk_data->syscon_top = syscon;
		ret = sg2044_register_mux_clks(node, clk_data);
	}

	if (of_device_is_compatible(node, "bm1690e, clk-default-rates"))
		ret = set_default_clk_rates(node);

	if (!ret)
		return;

no_match_data:
	kfree(clk_data);

out:
	pr_err("%s failed error number %d\n", __func__, ret);
}

CLK_OF_DECLARE(bm1690e_clk_pll, "bm1690e, pll-clock", bm1690e_clk_init);
CLK_OF_DECLARE(bm1690e_clk_pll_child, "bm1690e, pll-child-clock", bm1690e_clk_init);
CLK_OF_DECLARE(bm1690e_clk_pll_mux, "bm1690e, pll-mux-clock", bm1690e_clk_init);
CLK_OF_DECLARE(bm1690e_clk_default_rate, "bm1690e, clk-default-rates", bm1690e_clk_init);
