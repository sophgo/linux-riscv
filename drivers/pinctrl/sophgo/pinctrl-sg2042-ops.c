// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo sg2042 SoCs pinctrl driver.
 *
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinconf.h"
#include "../pinmux.h"

#include "pinctrl-sg2042.h"

#define PIN_IO_PULL_ONE_ENABLE		BIT(0)
#define PIN_IO_PULL_DIR_UP		(BIT(1) | PIN_IO_PULL_ONE_ENABLE)
#define PIN_IO_PULL_DIR_DOWN		(0 | PIN_IO_PULL_ONE_ENABLE)
#define PIN_IO_PULL_ONE_MASK		GENMASK(1, 0)

#define PIN_IO_PULL_UP			BIT(2)
#define PIN_IO_PULL_UP_DONW		BIT(3)
#define PIN_IO_PULL_UP_MASK		GENMASK(3, 2)

#define PIN_IO_MUX			GENMASK(5, 4)
#define PIN_IO_DRIVE			GENMASK(9, 6)
#define PIN_IO_SCHMITT_ENABLE		BIT(10)
#define PIN_IO_OUTPUT_ENABLE		BIT(11)

struct sg2042_pinctrl {
	struct device				*dev;
	struct pinctrl_dev			*pctl_dev;
	const struct sg2042_pinctrl_data	*data;
	struct pinctrl_desc			pdesc;

	struct mutex				mutex;
	raw_spinlock_t				lock;

	void __iomem				*regs;
};

struct sg2042_pin_mux_config {
	const struct sg2042_pin		*pin;
	u32				config;
};

static u16 sg2042_dt_get_pin(u32 value)
{
	return value;
}

static u8 sg2042_dt_get_pin_mux(u32 value)
{
	return value >> 16;
}

static const struct sg2042_pin *sg2042_get_pin(struct sg2042_pinctrl *pctrl,
					       unsigned long pin)
{
	if (pin < pctrl->data->npins)
		return &pctrl->data->pindata[pin];
	return NULL;
}

static inline u32 sg2042_get_pin_reg(struct sg2042_pinctrl *pctrl,
				     const struct sg2042_pin *pin)
{
	void __iomem *reg = pctrl->regs + pin->offset;

	if (pin->flags & PIN_FLAG_WRITE_HIGH)
		return readl(reg) >> 16;
	else
		return readl(reg) & 0xffff;
}

static inline void sg2042_set_pin_reg(struct sg2042_pinctrl *pctrl,
				      const struct sg2042_pin *pin,
				      u32 value, u32 mask)
{
	void __iomem *reg = pctrl->regs + pin->offset;
	u32 v = readl(reg);

	if (pin->flags & PIN_FLAG_WRITE_HIGH) {
		v &= ~(mask << 16);
		v |= value << 16;
	} else {
		v &= ~mask;
		v |= value;
	}

	writel(v, reg);
}

static void sg2042_pctrl_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *seq, unsigned int pin_id)
{
	struct sg2042_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct sg2042_pin *pin = sg2042_get_pin(pctrl, pin_id);
	u32 value, mux;

	value = sg2042_get_pin_reg(pctrl, pin);
	mux = FIELD_GET(PIN_IO_MUX, value);
	seq_printf(seq, "mux:%u reg:0x%04x ", mux, value);
}

