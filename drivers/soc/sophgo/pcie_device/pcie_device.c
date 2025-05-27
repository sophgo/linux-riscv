// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-p2pdma.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>
#include <linux/mmzone.h>
#include <linux/io.h>
#include <linux/pagemap.h>
#include <linux/circ_buf.h>
#include <linux/device.h>
#include "pcie_device.h"
#include "../c2c_rc/c2c_rc.h"

#define DRV_NAME "sgdrv"

static unsigned long long pci_dma_mask = DMA_BIT_MASK(40);
#define PCIE_INFO_BAR	0x2 //TODO: check pcie info bar

struct REG_BASES {
	// BAR0
	void __iomem *PcieCfgBase;
	void __iomem *PcieIatuBase;
	void __iomem *PcieTopBase;
	// BAR0
	void __iomem *BootRomBase;
	void __iomem *AxiSramBase;
	void __iomem *TopBase;
	void __iomem *Intc2Base;
	void __iomem *Intc3Base;
	void __iomem *CdmaBase;
};

struct p_dev {
	struct pci_dev *pdev;
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	uint32_t iatu_mask;
	void __iomem *BarVirt[4];
	resource_size_t BarPhys[4];
	resource_size_t BarLength[4];
	struct REG_BASES RegBases;

	void __iomem *shmem_bar_vaddr;
	void __iomem *sram_bar_vaddr;
	void __iomem *top_bar_vaddr;
	void __iomem *cdma_bar_vaddr;
	void __iomem *c2c_top_bar_vaddr;
	void __iomem *misc_bar_vaddr;
	void __iomem *copy_data_bar_vaddr;

	void __iomem *pci_info_base;
	int bus_num;
	int data_link_role;
};

static u32 top_reg_read(struct p_dev *hdev, u32 reg_offset)
{
	return ioread32(hdev->top_bar_vaddr + reg_offset);
}
//comment for compile unused warning
#if 0
static void top_reg_write(struct p_dev *hdev, u32 reg_offset, u32 val)
{
	iowrite32(val, hdev->top_bar_vaddr + reg_offset);
}

static irqreturn_t pci_irq_handler(int irq, void *data)
{
	pr_info("IRQ handler start.....\n");

	return IRQ_HANDLED;
}
#endif
static int pci_platform_init(struct pci_dev *pdev)
{
	struct p_dev *hdev = pci_get_drvdata(pdev);
	int ret;
	int i;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		pci_err(pdev, "can't enable PCI device\n");
		goto err0_out;
	}
	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret < 0) {
		pci_err(pdev, "cannot reserve memory region\n");
		goto err1_out;
	}

	// BAR0
	hdev->BarPhys[0] = pci_resource_start(pdev, 0);
	hdev->BarLength[0] = pci_resource_len(pdev, 0);
	hdev->BarVirt[0] = pci_iomap(pdev, 0, 0);
	// FIXME
	if (hdev->BarPhys[0] == 0) {
		pci_err(pdev, "skip root port\n");
		ret = -EINVAL;
		goto err2_out;
	}
	// BAR1
	hdev->BarPhys[1] = pci_resource_start(pdev, 1);
	hdev->BarLength[1] = pci_resource_len(pdev, 1);
	hdev->BarVirt[1] = pci_iomap(pdev, 1, 0);
	// BAR2
	hdev->BarPhys[2] = pci_resource_start(pdev, 2);
	hdev->BarLength[2] = pci_resource_len(pdev, 2);
	hdev->BarVirt[2] = pci_iomap(pdev, 2, 0);
	// BAR4 (actual index is 3)
	hdev->BarPhys[3] = pci_resource_start(pdev, 4);
	hdev->BarLength[3] = pci_resource_len(pdev, 4);
	hdev->BarVirt[3] = pci_iomap(pdev, 4, 0);

	for (i = 0; i < 4; i++) {
		pci_info(pdev, "BAR%d address 0x%px/0x%llx length 0x%llx\n",
			i, hdev->BarVirt[i],
			hdev->BarPhys[i],
			hdev->BarLength[i]);
	}

	pci_set_master(pdev);

	if (pci_try_set_mwi(pdev))
		pci_info(pdev, "Memory-Write-Invalidate not support\n");

	if (dma_set_mask(&pdev->dev, pci_dma_mask)) {
		pci_err(pdev, "setup DMA mask failed\n");
		ret = -EFAULT;
		goto err2_out;
	}
	if (dma_set_mask_and_coherent(&pdev->dev, pci_dma_mask)) {
		pci_err(pdev, "setup consistent DMA mask failed\n");
		ret = -EFAULT;
		goto err2_out;
	}
	pcie_capability_clear_word(pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_NOSNOOP_EN);


	ret = pci_alloc_irq_vectors(pdev, 1, 2, PCI_IRQ_MSI);
	if (ret <= 0) {
		pci_err(pdev, "alloc MSI IRQ failed %d\n", ret);
		ret = -1;
		goto err2_out;
	}
