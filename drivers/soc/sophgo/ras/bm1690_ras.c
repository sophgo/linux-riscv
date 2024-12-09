// SPDX-License-Identifier: GPL-2.0

#include "bm1690_ras.h"

static ssize_t kernel_exec_time_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct tp_status *tp = ras->status_va;
	uint64_t cur_exec_time = tp->kernel_exec_time;
	uint64_t last_exec_time = ras->last_exec_time;

	ras->last_exec_time = cur_exec_time;

	return sprintf(buf, "%lluns\n", cur_exec_time - last_exec_time);
}
static DEVICE_ATTR_RO(kernel_exec_time);

static ssize_t kernel_exec_raw_time_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct tp_status *tp = ras->status_va;
	uint64_t cur_exec_time = tp->kernel_exec_time;

	return sprintf(buf, "%lluns\n", cur_exec_time);
}
static DEVICE_ATTR_RO(kernel_exec_raw_time);

static ssize_t kernel_alive_time_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct tp_status *tp = ras->status_va;
	uint64_t cur_alive_time = tp->tp_alive_time;
	uint64_t last_alive_time = ras->last_alive_time;

	ras->last_alive_time = cur_alive_time;

	return sprintf(buf, "%lluns\n", cur_alive_time - last_alive_time);
}
static DEVICE_ATTR_RO(kernel_alive_time);

static ssize_t kernel_alive_raw_time_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct tp_status *tp = ras->status_va;
	uint64_t cur_alive_time = tp->tp_alive_time;

	return sprintf(buf, "%lluns\n", cur_alive_time);
}
static DEVICE_ATTR_RO(kernel_alive_raw_time);

static int get_dtb_info(struct platform_device *pdev, struct sophgo_card_ras_status *ras)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev_of_node(dev);
	struct resource *regs;
	int ret;
	uint32_t tpu_index;

	ret = of_property_read_u32(dev_node, "tp-index", &tpu_index);
	if (ret < 0) {
		pr_err("failed get ras-index, ret = %d, index:%u\n", ret, tpu_index);
		return -1;
	}

	ras->tpu_index = tpu_index;
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return pr_err("no registers defined\n");

	ras->status_pa = regs->start;
	pr_err("ras status:0x%llx\n", ras->status_pa);

	ras->status_va = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!ras->status_va) {
		pr_err("ioremap failed\n");
	}
	memset(ras->status_va, 0, 0x1000);

	return 0;
}

static int sgcard_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_card_ras_status *ras;
	int ret;

	ras = kmalloc(sizeof(struct sophgo_card_ras_status), GFP_KERNEL);
	if (ras == NULL) {
		pr_err("failed to alloc sophgo card ras status\n");
		return -1;
	}
	memset(ras, 0, sizeof(struct sophgo_card_ras_status));

	ret = get_dtb_info(pdev, ras);
	if (ret) {
		pr_err("failed get ras dtb info\n");
		return -1;
	}

	ret = device_create_file(dev, &dev_attr_kernel_exec_time);
	ret = device_create_file(dev, &dev_attr_kernel_exec_raw_time);
		ret = device_create_file(dev, &dev_attr_kernel_alive_time);
	ret = device_create_file(dev, &dev_attr_kernel_alive_raw_time);
	dev_set_drvdata(dev, ras);

	return 0;
}

static struct of_device_id sophgo_card_ras_of_match[] = {
	{ .compatible = "sophgo,bm1690-ras",},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_card_of_match);

static struct platform_driver sg_card_platform_driver = {
	.driver = {
		.name		= "bm1690-ras",
		.of_match_table	= sophgo_card_ras_of_match,
	},
	.probe			= sgcard_probe,
	//.remove			= sgcard_remove,
};

module_platform_driver(sg_card_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for sg runtime");
