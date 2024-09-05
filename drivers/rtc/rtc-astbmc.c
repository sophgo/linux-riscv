// SPDX-License-Identifier: GPL-2.0-only
/* rtc-astbmc.c
 *
 * Driver for Dallas Semiconductor astbmcrtc Low Current, PCI Compatible
 * Real Time Clock
 *
 * Author : Raghavendra Chandra Ganiga <ravi23ganiga@gmail.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/bcd.h>
#include <linux/regmap.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/jiffies.h>

/* Registers in astbmcrtc rtc */

#define ASPEED_PCI_BMC_HOST2BMC_Q1		0x30000
#define ASPEED_PCI_BMC_HOST2BMC_Q2		0x30010
#define ASPEED_PCI_BMC_BMC2HOST_Q1		0x30020
#define ASPEED_PCI_BMC_BMC2HOST_Q2		0x30030
#define ASPEED_PCI_BMC_BMC2HOST_STS		0x30040
#define	 BMC2HOST_INT_STS_DOORBELL		BIT(31)
#define	 BMC2HOST_ENABLE_INTB			BIT(30)
/* */
#define	 BMC2HOST_Q1_FULL				BIT(27)
#define	 BMC2HOST_Q1_EMPTY				BIT(26)
#define	 BMC2HOST_Q2_FULL				BIT(25)
#define	 BMC2HOST_Q2_EMPTY				BIT(24)
#define	 BMC2HOST_Q1_FULL_UNMASK		BIT(23)
#define	 BMC2HOST_Q1_EMPTY_UNMASK		BIT(22)
#define	 BMC2HOST_Q2_FULL_UNMASK		BIT(21)
#define	 BMC2HOST_Q2_EMPTY_UNMASK		BIT(20)

#define ASPEED_PCI_BMC_HOST2BMC_STS		0x30044
#define	 HOST2BMC_INT_STS_DOORBELL		BIT(31)
#define	 HOST2BMC_ENABLE_INTB			BIT(30)
/* */
#define	 HOST2BMC_Q1_FULL				BIT(27)
#define	 HOST2BMC_Q1_EMPTY				BIT(26)
#define	 HOST2BMC_Q2_FULL				BIT(25)
#define	 HOST2BMC_Q2_EMPTY				BIT(24)
#define	 HOST2BMC_Q1_FULL_UNMASK		BIT(23)
#define	 HOST2BMC_Q1_EMPTY_UNMASK		BIT(22)
#define	 HOST2BMC_Q2_FULL_UNMASK		BIT(21)
#define	 HOST2BMC_Q2_EMPTY_UNMASK		BIT(20)

struct aspeed_pci_bmc_dev {
	struct device *dev;
	struct miscdevice miscdev;

	unsigned long mem_bar_base;
	unsigned long mem_bar_size;
	void __iomem *mem_bar_reg;

	unsigned long message_bar_base;
	unsigned long message_bar_size;
	void __iomem *msg_bar_reg;

	struct bin_attribute	bin0;
	struct bin_attribute	bin1;

	struct kernfs_node	*kn0;
	struct kernfs_node	*kn1;

	/* Queue waiters for idle engine */
	wait_queue_head_t tx_wait0;
	wait_queue_head_t tx_wait1;
	wait_queue_head_t rx_wait0;
	wait_queue_head_t rx_wait1;

	void __iomem *sio_mbox_reg;
	int sio_mbox_irq;

	u8 IntLine;
	int legency_irq;
};

#define HOST_BMC_QUEUE_SIZE			(16 * 4)
#define PCIE_DEVICE_SIO_ADDR		(0x2E * 4)
#define BMC_MULTI_MSI	32

#define DRIVER_NAME "ASPEED BMC DEVICE"

static struct rtc_device *rtc;
static int time64_flag = 1;
module_param(time64_flag, int, 0644);

static struct aspeed_pci_bmc_dev *file_aspeed_bmc_device(struct file *file)
{
	return container_of(file->private_data, struct aspeed_pci_bmc_dev,
			miscdev);
}

