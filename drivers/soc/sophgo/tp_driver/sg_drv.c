// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include "sg_common.h"
#include "sg_drv.h"
#include "sg_io.h"
#include "sg_fw.h"
#ifdef PLAT_BM1686
#include "sg_clkrst.h"
#endif

static unsigned int dev_counter;
struct sg_dev *the_hdev;
struct class *sg_class;

static ssize_t sg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	DBG_MSG("%s called by %s\n", __func__, current->comm);
	return -EOPNOTSUPP;
}

static ssize_t sg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char cmd[4];
	int length = count < sizeof(cmd) ? count : sizeof(cmd);
	// struct sg_dev *hdev = file->private_data;

	DBG_MSG("%s called by %s\n", __func__, current->comm);

	if (count == 0 || copy_from_user(cmd, buf, length))
		return -EFAULT;

	return length;
}

static int sg_open(struct inode *inode, struct file *file)
{
	struct sg_dev *hdev = container_of(inode->i_cdev, struct sg_dev, cdev);

	DBG_MSG("%s called by %s\n", __func__, current->comm);

	file->private_data = hdev;
	return 0;
}

static int sg_close(struct inode *inode, struct file *file)
{
	DBG_MSG("%s called by %s\n", __func__, current->comm);
	return 0;
}

static int sg_mmap(struct file *file, struct vm_area_struct *vma)
{
	DBG_MSG("%s called by %s\n", __func__, current->comm);
	return -EOPNOTSUPP;
}

static long sg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;

	DBG_MSG("%s 0x%x called by %s\n", __func__, cmd, current->comm);

	switch (cmd) {

	default:
		pr_err("unknown ioctl command 0x%x\n", cmd);
		return -EINVAL;
	}
	if (ret)
		return ret;

	return 0;
}

static const struct file_operations sg_fops = {
	.read = sg_read,
	.write = sg_write,
	.open = sg_open,
	.release = sg_close,
	.unlocked_ioctl = sg_ioctl,
	.mmap = sg_mmap,
	.owner = THIS_MODULE,
};

static int sgdrv_create_cdev(struct sg_dev *hdev)
{
	int ret;

	if (sg_class == NULL) {
		sg_class = class_create(DEV_NAME);
		if (IS_ERR(sg_class)) {
			pr_err("create class error\n");
			sg_class = NULL;
			return -ENOENT;
		}
	}

	snprintf(hdev->dev_name, sizeof(hdev->dev_name), "%s-%d", DEV_NAME, hdev->id);
	ret = alloc_chrdev_region(&hdev->devno, 0, MAX_CARD_NUMBER, hdev->dev_name);
	if (ret < 0) {
		pr_err("register char device error\n");
		return ret;
	}

	hdev->dev = device_create(sg_class, hdev->parent, hdev->devno, NULL, hdev->dev_name);
	cdev_init(&hdev->cdev, &sg_fops);
	hdev->cdev.owner = THIS_MODULE;
	cdev_add(&hdev->cdev, hdev->devno, 1);

	return 0;
}

static void sgdrv_destroy_cdev(struct sg_dev *hdev)
{
	cdev_del(&hdev->cdev);
	device_destroy(sg_class, hdev->devno);
	unregister_chrdev_region(hdev->devno, MAX_CARD_NUMBER);
	class_destroy(sg_class);
}

