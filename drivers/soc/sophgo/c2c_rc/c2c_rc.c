// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/sbitmap.h>

int sophgo_dw_pcie_probe(struct platform_device *pdev);

static int c2c_enable;

static const struct of_device_id sophgo_dw_c2c_pcie_of_match[] = {
	{ .compatible = "sophgo,bm1690-c2c-pcie-host", },
	{},
};

static struct platform_driver sophgo_dw_c2c_pcie_driver = {
	.driver = {
		.name	= "sophgo-dw-pcie-c2c",
		.of_match_table = sophgo_dw_c2c_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = sophgo_dw_pcie_probe,
};

static struct platform_driver *const c2c_drivers[] = {
	&sophgo_dw_c2c_pcie_driver,
};

static ssize_t c2c_enable_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	char buf[32] = {0};
	int enable;
	int ret;

	pr_err("c2c enable store\n");
	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &enable);

	if (enable == 0 || enable == 1) {
		pr_err("enable = %d\n", enable);
		c2c_enable = enable;
		platform_register_drivers(c2c_drivers, 1);

		return len;
	}

	pr_err("please echo 0 for disable and 1 for enable\n");

	return -EINVAL;
}

static ssize_t c2c_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	if (c2c_enable) {
		sprintf(buf, "c2c enable\n");
		return strlen(buf);
	}

	sprintf(buf, "c2c disable\n");
	return strlen(buf);
}

static DEVICE_ATTR_RW(c2c_enable);
static int sophgo_c2c_enable_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = device_create_file(dev, &dev_attr_c2c_enable);
	pr_info("[c2c_enable]: create c2c_enable success\n");

	return ret;
}

static int sophgo_c2c_enable_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_remove_file(dev, &dev_attr_c2c_enable);
	pr_info("[c2c_enable]: remove c2c_enable success\n");

	return 0;
}

static const struct of_device_id c2c_enable_of_match[] = {
	{ .compatible = "sophgo,c2c_enable",},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, c2c_enable_of_match);

static struct platform_driver sophgo_c2c_enable_driver = {
	.driver = {
		.name		= "sophgo-c2c-enable",
		.of_match_table	= c2c_enable_of_match,
	},
	.probe			= sophgo_c2c_enable_probe,
	.remove			= sophgo_c2c_enable_remove,
};

module_platform_driver(sophgo_c2c_enable_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for c2c rc driver");