#if 0
	ret = request_threaded_irq(pdev->irq, NULL, pci_irq_handler, 0, DRV_NAME, hdev);
	if (ret < 0) {
		pci_err(pdev, "request IRQ failed %d\n", ret);
		ret = -EFAULT;
		goto err2_out;
	}

	pci_info(pdev, "MSI IRQ %d\n", pdev->irq);
#endif
	hdev->bus_num = pdev->bus->number >> 4;
	dev_err(&pdev->dev, "probe pci bus-0x%x ep device, fix device pcie bus to 0x%x\n",
		pdev->bus->number, hdev->bus_num);

err2_out:
	pci_release_regions(pdev);
err1_out:
	pci_disable_device(pdev);
err0_out:
	if (hdev) {
		pci_set_drvdata(pdev, NULL);
		kfree(hdev);
	}

	return ret;
}

static void bm1690_map_bar(struct p_dev *hdev, struct pci_dev *pdev)
{
	uint32_t val = 0;
	uint32_t c2c_id = 0;
	uint64_t c2c_base = 0;
	void __iomem *atu_base_addr = NULL;

	hdev->iatu_mask = 0x1; // BAR0 occupied iATU0
	atu_base_addr = hdev->BarVirt[0] + REG_OFFSET_PCIE_iATU;

	hdev->RegBases.PcieCfgBase = hdev->BarVirt[0];
	hdev->RegBases.PcieIatuBase = hdev->BarVirt[0] + REG_OFFSET_PCIE_iATU;
	pr_info("Start to set atu\n");

	// sram 2M
	REG_WRITE32(atu_base_addr, 0x300, 0);
	REG_WRITE32(atu_base_addr, 0x304, 0x80000100);
	REG_WRITE32(atu_base_addr, 0x308, (u32)(hdev->BarPhys[1] & 0xffffffff));                //src addr
	REG_WRITE32(atu_base_addr, 0x30C, 0);
	REG_WRITE32(atu_base_addr, 0x310, (u32)(hdev->BarPhys[1] & 0xffffffff) + 0x1fffff);       //size 2M
	REG_WRITE32(atu_base_addr, 0x314, 0x10000000);          //dst addr
	REG_WRITE32(atu_base_addr, 0x318, 0x70);
	hdev->sram_bar_vaddr = hdev->BarVirt[1];

	//bm1690 top sys_ctrl
	REG_WRITE32(atu_base_addr, 0x500, 0);
	REG_WRITE32(atu_base_addr, 0x504, 0x80000100);
	REG_WRITE32(atu_base_addr, 0x508, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART2_OFFSET);                //src addr
	REG_WRITE32(atu_base_addr, 0x50C, 0);
	REG_WRITE32(atu_base_addr, 0x510, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART2_OFFSET + 0x7fff);       //size 3M
	REG_WRITE32(atu_base_addr, 0x514, 0x50000000);          //dst addr
	REG_WRITE32(atu_base_addr, 0x518, 0x70);

	hdev->top_bar_vaddr = hdev->BarVirt[1] + BAR1_PART2_OFFSET;
	val = top_reg_read(hdev, 0x0);
	pr_info("[Top_reg_0] the val = 0x%x\n", val);
	val = top_reg_read(hdev, 0x4);
	pr_info("[Top_reg_1] the val = 0x%x\n", val);
	c2c_id = (val >> 3) & 0x3;
	if (c2c_id == 2)
		c2c_id = 4;
	c2c_base = 0x6c00000000 + (c2c_id * 0x02000000) + 0x780000;

	// c2c cdma, top, 512K
	REG_WRITE32(atu_base_addr, 0x700, 0);
	REG_WRITE32(atu_base_addr, 0x704, 0x80000100);
	REG_WRITE32(atu_base_addr, 0x708, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART1_OFFSET);  //src addr
	REG_WRITE32(atu_base_addr, 0x70C, 0);
	REG_WRITE32(atu_base_addr, 0x710, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART1_OFFSET + 0x7ffff);       //size 3M
	REG_WRITE32(atu_base_addr, 0x714, (c2c_base & 0xffffffff));          //dst addr
	REG_WRITE32(atu_base_addr, 0x718, 0x6c);
	hdev->cdma_bar_vaddr = hdev->BarVirt[1] + BAR1_PART1_OFFSET + 0x10000; //0x6c00790000

	// MTLI-2 AP 8K
	REG_WRITE32(atu_base_addr, 0x900, 0);
	REG_WRITE32(atu_base_addr, 0x904, 0x80000100);
	REG_WRITE32(atu_base_addr, 0x908, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART3_OFFSET); //src addr
	REG_WRITE32(atu_base_addr, 0x90C, 0);
	REG_WRITE32(atu_base_addr, 0x910, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART3_OFFSET + 0x1fff);//size 256K
	REG_WRITE32(atu_base_addr, 0x914, 0x10000000);          //dst addr
	REG_WRITE32(atu_base_addr, 0x918, 0x6e);

	// misc 1M
	REG_WRITE32(atu_base_addr, 0xb00, 0);
	REG_WRITE32(atu_base_addr, 0xb04, 0x80000100);
	REG_WRITE32(atu_base_addr, 0xb08, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART4_OFFSET);  //src addr
	REG_WRITE32(atu_base_addr, 0xb0C, 0);
	REG_WRITE32(atu_base_addr, 0xb10, (u32)(hdev->BarPhys[1] & 0xffffffff)
			+ BAR1_PART4_OFFSET + 0xfffff);       //size 3M
	REG_WRITE32(atu_base_addr, 0xb14, 0x40000000);          //dst addr
	REG_WRITE32(atu_base_addr, 0xb18, 0x70);
	hdev->misc_bar_vaddr = hdev->BarVirt[1] + BAR1_PART4_OFFSET;

	REG_WRITE32(atu_base_addr, 0xd00, 0);
	REG_WRITE32(atu_base_addr, 0xd04, 0x80000100);
	REG_WRITE32(atu_base_addr, 0xd08, (u32)(hdev->BarPhys[3] & 0xffffffff));//src addr
	REG_WRITE32(atu_base_addr, 0xd0C, hdev->BarPhys[3] >> 32);
	REG_WRITE32(atu_base_addr, 0xd10, (u32)(hdev->BarPhys[3] & 0xffffffff)
				+ 0xFFFFF); //1M size
	REG_WRITE32(atu_base_addr, 0xd14, 0x10080000); //dst addr
	REG_WRITE32(atu_base_addr, 0xd18, 0x70);
	hdev->copy_data_bar_vaddr = hdev->BarVirt[3];
}

