#ifndef __SOPHGO_2044_CLOCK__
#define __SOPHGO_2044_CLOCK__

#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/clkdev.h>

#include <dt-bindings/clock/sophgo,sg2044-clock.h>

#define KHZ 1000L
#define MHZ (KHZ * KHZ)

#define SG2044_CLK_USE_INIT_VAL		BIT(0) /* use default value */
#define SG2044_CLK_USE_REG_VAL		BIT(1) /* use reg divider value */
#define SG2044_CLK_RO			BIT(2) /* use reg divider value */

#define CLK_PLL	BIT(0)
#define CLK_MUX	BIT(1)

#define PLL_CTRL_OFFSET	0xC4
#define PLL_STAT_LOCK_OFFSET	0x10
#define PLL_SELECT_OFFSET	0x2020
#define CLK_MODE	0x4
#define CLK_MODE_MASK	0x3

#define REFDIV_MIN 1
#define REFDIV_MAX 63
#define FBDIV_MIN 8
#define FBDIV_MAX 1066

#define PLL_FREQ_MIN (1600 * MHZ)
#define PLL_FREQ_MAX (3200 * MHZ)

#define div_mask(width) ((1 << (width)) - 1)
#define TOP_PLL_CTRL(fbdiv, p1, p2, refdiv) \
	(BIT(30) | (0x2 << 27) | BIT(24) | (((p2 - 1) & 0x7) << 21) | \
	(((p1 - 1) & 0x7) << 18) | ((refdiv & 0x3f) << 12) | (fbdiv & 0xfff))

struct sg2044_pll_ctrl {
	unsigned int mode;
	unsigned long freq;

	unsigned int fbdiv;
	unsigned int postdiv1;
	unsigned int postdiv2;
	unsigned int refdiv;
};

struct sg2044_pll_clock {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	struct clk_hw	hw;
	struct regmap *syscon_top;

	/* Below lock used to protect PLL top register during write */
	spinlock_t *lock;
	u32 ini_flags;

	u32 status_offset;
	u32 enable_offset;

	struct sg2044_pll_ctrl pctrl_table[4];
};

#define to_sg2044_pll_clk(_hw) container_of(_hw, struct sg2044_pll_clock, hw)

#define to_sg2044_clk_divider(_hw)	\
	container_of(_hw, struct sg2044_clk_divider, hw)

#define to_sg2044_clk_mux(nb) \
	container_of(nb, struct sg2044_mux_clock, clk_nb)

struct sg2044_divider_clock {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u8 shift;
	u8 width;
	u8 div_flags;
	u32 initial_sel;
	u32 initial_val;
	struct clk_div_table *table;
};

struct sg2044_mux_clock {
	unsigned int id;
	const char *name;
	const char *const *parent_names;
	u8 num_parents;
	unsigned long flags;
	unsigned long offset;
	u8 shift;
	u8 width;
	u8 mux_flags;
	u32 *table;

	struct notifier_block clk_nb;
};

struct sg2044_gate_clock {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u8 bit_idx;
	u8 gate_flags;
	const char *alias;
};

struct sg2044_clk_table {
	u32 id;
	u32 pll_clks_num;
	u32 div_clks_num;
	u32 gate_clks_num;
	u32 mux_clks_num;

	const struct sg2044_pll_clock *pll_clks;
	const struct sg2044_divider_clock *div_clks;
	const struct sg2044_gate_clock *gate_clks;
	const struct sg2044_mux_clock *mux_clks;
};

struct sg2044_clk_data {
	void __iomem *base;
	spinlock_t lock;
	struct regmap *syscon_top;
	struct clk_onecell_data	clk_data;
	const struct sg2044_clk_table *table;
};

int sg2044_register_mux_clks
(struct device_node *node, struct sg2044_clk_data *clk_data);
int sg2044_register_div_clks
(struct device_node *node, struct sg2044_clk_data *clk_data);
int sg2044_register_pll_clks
(struct device_node *node, struct sg2044_clk_data *clk_data, unsigned int id);
int set_default_clk_rates(struct device_node *node);
#endif