static int sg2042_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				       struct device_node *np,
				       struct pinctrl_map **maps,
				       unsigned int *num_maps)
{
	struct sg2042_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pctrl->dev;
	struct pinctrl_map *map;
	const char **grpnames;
	const char *grpname;
	int ngroups = 0;
	int nmaps = 0;
	int ret;

	for_each_available_child_of_node_scoped(np, child)
		ngroups += 1;

	grpnames = devm_kcalloc(dev, ngroups, sizeof(*grpnames), GFP_KERNEL);
	if (!grpnames)
		return -ENOMEM;

	map = kcalloc(ngroups * 2, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	ngroups = 0;
	guard(mutex)(&pctrl->mutex);
	for_each_available_child_of_node_scoped(np, child) {
		int npins = of_property_count_u32_elems(child, "pinmux");
		unsigned int *pins;
		struct sg2042_pin_mux_config *pinmuxs;
		u32 config;
		int i;

		if (npins < 1) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn\n",
				np, child);
			ret = -EINVAL;
			goto failed;
		}

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn",
					 np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto failed;
		}

		grpnames[ngroups++] = grpname;

		pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
		if (!pins) {
			ret = -ENOMEM;
			goto failed;
		}

		pinmuxs = devm_kcalloc(dev, npins, sizeof(*pinmuxs), GFP_KERNEL);
		if (!pinmuxs) {
			ret = -ENOMEM;
			goto failed;
		}

		for (i = 0; i < npins; i++) {
			ret = of_property_read_u32_index(child, "pinmux",
							 i, &config);
			if (ret)
				goto failed;

			pins[i] = sg2042_dt_get_pin(config);
			pinmuxs[i].config = config;
			pinmuxs[i].pin = sg2042_get_pin(pctrl, pins[i]);

			if (!pinmuxs[i].pin) {
				dev_err(dev, "failed to get pin %d\n", pins[i]);
				ret = -ENODEV;
				goto failed;
			}
		}

		map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		map[nmaps].data.mux.function = np->name;
		map[nmaps].data.mux.group = grpname;
		nmaps += 1;

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret) {
			dev_err(dev, "failed to parse pin config of group %s: %d\n",
				grpname, ret);
			goto failed;
		}

		ret = pinctrl_generic_add_group(pctldev, grpname,
						pins, npins, pinmuxs);
		if (ret < 0) {
			dev_err(dev, "failed to add group %s: %d\n", grpname, ret);
			goto failed;
		}

		/* don't create a map if there are no pinconf settings */
		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name,
					  grpnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto failed;
	}

	*maps = map;
	*num_maps = nmaps;

	return 0;

failed:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	return ret;
}

static const struct pinctrl_ops sg2042_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.pin_dbg_show		= sg2042_pctrl_dbg_show,
	.dt_node_to_map		= sg2042_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int sg2042_pmx_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int fsel, unsigned int gsel)
{
	struct sg2042_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct sg2042_pin_mux_config *configs;
	unsigned int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	configs = group->data;

	for (i = 0; i < group->grp.npins; i++) {
		const struct sg2042_pin *pin = configs[i].pin;
		u32 value = configs[i].config;
		u32 mux = sg2042_dt_get_pin_mux(value);

		guard(raw_spinlock_irqsave)(&pctrl->lock);

		if (!(pin->flags & PIN_FLAG_NO_PINMUX))
			sg2042_set_pin_reg(pctrl, pin, mux, PIN_IO_MUX);
	}

	return 0;
}

static const struct pinmux_ops sg2042_pmx_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= sg2042_pmx_set_mux,
	.strict			= true,
};

static u32 sg2042_pull_down_typical_resistor(struct sg2042_pinctrl *pctrl)
{
	return pctrl->data->pulldown_res;
}

static u32 sg2042_pull_up_typical_resistor(struct sg2042_pinctrl *pctrl)
{
	return pctrl->data->pullup_res;
}

static int sg2042_pinctrl_oc2reg(struct sg2042_pinctrl *pctrl,
				 const struct sg2042_pin *pin, u32 target)
{
	const u32 *map = pctrl->data->oc_reg_map;
	int len = pctrl->data->noc_reg;
	int i;

	for (i = 0; i < len; i++) {
		if (map[i] >= target)
			return i;
	}

	return -EINVAL;
}

static int sg2042_pinctrl_reg2oc(struct sg2042_pinctrl *pctrl,
				 const struct sg2042_pin *pin, u32 reg)
{
	const u32 *map = pctrl->data->oc_reg_map;
	int len = pctrl->data->noc_reg;

	if (reg >= len)
		return -EINVAL;

	return map[reg];
}