static int show_pcie_info(int pcie_id, char *head, struct pcie_info *info)
{
	pr_info("[%s pcie%d]->slot id:0x%llx\n", head, pcie_id, info->slot_id);
	pr_info("[%s pcie%d]->socket id:0x%llx\n", head, pcie_id, info->socket_id);
	pr_info("[%s pcie%d]->send port:0x%llx\n", head, pcie_id, info->send_port);
	pr_info("[%s pcie%d]->recv port:0x%llx\n", head, pcie_id, info->recv_port);
	pr_info("[%s pcie%d]->data link type:%s\n", head, pcie_id,
		info->data_link_type == PCIE_DATA_LINK_C2C ? "c2c": "cascade");
	pr_info("[%s pcie%d]->link role:%s\n", head, pcie_id,
		info->link_role == PCIE_LINK_ROLE_EP ? "ep" : "rc");
	pr_info("[%s pcie%d]->peer slot id:0x%llx\n", head, pcie_id, info->peer_slotid);
	pr_info("[%s pcie%d]->peer socket id:0x%llx\n", head, pcie_id, info->peer_socketid);
	pr_info("[%s pcie%d]->peer pcie id:0x%llx\n", head, pcie_id, info->peer_pcie_id);

	return 0;
}

static int build_pcie_info(struct p_dev *hdev)
{
	struct pcie_info *myself_pcie_info;
	struct pcie_info *peer_pcie_info;
	void *info_addr;

	hdev->pci_info_base = ioremap(PCIE_INFO_BASE, PCIE_INFO_SIZE);
	if (!hdev->pci_info_base) {
		pr_err("[pcie device]: failed to map pci info base\n");
		goto failed;
	}
	myself_pcie_info = hdev->pci_info_base + hdev->bus_num * PER_INFO_SIZE;
	peer_pcie_info = myself_pcie_info + 1;
	pr_info("[pcie device]:pcie%d, myself pcie info addr:%px, peer pcie info addr:%px\n", hdev->bus_num,
		myself_pcie_info, peer_pcie_info);
	show_pcie_info(hdev->bus_num, "myself", myself_pcie_info);

	info_addr = hdev->BarVirt[1] + CONFIG_STRUCT_OFFSET + hdev->bus_num * PER_INFO_SIZE;
	memcpy_fromio(peer_pcie_info, info_addr, sizeof(struct pcie_info));
	show_pcie_info(hdev->bus_num, "peer", peer_pcie_info);

	if (peer_pcie_info->peer_pcie_id != hdev->bus_num) {
		pr_err("[pcie device]:error pcie link, RC and EP are not match\n");
		pr_err("[pcie device]:my bus num is %d, but peer expect 0x%llx\n", hdev->bus_num,
			peer_pcie_info->peer_pcie_id);
		memset(peer_pcie_info, PCIE_INFO_DEF_VAL, sizeof(struct pcie_info));
		goto unmap_pci_info;
	}

	memcpy_toio(info_addr + sizeof(struct pcie_info), myself_pcie_info,
		    sizeof(struct pcie_info));

	return 0;
unmap_pci_info:
	iounmap(hdev->pci_info_base);
failed:
	return -1;
}