static int aspeed_pci_bmc_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct aspeed_pci_bmc_dev *pci_bmc_dev = file_aspeed_bmc_device(file);
	unsigned long vsize = vma->vm_end - vma->vm_start;
	pgprot_t prot = vma->vm_page_prot;

	if (vma->vm_pgoff + vsize > pci_bmc_dev->mem_bar_base + 0x100000)
		return -EINVAL;

	prot = pgprot_noncached(prot);

	if (remap_pfn_range(vma, vma->vm_start,
		(pci_bmc_dev->mem_bar_base >> PAGE_SHIFT) + vma->vm_pgoff,
		vsize, prot))
		return -EAGAIN;

	return 0;
}

static const struct file_operations aspeed_pci_bmc_dev_fops = {
	.owner		= THIS_MODULE,
	.mmap		= aspeed_pci_bmc_dev_mmap,
};

static int clear_r_queue1(struct device *dev, int timeout)
{
	u32 value;
	unsigned long tick_end = jiffies + timeout * HZ;
	struct aspeed_pci_bmc_dev *pci_bmc_device = dev_get_drvdata(dev);
	while (!(readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_BMC2HOST_STS) & BMC2HOST_Q1_EMPTY)) {
		value = readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_BMC2HOST_Q1);
		writel(HOST2BMC_INT_STS_DOORBELL | HOST2BMC_ENABLE_INTB, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS);

		if (time_after(jiffies, tick_end)) {
			return 0;
		}
	}
	return 1;
}



static ssize_t read_8bytes_from_queue1(struct device *dev, time64_t * time_stamp, int timeout)
{
	int i = 0;
	u32 * time_buf = (u32*)time_stamp;
	struct aspeed_pci_bmc_dev *pci_bmc_device = dev_get_drvdata(dev);
	unsigned long tick_end = jiffies + timeout * HZ;

	while (i < 2) {
		if (!(readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_BMC2HOST_STS) & BMC2HOST_Q1_EMPTY)) {
			time_buf[i++] = readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_BMC2HOST_Q1);
			writel(HOST2BMC_INT_STS_DOORBELL | HOST2BMC_ENABLE_INTB, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS);
		}

		if (time_after(jiffies, tick_end)) {
			return 0;
		}
	}
	return sizeof(time64_t);
}


static ssize_t write_queue1_4bytes_poll(struct device *dev, u32 data, int timeout)
{
	struct aspeed_pci_bmc_dev *pci_bmc_device = dev_get_drvdata(dev);

	unsigned long tick_end = jiffies + timeout * HZ;
	while(1) {
		if(!(readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS) & HOST2BMC_Q1_FULL))
			break;

		if (time_after(jiffies, tick_end)) {
			return 0;
		}
	}
	writel(data, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_Q1);
	writel(HOST2BMC_INT_STS_DOORBELL | HOST2BMC_ENABLE_INTB, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS);

	return sizeof(u32);
}



static ssize_t write_queue2_4bytes_poll(struct device *dev, u32 data, int timeout)
{
	struct aspeed_pci_bmc_dev *pci_bmc_device = dev_get_drvdata(dev);
	unsigned long tick_end = jiffies + timeout * HZ;
	while(1) {
		if(!(readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS) & HOST2BMC_Q2_FULL))
			break;

		if (time_after(jiffies, tick_end)) {
			return 0;
		}
	}
	writel(data, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_Q2);
	writel(HOST2BMC_INT_STS_DOORBELL | HOST2BMC_ENABLE_INTB, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS);


	return sizeof(u32);
}

static ssize_t write_8bytes_to_queue2(struct device *dev, time64_t * time_stamp, int timeout)
{
	int i = 0;
	u32 * time_buf = (u32*)time_stamp;
	struct aspeed_pci_bmc_dev *pci_bmc_device = dev_get_drvdata(dev);
	unsigned long tick_end = jiffies + timeout * HZ;

	while (i < 2) {
		if (!(readl(pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS) & HOST2BMC_Q2_FULL)) {
			writel(time_buf[i++], pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_Q2);
			writel(HOST2BMC_INT_STS_DOORBELL | HOST2BMC_ENABLE_INTB, pci_bmc_device->msg_bar_reg + ASPEED_PCI_BMC_HOST2BMC_STS);
		}

		if (time_after(jiffies, tick_end)) {
			return 0;
		}
	}
	return sizeof(time64_t);
}