static int sg2042_pconf_get(struct pinctrl_dev *pctldev,
			    unsigned int pin_id, unsigned long *config)
{
	struct sg2042_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	const struct sg2042_pin *pin = sg2042_get_pin(pctrl, pin_id);
	u32 value;
	u32 arg;
	bool enabled;
	int ret;

	if (!pin)
		return -EINVAL;

	value = sg2042_get_pin_reg(pctrl, pin);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pin->flags & PIN_FLAG_ONLY_ONE_PULL)
			arg = FIELD_GET(PIN_IO_PULL_ONE_ENABLE, value);
		else
			arg = FIELD_GET(PIN_IO_PULL_UP_MASK, value);
		enabled = arg == 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (pin->flags & PIN_FLAG_ONLY_ONE_PULL) {
			arg = FIELD_GET(PIN_IO_PULL_ONE_MASK, value);
			enabled = arg == PIN_IO_PULL_DIR_DOWN;
		} else {
			enabled = FIELD_GET(PIN_IO_PULL_UP_DONW, value) != 0;
		}
		arg = sg2042_pull_down_typical_resistor(pctrl);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pin->flags & PIN_FLAG_ONLY_ONE_PULL) {
			arg = FIELD_GET(PIN_IO_PULL_ONE_MASK, value);
			enabled = arg == PIN_IO_PULL_DIR_UP;
		} else {
			enabled = FIELD_GET(PIN_IO_PULL_UP, value) != 0;
		}
		arg = sg2042_pull_up_typical_resistor(pctrl);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		enabled = FIELD_GET(PIN_IO_OUTPUT_ENABLE, value) != 0;
		arg = FIELD_GET(PIN_IO_DRIVE, value);
		ret = sg2042_pinctrl_reg2oc(pctrl, pin, arg);
		if (ret < 0)
			return ret;
		arg = ret;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		arg = FIELD_GET(PIN_IO_SCHMITT_ENABLE, value);
		enabled = arg != 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return enabled ? 0 : -EINVAL;
}