static int config_ep_huge_bar(struct p_dev *hdev)
{
	void __iomem *pcie_dbi_base;
	uint32_t val;
	uint32_t func = 0; //TODO: why 0?

	pcie_dbi_base = hdev->BarVirt[0]; //TODO: is bar0?

	//enable DBI_RO_WR_EN
	val = readl(pcie_dbi_base + 0x8bc);
	val = (val & 0xfffffffe) | 0x1;
	writel(val, (pcie_dbi_base + 0x8bc));

	writel(0xffffffff, (void *)((uint64_t)(pcie_dbi_base + C2C_PCIE_DBI2_OFFSET + 0x20) | (func << 16)));
	writel(0x1fffff, (void *)((uint64_t)(pcie_dbi_base + C2C_PCIE_DBI2_OFFSET + 0x24) | (func << 16)));

	// disable DBI_RO_WR_EN
	val = readl(pcie_dbi_base + 0x8bc);
	val &= 0xfffffffe;
	writel(val, (pcie_dbi_base + 0x8bc));

	readl(pcie_dbi_base + 0x20);
	writel(0x0, (pcie_dbi_base + 0x20));

	writel(0x0, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x514));
	writel(0x0, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x518));
	writel(0x0, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x500));
	writel(0xC0080400, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x504));

	return 0;
}