static int astbmc_read_time(struct device *dev, struct rtc_time *dt)
{
	time64_t time_stamp = 0;
	u32 tx_cmd = 0x55555555;

	if (clear_r_queue1(dev, 5) == 0) {
		pr_info("01-Clear queue1 error!\n");
		return 0;
	}

	if (write_queue1_4bytes_poll(dev, tx_cmd, 3) == 0) {
		pr_info("01-Write queue1 bmc cmd error!\n");
		return 0;
	}

	if(read_8bytes_from_queue1(dev, &time_stamp, 5) == 0) {
		pr_info("01-Read queue1 bmc rtc error!\n");
		return 0;
	}

	rtc_time64_to_tm(time_stamp, dt);

	return 0;
}

static int astbmc_set_time(struct device *dev, struct rtc_time *dt)
{
	time64_t time_stamp = 0;
	u32 tx_cmd = 0xaaaaaaaa;

	if (write_queue2_4bytes_poll(dev, tx_cmd, 3) ==0 ) {
		pr_info("Write queue2 bmc cmd error!\n");
		return 0;
	}

	time_stamp = rtc_tm_to_time64(dt);

	if (write_8bytes_to_queue2(dev, &time_stamp, 3) == 0) {
		pr_info("Set queue2 bmc rtc error!\n");
		return 0;
	}

	return 0;
}

static const struct rtc_class_ops astbmc_rtc_ops = {
	.read_time = astbmc_read_time,
	.set_time = astbmc_set_time,
};


static int is_bmc_rtc_device_func_enable(struct device *dev)
{
	time64_t time_stamp = 0;
	u32 tx_cmd = 0x55555555;
	clear_r_queue1(dev, 5);

	if (write_queue1_4bytes_poll(dev, tx_cmd, 3) == 0) {
		pr_info("Write bmc cmd error!\n");
		return 0;
	}

	if (read_8bytes_from_queue1(dev, &time_stamp, 5) == 0) {
		pr_info("Read bmc rtc error!\n");
		return 0;
	}

	pr_info("BMC has enabled rtc device func!\n");
	return 1;

}


