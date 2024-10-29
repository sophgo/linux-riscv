#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pinctrl-sophgo.h"

static int sg_get_groups(struct pinctrl_dev *pctldev, unsigned int selector,
		const char * const **groups,
		unsigned int * const num_groups);

static struct sg_soc_pinctrl_data *get_pinmux_data(struct pinctrl_dev *pctldev)
{
	struct sg_pinctrl *sgpctrl = pinctrl_dev_get_drvdata(pctldev);

	return sgpctrl->data;
}

static int sg_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	return data->functions_count;
}

static const char *sg_get_fname(struct pinctrl_dev *pctldev,
		unsigned int selector)
{
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	return data->functions[selector].name;
}

static int sg_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
		unsigned int group)
{
	int p;
	unsigned int pidx;
	u32 offset, regval, mux_offset;
	struct sg_pinctrl *ctrl = pinctrl_dev_get_drvdata(pctldev);
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	data->groups[group].cur_func_idx = data->functions[selector].mode;
	for (p = 0; p < data->groups[group].num_pins; p++) {
		pidx = data->groups[group].pins[p];
		offset = (pidx >> 1) << 2;
		regmap_read(ctrl->syscon_pinctl,
			ctrl->top_pinctl_offset + offset, &regval);
		mux_offset = ((!((pidx + 1) & 1) << 4) + 4);

		regval = regval & ~(3 << mux_offset);
		regval |= data->functions[selector].mode << mux_offset;
		regmap_write(ctrl->syscon_pinctl,
			ctrl->top_pinctl_offset + offset, regval);
		regmap_read(ctrl->syscon_pinctl,
			ctrl->top_pinctl_offset + offset, &regval);
		dev_dbg(ctrl->dev, "%s : check new reg=0x%x val=0x%x\n",
			data->groups[group].name,
			ctrl->top_pinctl_offset + offset, regval);
	}

	return 0;
}

static const struct pinmux_ops sg_pinmux_ops = {
	.get_functions_count = sg_get_functions_count,
	.get_function_name = sg_get_fname,
	.get_function_groups = sg_get_groups,
	.set_mux = sg_set_mux,
	.strict = true,
};

static int sg_pinconf_cfg_get(struct pinctrl_dev *pctldev, unsigned int pin,
		unsigned long *config)
{
	return 0;
}

static int sg_pinconf_cfg_set(struct pinctrl_dev *pctldev, unsigned int pin,
		unsigned long *configs, unsigned int num_configs)
{
	return 0;
}

static int sg_pinconf_group_set(struct pinctrl_dev *pctldev,
		unsigned int selector, unsigned long *configs, unsigned int num_configs)
{
	return 0;
}

static const struct pinconf_ops sg_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = sg_pinconf_cfg_get,
	.pin_config_set = sg_pinconf_cfg_set,
	.pin_config_group_set = sg_pinconf_group_set,
};

static int sg_get_groups(struct pinctrl_dev *pctldev, unsigned int selector,
		const char * const **groups,
		unsigned int * const num_groups)
{
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	*groups = data->functions[selector].groups;
	*num_groups = data->functions[selector].num_groups;

	return 0;
}

static int sg_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	return data->groups_count;
}

static const char *sg_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned int selector)
{
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	return data->groups[selector].name;
}

static int sg_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
		const unsigned int **pins,
		unsigned int *num_pins)
{
	struct sg_soc_pinctrl_data *data = get_pinmux_data(pctldev);

	*pins = data->groups[selector].pins;
	*num_pins = data->groups[selector].num_pins;

	return 0;
}

static void sg_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
	unsigned int offset)
{
	return ;
}

static const struct pinctrl_ops sg_pctrl_ops = {
	.get_groups_count = sg_get_groups_count,
	.get_group_name = sg_get_group_name,
	.get_group_pins = sg_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show = sg_pin_dbg_show,
#endif
};

static struct pinctrl_desc sg_desc = {
	.name = "sg_pinctrl",
	.pctlops = &sg_pctrl_ops,
	.pmxops = &sg_pinmux_ops,
	.confops = &sg_pinconf_ops,
	.owner = THIS_MODULE,
};