static int pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret = 0;
	struct p_dev *hdev;
	void __iomem *top_base;

	dev_err(&pdev->dev, "vid:[0x%x], pid:[0x%x], sub-device:[0x%x]\n", id->vendor, id->device, id->subdevice);

	hdev = kzalloc(sizeof(struct p_dev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	hdev->pdev = pdev;
	hdev->iatu_mask = 0;
	hdev->parent = &pdev->dev;
	hdev->data_link_role = id->subdevice;//TODO: which to match pcie/c2c
	pci_set_drvdata(pdev, hdev);

	pci_platform_init(pdev);
	bm1690_map_bar(hdev, pdev);

	if (id->subdevice == PCIE_DATA_LINK_C2C_DEVICEID) {
		ret = build_pcie_info(hdev);
		if (ret)
			goto failed;

		config_ep_huge_bar(hdev);
		sophgo_set_c2c_ready(hdev->bus_num);
	} else {
		top_base = ioremap(0x7050000000, 0x1000);
		pr_err("top ioremap va:0x%llx\n", (uint64_t)top_base);
		writel(0x5, top_base + 0x1c4);
	}

	pr_info("[pcie device]:bus%d probe done\n", hdev->bus_num);

	return 0;
failed:
	return ret;
}

static void pci_remove(struct pci_dev *pdev)
{
	struct p_dev *hdev = pci_get_drvdata(pdev);

	if (hdev == NULL)
		return;

#if ((defined(SG2260_PLD)) && ((defined(SG2260_PLD_MSI)) || (defined(SG2260_PLD_MSI_X))))
	for (int i = 0; i < MSI_X_NUM; i++) {
	      //free_irq((pdev->irq+i),hdev);
		free_irq(msix_entries[i].vector, hdev);
	}
	pci_free_irq_vectors(pdev);
#else
	free_irq(pdev->irq, hdev);
	pci_disable_msi(pdev);
#endif
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(hdev);
}

static struct pci_device_id pci_table[] = {
	{PCI_DEVICE(0x1E30, 0x1684)},
	{PCI_DEVICE(0x1f1C, 0x1686)},
	{.vendor = 0x1f1C, .device = 0x1690, .subvendor = PCI_ANY_ID, .subdevice = PCIE_DATA_LINK_PCIE, 0, 0},
	{.vendor = 0x1f1C, .device = 0x1690, .subvendor = PCI_ANY_ID, .subdevice = PCIE_DATA_LINK_C2C_DEVICEID, 0, 0},
	{.vendor = 0x1f1C, .device = 0x1690, .subvendor = PCI_ANY_ID, .subdevice = 0x10, 0, 0},
	{.vendor = 0x1f1C, .device = 0x1690, .subvendor = PCI_ANY_ID, .subdevice = 0x11, 0, 0},
	{.vendor = 0x1f1C, .device = 0x1690, .subvendor = PCI_ANY_ID, .subdevice = 0x12, 0, 0},
	{.vendor = 0x1f1C, .device = 0x1690, .subvendor = PCI_ANY_ID, .subdevice = 0x13, 0, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

static struct pci_driver pci_dma_driver = {
	.name		= DRV_NAME,
	.id_table	= pci_table,
	.probe		= pci_probe,
	.remove		= pci_remove,
	// .shutdown	= pci_shutdown,
};

static int __init pci_init(void)
{
	int ret;

	ret = pci_register_driver(&pci_dma_driver);
	return ret;
}

static void __exit pci_exit(void)
{
	pci_unregister_driver(&pci_dma_driver);
}

module_init(pci_init);
module_exit(pci_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for c2c/pcie devices driver");