static int sgdrv_create_dev(struct sg_dev **phdev)
{
	int i, ret;
	struct sg_dev *hdev;

	hdev = kzalloc(sizeof(struct sg_dev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;
	hdev->parent = NULL;
	hdev->id = dev_counter++;

	for (i = 0; i < CORE_NUM; i++) {
		hdev->core_info[i] = kzalloc(sizeof(struct sg_core), GFP_KERNEL);
		if (!hdev->core_info[i])
			return -ENOMEM;
	}

	ret = sgdrv_create_cdev(hdev);
	if (ret == 0)
		*phdev = hdev;
	return ret;
}

static void sgdrv_destroy_dev(struct sg_dev *hdev)
{
	int i;

	if (hdev) {
		sgdrv_destroy_cdev(hdev);
		for (i = 0; i < CORE_NUM; i++)
			kfree(hdev->core_info[i]);
		kfree(hdev);
	}
}

#ifdef PLAT_BM1686
static int sgdrv_clkrst_init(struct platform_device *pdev, struct sg_dev *sgdev)
{
	u32 val;

	bm1686_modules_clk_init(pdev);
	bm1686_modules_clk_enable(sgdev);
	bm1686_modules_reset_init(pdev);
	sgdrv_clk_enable_tpu_subsystem_axi_sram_auto_clk_gate(sgdev);
	sgdrv_clk_enable_tpu_subsystem_fabric_auto_clk_gate(sgdev);
	sgdrv_clk_enable_pcie_subsystem_fabric_auto_clk_gate(sgdev);
	bm1686_modules_reset(sgdev);

	val = bdc_reg_read(sgdev, 0x100);
	bdc_reg_write(sgdev, 0x100, val | 0x1);

	return 0;
}

static void sgdrv_clkrst_deinit(struct platform_device *pdev, struct sg_dev *sgdev)
{
	bm1686_modules_clk_disable(sgdev);
	bm1686_modules_clk_deinit(pdev);
}
#endif

static const struct of_device_id sgdrv_match_table[]  = {
	{ .compatible = "sophgo,tpu-1690" },
	{ .compatible = "sophgo,tpu-1684" },
	{},
};

static int sgdrv_probe(struct platform_device *pdev)
{
	int ret;

	dev_info(&pdev->dev, "sgdrv: probe start\n");

	ret = sgdrv_create_dev(&the_hdev);
	if (ret) {
		pr_err("sgdrv: create device failed %d\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, the_hdev);

	dev_info(&pdev->dev, "sgdrv: io_init start\n");
	ret = sgdrv_io_init(pdev);
	if (ret) {
		pr_err("sgdrv: io init failed %d\n", ret);
		goto err_io;
	}

#ifdef PLAT_BM1686
	dev_info(&pdev->dev, "sgdrv: clkrst_init start\n");
	ret = sgdrv_clkrst_init(pdev, the_hdev);
	if (ret) {
		pr_err("sgdrv: clkrst init failed %d\n", ret);
		goto err_clkrst;
	}
#endif

	dev_info(&pdev->dev, "sgdrv: load_firmware start\n");
	sgdrv_set_fw_mode(the_hdev, pdev);
	if (sgdrv_fw_load(the_hdev)) {
		pr_err("sgdrv: firmware load failed!\n");
		goto err_fw;
	}

	dev_info(&pdev->dev, "sgdrv: probe done\n");
	return 0;

// err_irq:
	// sgdrv_fw_unload(the_hdev);
err_fw:
#ifdef PLAT_BM1686
	sgdrv_clkrst_deinit(pdev, the_hdev);
err_clkrst:
#endif
err_io:
	platform_set_drvdata(pdev, NULL);
	sgdrv_destroy_dev(the_hdev);
	return ret;
}

static void sgdrv_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "sgdrv: remove start\n");

	sgdrv_fw_unload(the_hdev);
#ifdef PLAT_BM1686
	sgdrv_clkrst_deinit(pdev, the_hdev);
#endif
	platform_set_drvdata(pdev, NULL);
	sgdrv_destroy_dev(the_hdev);

	dev_info(&pdev->dev, "sgdrv: remove done\n");

	return;
}

static struct platform_driver sg_tpu_driver = {
	.probe		= sgdrv_probe,
	.remove		= sgdrv_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DEV_NAME,
		.of_match_table = sgdrv_match_table,
	},
};

module_platform_driver(sg_tpu_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhuolin.li");
MODULE_DESCRIPTION("sg driver for tpu");
