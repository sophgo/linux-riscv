// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
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

static ssize_t user_heart_beat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct tp_status *tp = ras->status_va;
	uint64_t tp_cur_time = tp->tp_read_loop_time;

	return sprintf(buf, "%llums\n", tp_cur_time / (1000 * 1000));
}
static DEVICE_ATTR_RO(user_heart_beat);

static ssize_t tp_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct tp_status *tp = ras->status_va;
	uint64_t last_tp_alive_time = tp->tp_alive_time;
	uint64_t last_tp_cur_time = tp->tp_read_loop_time;
	uint64_t tp_alive_time;
	uint64_t tp_cur_time;

	for (int i = 0; i < 300; i++) {
		msleep(10);

		tp_alive_time = tp->tp_alive_time;
		tp_cur_time = tp->tp_read_loop_time;

		if (last_tp_cur_time != tp_cur_time || last_tp_alive_time != tp_alive_time) {
			return sprintf(buf, "tpu%u alive\n", ras->tpu_index);
		}
	}

	return sprintf(buf, "tpu%u dead\n", ras->tpu_index);
}
static DEVICE_ATTR_RO(tp_status);

static ssize_t ap_user_heart_beat_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct ap_status *ap = ras->status_va;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	ap->user_space_kick_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);

	return len;
}

static ssize_t ap_user_heart_beat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct ap_status *ap = ras->status_va;

	return sprintf(buf, "%llums\n", ap->user_space_kick_time / (1000 * 1000));
}
static DEVICE_ATTR_RW(ap_user_heart_beat);

static ssize_t ap_kernel_heart_beat_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_card_ras_status *ras = dev_get_drvdata(dev);
	struct ap_status *ap = ras->status_va;

	return sprintf(buf, "%llums\n", ap->kernel_space_kick_time / (1000 * 1000));
}
static DEVICE_ATTR_RO(ap_kernel_heart_beat);

static int get_dtb_info(struct platform_device *pdev, struct sophgo_card_ras_status *ras)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev_of_node(dev);
	struct resource *regs;
	int tp_ret;
	int ap_ret;
	uint32_t tpu_index;

	tp_ret = of_property_read_u32(dev_node, "tp-index", &tpu_index);
	ap_ret = of_property_read_u32(dev_node, "ap-index", &tpu_index);
	if (tp_ret < 0 && ap_ret) {
		pr_err("failed get ras-index\n");
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

static void ap_kernel_kick_wdt_work_func(struct work_struct *p_work)
{
	struct sophgo_card_ras_status *ras = container_of(p_work, struct sophgo_card_ras_status,
							ras_delayed_work.work);
	struct ap_status *ap = ras->status_va;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	ap->kernel_space_kick_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);

	schedule_delayed_work(&ras->ras_delayed_work, HZ);
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

	if (ras->tpu_index != 0xff) {
		ret = device_create_file(dev, &dev_attr_kernel_exec_time);
		ret = device_create_file(dev, &dev_attr_kernel_exec_raw_time);
		ret = device_create_file(dev, &dev_attr_kernel_alive_time);
		ret = device_create_file(dev, &dev_attr_kernel_alive_raw_time);
		ret = device_create_file(dev, &dev_attr_user_heart_beat);
		ret = device_create_file(dev, &dev_attr_tp_status);
	} else {
		ret = device_create_file(dev, &dev_attr_ap_user_heart_beat);
		ret = device_create_file(dev, &dev_attr_ap_kernel_heart_beat);
		INIT_DELAYED_WORK(&ras->ras_delayed_work, ap_kernel_kick_wdt_work_func);
		schedule_delayed_work(&ras->ras_delayed_work, HZ);
	}

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
