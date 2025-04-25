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
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/of_gpio.h>
#include "ap_pcie_ep.h"
#include "../ap_sgcard/ap_sgcard.h"

static LIST_HEAD(ep_list_head);
static DEFINE_SPINLOCK(ep_list_lock);

static struct vector_info *get_vector(struct sophgo_pcie_ep *sg_ep, int vector_nm)
{
	struct vector_info *vector;

	pr_err("vector nm: %d, vector flag:%d\n", vector_nm, sg_ep->vector_info[vector_nm].vector_flag);
	if (sg_ep->vector_info[vector_nm].vector_flag != VECTOR_INVALID && vector_nm < sg_ep->vector_allocated) {
		vector = &sg_ep->vector_info[vector_nm];
		pr_err("get vector:%px, va:%px, da:0x%llx\n", vector, vector->msi_va, vector->msi_data);
		return &(sg_ep->vector_info[vector_nm]);
	}
	else
		return NULL;
}

static int sophgo_pcie_link_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);
	struct device_node *child_node;
	struct platform_device *child_pdev;
	struct resource *res;
	int ret = 0;
	int resource_num = 0;

	pr_err("[pcie ep] pcie link probe\n");

	if (sg_ep->set_vector)
		sg_ep->set_vector(sg_ep);
	if (sg_ep->set_iatu)
		sg_ep->set_iatu(sg_ep);
	if (sg_ep->set_c2c_atu)
		sg_ep->set_c2c_atu(sg_ep);
	if (sg_ep->set_recoder)
		sg_ep->set_recoder(sg_ep);
	if (sg_ep->set_portcode)
		sg_ep->set_portcode(sg_ep);
	if (sg_ep->set_wr_order)
		sg_ep->set_wr_order(sg_ep);

	res = kzalloc(sizeof(struct resource) * 32, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	child_node = of_get_compatible_child(dev->of_node, "sophgo,sophgo-card");
	if (!child_node) {
		pr_err("failed to find sophgo-card node\n");
		return -ENODEV;
	}

	child_pdev = platform_device_alloc("sophgo-card", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	child_pdev->dev.of_node = of_node_get(child_node);

	ret = of_irq_to_resource_table(child_node, res, 32);
	if (!ret)
		pr_err("[pcie ep] get 0 irq resource\n");
	resource_num = ret;
	pr_info("[pcie ep] get sophgo card irq resource %d\n", resource_num);

	for (int i = 0; i < 32; i++) {
		ret = of_address_to_resource(child_node, i, &res[resource_num]);
		if (ret) {
			pr_debug("ret = %d\n", ret);
			break;
		}

		resource_num++;
	}

	pr_info("[pcie ep] get all resource %d\n", resource_num);

	ret = platform_device_add_resources(child_pdev, res, resource_num);
	if (ret) {
		pr_err("[pcie ep] platform_device_add_resources failed\n");
		return ret;
	}

	ret = platform_device_add(child_pdev);
	if (ret) {
		pr_err("[pcie ep] platform_device_add failed\n");
		return ret;
	}

#if 0
	uint64_t irq_nr;
	uint64_t mtli_phy_addr;
	void __iomem *mtli_base;

	sg_ep->vfun[PCIE_VFUN_SGCARD].vector = get_vector(sg_ep, PCIE_VFUN_SGCARD);
	if (!sg_ep->vfun[PCIE_VFUN_SGCARD].vector) {
		pr_err("%d failed get vector\n", PCIE_VFUN_SGCARD);
		return -1;
	}
	sg_ep->vfun[PCIE_VFUN_VETH].vector = get_vector(sg_ep, PCIE_VFUN_VETH);
	if (!sg_ep->vfun[PCIE_VFUN_VETH].vector) {
		pr_err("%d failed get vector\n", PCIE_VFUN_VETH);
		return -1;
	}


	irq_nr = alloc_irq_from_mtli(&mtli_phy_addr, &mtli_base);
	if (irq_nr < 0) {
		pr_err("%d failed get mtli irq\n", PCIE_VFUN_SGCARD);
		return -1;
	}
	sg_ep->vfun[PCIE_VFUN_SGCARD].rcv_msi_info.irq_nr = irq_nr;
	sg_ep->vfun[PCIE_VFUN_SGCARD].rcv_msi_info.phy_addr = mtli_phy_addr;
	sg_ep->vfun[PCIE_VFUN_SGCARD].rcv_msi_info.msi_va = mtli_base;

	irq_nr = alloc_irq_from_mtli(&mtli_phy_addr, &mtli_base);
	if (irq_nr < 0) {
		pr_err("%d failed get mtli irq\n", PCIE_VFUN_VETH);
		return -1;
	}
	sg_ep->vfun[PCIE_VFUN_VETH].rcv_msi_info.irq_nr = irq_nr;
	sg_ep->vfun[PCIE_VFUN_VETH].rcv_msi_info.phy_addr = mtli_phy_addr;
	sg_ep->vfun[PCIE_VFUN_VETH].rcv_msi_info.msi_va = mtli_base;

	probe_sgcard(&sg_ep->vfun[PCIE_VFUN_SGCARD]);
#endif
	return 0;
}

static irqreturn_t perst_interrupt(int irq, void *dev_id)
{
	struct sophgo_pcie_ep *sg_ep = (struct sophgo_pcie_ep *)dev_id;

	pr_err("[%s] get perst interrupt, clr irq va:0x%llx\n", sg_ep->name, (uint64_t)sg_ep->clr_irq);

	if (sg_ep->clr_irq)
		writel(sg_ep->clr_irq_data, sg_ep->clr_irq);

	schedule_delayed_work(&sg_ep->link_work, 0);

	return IRQ_HANDLED;
}

static ssize_t c2c_ep_enable_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);
	char buf[32] = {0};
	int enable;
	int ret;

	pr_err("c2c enable store\n");
	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &enable);

	if (enable == 0 || enable == 1) {
		pr_err("enable = %d\n", enable);
		sg_ep->c2c_enable = enable;
		schedule_delayed_work(&sg_ep->link_work, 0);

		return len;
	}

	pr_err("please echo 1 for init c2c ep\n");

	return -EINVAL;
}