#define BMC_MSI_IDX_BASE	0
static int aspeed_pci_host_bmc_device_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct aspeed_pci_bmc_dev *pci_bmc_dev;
	struct device *dev = &pdev->dev;
	u16 config_cmd_val;
	int nr_entries;
	int rc = 0;

	pr_info("ASPEED BMC PCI ID %04x:%04x, IRQ=%u\n", pdev->vendor, pdev->device, pdev->irq);

	pci_bmc_dev = kzalloc(sizeof(*pci_bmc_dev), GFP_KERNEL);
	if (!pci_bmc_dev) {
		rc = -ENOMEM;
		dev_err(&pdev->dev, "kmalloc() returned NULL memory.\n");
		goto out_err;
	}

	rc = pci_enable_device(pdev);
	if (rc != 0) {
		dev_err(&pdev->dev, "pci_enable_device() returned error %d\n", rc);
		goto out_err;
	}

	pci_set_master(pdev);

	nr_entries = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (nr_entries < 0) {
		pci_bmc_dev->legency_irq = 1;
		pci_read_config_word(pdev, PCI_COMMAND, &config_cmd_val);
		config_cmd_val &= ~PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word((struct pci_dev *)pdev, PCI_COMMAND, config_cmd_val);

	} else {
		pci_bmc_dev->legency_irq = 0;
		pci_read_config_word(pdev, PCI_COMMAND, &config_cmd_val);
		config_cmd_val |= PCI_COMMAND_INTX_DISABLE;
		pci_write_config_word((struct pci_dev *)pdev, PCI_COMMAND, config_cmd_val);
		pdev->irq = pci_irq_vector(pdev, BMC_MSI_IDX_BASE);
	}

	pr_info("ASPEED BMC PCI ID %04x:%04x, IRQ=%u\n", pdev->vendor, pdev->device, pdev->irq);

	init_waitqueue_head(&pci_bmc_dev->tx_wait0);
	init_waitqueue_head(&pci_bmc_dev->tx_wait1);
	init_waitqueue_head(&pci_bmc_dev->rx_wait0);
	init_waitqueue_head(&pci_bmc_dev->rx_wait1);

	pci_bmc_dev->mem_bar_base = pci_resource_start(pdev, 0);
	pci_bmc_dev->mem_bar_size = pci_resource_len(pdev, 0);

	pr_info("BAR0 I/O Mapped Base Address is: %08lx End %08lx\n", pci_bmc_dev->mem_bar_base, pci_bmc_dev->mem_bar_size);

	pci_bmc_dev->mem_bar_reg = pci_ioremap_bar(pdev, 0);
	if (!pci_bmc_dev->mem_bar_reg) {
		rc = -ENOMEM;
		goto out_free0;
	}

	pci_bmc_dev->message_bar_base = pci_resource_start(pdev, 1);
	pci_bmc_dev->message_bar_size = pci_resource_len(pdev, 1);

	pr_info("MSG BAR1 Memory Mapped Base Address is: %08lx End %08lx\n", pci_bmc_dev->message_bar_base, pci_bmc_dev->message_bar_size);

	pci_bmc_dev->msg_bar_reg = pci_ioremap_bar(pdev, 1);
	if (!pci_bmc_dev->msg_bar_reg) {
		rc = -ENOMEM;
		goto out_free1;
	}

	(void)__raw_readl((void __iomem *)pci_bmc_dev->msg_bar_reg);


	pci_bmc_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	pci_bmc_dev->miscdev.name = DRIVER_NAME;
	pci_bmc_dev->miscdev.fops = &aspeed_pci_bmc_dev_fops;
	pci_bmc_dev->miscdev.parent = dev;

	rc = misc_register(&pci_bmc_dev->miscdev);
	if (rc) {
		pr_err("host bmc register fail %d\n", rc);
		goto out_free;
	}

	pci_set_drvdata(pdev, pci_bmc_dev);

	pr_info("Class code is: %08lx\n", pdev->class);

	if ((pdev->class >> 8) == PCI_CLASS_SYSTEM_RTC) {

		pr_info("BMC rtc device!\n");
		if (!is_bmc_rtc_device_func_enable(dev))
			return 0;

		rtc = devm_rtc_allocate_device(dev);
		if (IS_ERR(rtc))
			return PTR_ERR(rtc);

		rtc->ops = &astbmc_rtc_ops;
		rtc->range_min = RTC_TIMESTAMP_BEGIN_0000;
		rtc->range_max = RTC_TIMESTAMP_END_9999;

		devm_rtc_register_device(rtc);
	}

	return 0;

out_unreg:
	misc_deregister(&pci_bmc_dev->miscdev);
out_free1:
	pci_release_region(pdev, 1);
out_free0:
	pci_release_region(pdev, 0);
out_free:
	kfree(pci_bmc_dev);
out_err:
	pci_disable_device(pdev);

	return rc;

}



static void aspeed_pci_host_bmc_device_remove(struct pci_dev *pdev)
{
	struct aspeed_pci_bmc_dev *pci_bmc_dev = pci_get_drvdata(pdev);

	free_irq(pdev->irq, pdev);
	misc_deregister(&pci_bmc_dev->miscdev);
	pci_release_regions(pdev);
	kfree(pci_bmc_dev);
	pci_disable_device(pdev);
}

static struct pci_device_id aspeed_host_bmc_dev_pci_ids[] = {
	{ PCI_DEVICE(0x1A03, 0x2402), },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, aspeed_host_bmc_dev_pci_ids);

static struct pci_driver aspeed_host_bmc_dev_driver = {
	.name		= DRIVER_NAME,
	.id_table	= aspeed_host_bmc_dev_pci_ids,
	.probe		= aspeed_pci_host_bmc_device_probe,
	.remove		= aspeed_pci_host_bmc_device_remove,
};

static int __init aspeed_host_bmc_device_init(void)
{
	int ret;

	/* register pci driver */
	ret = pci_register_driver(&aspeed_host_bmc_dev_driver);
	if (ret < 0) {
		pr_err("pci-driver: can't register pci driver\n");
		return ret;
	}

	return 0;

}

static void aspeed_host_bmc_device_exit(void)
{
	pci_unregister_driver(&aspeed_host_bmc_dev_driver);
}

module_init(aspeed_host_bmc_device_init);
module_exit(aspeed_host_bmc_device_exit);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED Host BMC DEVICE Driver");
MODULE_LICENSE("GPL");