ssize_t pinmux_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sg_pinctrl *sgpctrl;
	int p, ret, group, selector = -1;
	struct sg_soc_pinctrl_data *data;

	sgpctrl = dev_get_drvdata(dev);
	data = (struct sg_soc_pinctrl_data *)sgpctrl->data;

	for (p = 0; p < data->functions_count; p++) {
		if (!strncmp(attr->attr.name, data->functions[p].name,
			 strlen(attr->attr.name))) {
			selector = p;
			break;
		}
	}
	if (selector < 0)
		return -ENXIO;

	group = selector/2;
	ret = snprintf(buf, 128, "%d\n", data->groups[group].cur_func_idx);
	if (ret <= 0 || ret > 128) {
		dev_err(dev, "snprintf failed %d\n", ret);
		return -EFAULT;
	}
	return ret;
}

ssize_t pinmux_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct sg_pinctrl *sgpctrl;
	int p, ret, group, selector = -1;
	unsigned long user_data;
	struct sg_soc_pinctrl_data *data;

	ret = kstrtoul(buf, 0, &user_data);
	if (ret)
		return -EINVAL;

	if (user_data != 0 && user_data != 1)
		return -EINVAL;

	sgpctrl = dev_get_drvdata(dev);
	data = (struct sg_soc_pinctrl_data *)sgpctrl->data;

	for (p = 0; p < data->functions_count; p++) {
		if (!strncmp(attr->attr.name, data->functions[p].name,
				strlen(attr->attr.name)) &&
				(user_data == data->functions[p].mode)) {
			selector = p;
			break;
		}
	}
	if (selector < 0)
		return -ENXIO;

	group = selector/2;
	sg_set_mux(sgpctrl->pctl, selector, group);

	dev_info(dev, "pinmux store set %s to func %d\n",
			attr->attr.name, data->functions[selector].mode);
	return size;
}

int sophgo_pinctrl_probe(struct platform_device *pdev)
{
	struct fwnode_handle *fwnode;
	struct sg_pinctrl *sgpctrl;
	struct pinctrl_desc *desc;
	struct sg_soc_pinctrl_data *data;
	struct device *dev = &pdev->dev;
	struct device *pin_dev = NULL;
	static struct regmap *syscon;
	int ret;

	data = (struct sg_soc_pinctrl_data *)device_get_match_data(&pdev->dev);
	if (!data)
		return -EINVAL;
	sgpctrl = devm_kzalloc(&pdev->dev, sizeof(*sgpctrl), GFP_KERNEL);
	if (!sgpctrl)
		return -ENOMEM;

	sgpctrl->dev = &pdev->dev;

	fwnode = fwnode_find_reference(dev_fwnode(&pdev->dev), "subctrl-syscon", 0);
	if (!fwnode) {
		dev_err(dev, "%s can't get subctrl-syscon node\n", __func__);
		return -EINVAL;
	}

	syscon = syscon_fwnode_to_regmap(fwnode);
	if (IS_ERR(syscon)) {
		dev_err(dev, "cannot get regmap\n");
		return PTR_ERR(syscon);
	}

	sgpctrl->syscon_pinctl = syscon;

	ret = device_property_read_u32(&pdev->dev,
		"top_pinctl_offset", &sgpctrl->top_pinctl_offset);
	if (ret < 0) {
		dev_err(dev, "cannot get top_pinctl_offset\n");
		return ret;
	}

	desc = &sg_desc;
	desc->pins = data->pins;
	desc->npins = data->npins;

	sgpctrl->data = (void *)data;
	sgpctrl->pctl = devm_pinctrl_register(&pdev->dev, desc, sgpctrl);
	if (IS_ERR(sgpctrl->pctl)) {
		dev_err(&pdev->dev, "could not register sophgo pin ctrl driver\n");
		return PTR_ERR(sgpctrl->pctl);
	}

	platform_set_drvdata(pdev, sgpctrl);

	ret = class_register(data->p_class);
	if (ret < 0) {
		dev_err(dev, "cannot register pinmux class\n");
		return ret;
	}

	pin_dev = device_create(data->p_class, &pdev->dev, MKDEV(0, 0), sgpctrl, "sg_pinmux");
	if (IS_ERR(pin_dev))
		return PTR_ERR(pin_dev);

	return 0;
}