static ssize_t c2c_ep_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);

	if (sg_ep->c2c_enable) {
		sprintf(buf, "c2c enable\n");
		return strlen(buf);
	}

	sprintf(buf, "c2c disable\n");
	return strlen(buf);
}

static DEVICE_ATTR_RW(c2c_ep_enable);
static int sophgo_c2c_ep_enable_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = device_create_file(dev, &dev_attr_c2c_ep_enable);
	pr_info("[c2c_enable]: create c2c_ep_enable success\n");

	return ret;
}

static void c2c_init_ep(struct work_struct *p_work)
{
	struct sophgo_pcie_ep *sg_ep = container_of(p_work, struct sophgo_pcie_ep, link_work.work);

	pr_err("sophgo pcie c2c ep dealy work queue\n");

	bm1690_pcie_init_link(sg_ep);
	sophgo_pcie_ep_config_cdma_route(sg_ep);
}

static int sophgo_c2c_link_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);
	int ret;

	INIT_DELAYED_WORK(&sg_ep->link_work, c2c_init_ep);
	sophgo_c2c_ep_enable_probe(pdev);

	ret = request_irq(sg_ep->perst_irqnr, perst_interrupt, IRQF_TRIGGER_RISING,
			  sg_ep->name, sg_ep);
	if (ret < 0) {
		pr_err("%s request int failed\n", sg_ep->name);
		return -1;
	}

	return 0;
}