static int sg2042_pinconf_compute_config(struct sg2042_pinctrl *pctrl,
					 const struct sg2042_pin *pin,
					 unsigned long *configs,
					 unsigned int num_configs,
					 u16 *value, u16 *mask)
{
	int i;
	u16 v = 0, m = 0;
	int ret;

	if (!pin)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			if (pin->flags & PIN_FLAG_ONLY_ONE_PULL) {
				v &= ~PIN_IO_PULL_ONE_ENABLE;
				m |= PIN_IO_PULL_ONE_ENABLE;
			} else {
				v &= ~PIN_IO_PULL_UP_MASK;
				m |= PIN_IO_PULL_UP_MASK;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (pin->flags & PIN_FLAG_ONLY_ONE_PULL) {
				v &= ~PIN_IO_PULL_ONE_MASK;
				v |= PIN_IO_PULL_DIR_DOWN;
				m |= PIN_IO_PULL_ONE_MASK;
			} else {
				v |= PIN_IO_PULL_UP_DONW;
				m |= PIN_IO_PULL_UP_DONW;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (pin->flags & PIN_FLAG_ONLY_ONE_PULL) {
				v &= ~PIN_IO_PULL_ONE_MASK;
				v |= PIN_IO_PULL_DIR_UP;
				m |= PIN_IO_PULL_ONE_MASK;
			} else {
				v |= PIN_IO_PULL_UP;
				m |= PIN_IO_PULL_UP;
			}
			break;
		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			v &= ~(PIN_IO_DRIVE | PIN_IO_OUTPUT_ENABLE);
			if (arg != 0) {
				ret = sg2042_pinctrl_oc2reg(pctrl, pin, arg);
				if (ret < 0)
					return ret;
				if (!(pin->flags & PIN_FLAG_NO_OEX_EN))
					v |= PIN_IO_OUTPUT_ENABLE;
				v |= FIELD_PREP(PIN_IO_DRIVE, ret);
			}
			m |= PIN_IO_DRIVE | PIN_IO_OUTPUT_ENABLE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			v |= PIN_IO_SCHMITT_ENABLE;
			m |= PIN_IO_SCHMITT_ENABLE;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	*value = v;
	*mask = m;

	return 0;
}

static int sg2042_pin_set_config(struct sg2042_pinctrl *pctrl,
				 unsigned int pin_id,
				 u16 value, u16 mask)
{
	const struct sg2042_pin *pin = sg2042_get_pin(pctrl, pin_id);

	if (!pin)
		return -EINVAL;

	guard(raw_spinlock_irqsave)(&pctrl->lock);
	sg2042_set_pin_reg(pctrl, pin, value, mask);

	return 0;
}

static int sg2042_pconf_set(struct pinctrl_dev *pctldev,
			    unsigned int pin_id, unsigned long *configs,
			    unsigned int num_configs)
{
	struct sg2042_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct sg2042_pin *pin = sg2042_get_pin(pctrl, pin_id);
	u16 value, mask;

	if (!pin)
		return -ENODEV;

	if (sg2042_pinconf_compute_config(pctrl, pin,
					  configs, num_configs,
					  &value, &mask))
		return -ENOTSUPP;

	return sg2042_pin_set_config(pctrl, pin_id, value, mask);
}

static int sg2042_pconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int gsel,
				  unsigned long *configs,
				  unsigned int num_configs)
{
	struct sg2042_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct sg2042_pin_mux_config *pinmuxs;
	u16 value, mask;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	pinmuxs = group->data;

	if (sg2042_pinconf_compute_config(pctrl, pinmuxs[0].pin,
					  configs, num_configs,
					  &value, &mask))
		return -ENOTSUPP;

	for (i = 0; i < group->grp.npins; i++)
		sg2042_pin_set_config(pctrl, group->grp.pins[i], value, mask);

	return 0;
}

static const struct pinconf_ops sg2042_pconf_ops = {
	.pin_config_get			= sg2042_pconf_get,
	.pin_config_set			= sg2042_pconf_set,
	.pin_config_group_set		= sg2042_pconf_group_set,
	.is_generic			= true,
};

int sg2042_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sg2042_pinctrl *pctrl;
	const struct sg2042_pinctrl_data *pctrl_data;
	int ret;

	pctrl_data = device_get_match_data(dev);
	if (!pctrl_data)
		return -ENODEV;

	if (pctrl_data->npins == 0)
		return dev_err_probe(dev, -EINVAL, "invalid pin data\n");

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->regs))
		return PTR_ERR(pctrl->regs);

	pctrl->pdesc.name = dev_name(dev);
	pctrl->pdesc.pins = pctrl_data->pins;
	pctrl->pdesc.npins = pctrl_data->npins;
	pctrl->pdesc.pctlops = &sg2042_pctrl_ops;
	pctrl->pdesc.pmxops = &sg2042_pmx_ops;
	pctrl->pdesc.confops = &sg2042_pconf_ops;
	pctrl->pdesc.owner = THIS_MODULE;

	pctrl->data = pctrl_data;
	pctrl->dev = dev;
	raw_spin_lock_init(&pctrl->lock);
	mutex_init(&pctrl->mutex);

	platform_set_drvdata(pdev, pctrl);

	ret = devm_pinctrl_register_and_init(dev, &pctrl->pdesc,
					     pctrl, &pctrl->pctl_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "fail to register pinctrl driver\n");

	return pinctrl_enable(pctrl->pctl_dev);
}
EXPORT_SYMBOL_GPL(sg2042_pinctrl_probe);

MODULE_DESCRIPTION("Pinctrl OPs for the SG2042 SoC");
MODULE_LICENSE("GPL");