static int sophgo_pcie_ep_get_dtbif(struct platform_device *pdev, uint64_t link_role)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);
	struct device_node *dev_node = dev_of_node(dev);
	const char *chip_type;
	int ret;

	ret = of_property_read_string(dev_node, "chip_type", &chip_type);
	if (ret < 0) {
		pr_err("failed to get chip type\n");
		return -EINVAL;
	}

	sg_ep->ep_info.link_role = link_role;

	if (strcmp(chip_type, "bm1684x") == 0) {
		sg_ep->chip_type = CHIP_BM1684X;
		bm1684x_ep_int(pdev);
		dev_info(dev, "found bm1684x pcie ep\n");
	} else if (strcmp(chip_type, "bm1690") == 0) {
		sg_ep->chip_type = CHIP_BM1690;
		bm1690_ep_init(pdev);
		dev_info(dev, "found bm1690 pcie ep\n");
	} else if (strcmp(chip_type, "bm1690e") == 0) {
		sg_ep->chip_type = CHIP_BM1690E;
		bm1690_ep_init(pdev);
	} else {
		pr_err("unknown chip type %s\n", chip_type);
		return -EINVAL;
	}

	return 0;
}

static int sophgo_ep_probe(struct platform_device *pdev)
{
	int ret = 0;
	uint64_t link_role;
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_ep *sg_ep;

	link_role = (uint64_t)device_get_match_data(dev);

	sg_ep = kzalloc(sizeof(struct sophgo_pcie_ep), GFP_KERNEL);
	if (!sg_ep)
		goto fail;
	dev_set_drvdata(dev, sg_ep);
	sg_ep->dev = dev;

	spin_lock(&ep_list_lock);
	list_add(&sg_ep->pcie_ep_list, &ep_list_head);
	spin_unlock(&ep_list_lock);

	sophgo_pcie_ep_get_dtbif(pdev, link_role);

	switch (link_role) {
	case PCIE_DATA_LINK_PCIE:
		sophgo_pcie_link_probe(pdev);
		break;
	case PCIE_DATA_LINK_C2C:
		sophgo_c2c_link_probe(pdev);
		break;
	}

	return 0;

fail:
	pr_err("malloc sg_ep failed\n");

	return ret;
}

static void sophgo_ep_remove(struct platform_device *pdev)
{
	return;
}

struct vector_info *sophgo_ep_alloc_vector(int pcie_id, int vector_id)
{
	struct sophgo_pcie_ep *sg_ep;
	struct vector_info *vector;

	spin_lock(&ep_list_lock);
	list_for_each_entry(sg_ep, &ep_list_head, pcie_ep_list) {
		if (sg_ep->ep_info.link_role == PCIE_DATA_LINK_PCIE)
			break;
	}
	spin_unlock(&ep_list_lock);

	if (vector_id != -1) {
		vector = get_vector(sg_ep, vector_id);
		if (vector != NULL && vector->allocated_num == 0) {
			vector->allocated_num++;
			pr_err("vp:%px, alloc vector %d, vector msi va:%px, vector msi data:0x%llx\n",
				vector, vector_id, vector->msi_va, vector->msi_data);
			return vector;
		}
	} else {
		for (int i = 0; i < VECTOR_MAX; i++) {
			vector = get_vector(sg_ep, i);
			if (vector != NULL && vector->allocated_num == 0 && vector->vector_flag == VECTOR_EXCLUSIVE) {
				vector->allocated_num++;
				return vector;
			}
		}
		for (int i = 0; i < VECTOR_MAX; i++) {
			vector = get_vector(sg_ep, i);
			if (vector != NULL && vector->vector_flag == VECTOR_SHARED) {
				vector->allocated_num++;
				return vector;
			}
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(sophgo_ep_alloc_vector);

static const struct of_device_id sophgo_pcie_ep_of_match[] = {
	{ .compatible = "sophgo,pcie-link-ep", (const void *)PCIE_DATA_LINK_PCIE},
	{ .compatible = "sophgo,c2c-link-ep", (const void *)PCIE_DATA_LINK_C2C},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_pcie_ep_of_match);

static struct platform_driver sg_card_platform_driver = {
	.driver = {
		.name		= "sophgo-pcie-ep",
		.of_match_table	= sophgo_pcie_ep_of_match,
	},
	.probe			= sophgo_ep_probe,
	.remove			= sophgo_ep_remove,
};

module_platform_driver(sg_card_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for pcie ep");
