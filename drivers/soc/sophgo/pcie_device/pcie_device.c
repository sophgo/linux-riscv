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
#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/stddef.h>
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

#define ADDRES_MATCH	0
#define BAR_MATCH	1

#define PCIE_ATU_REGION_CTRL1		0x000

#define PCIE_ATU_REGION_CTRL2		0x004
#define PCIE_ATU_ENABLE			BIT(31)
#define PCIE_ATU_BAR_MODE_ENABLE	BIT(30)
#define PCIE_ATU_INHIBIT_PAYLOAD	BIT(22)
#define PCIE_ATU_FUNC_NUM_MATCH_EN      BIT(19)

#define PCIE_ATU_LOWER_BASE		0x008
#define PCIE_ATU_UPPER_BASE		0x00C
#define PCIE_ATU_LIMIT			0x010
#define PCIE_ATU_LOWER_TARGET		0x014
#define PCIE_ATU_UPPER_TARGET		0x018
#define PCIE_ATU_UPPER_LIMIT		0x020

#define PCIE_ATU_INCREASE_REGION_SIZE	BIT(13)

#define PCIE_ATU_FUNC_NUM(pf)           ((pf) << 20)

#define PCIE_ATU_TYPE_MEM		0x0

#define ATU_IB	1
#define ATU_OB	0

#define PCIE_ATU_BASE(dir, index) (((index) << 9) | (dir << 8))

#define TOP_REG_SIZE	(0x100000)

#define CDMA_CSR_OFFSET	(0x1000)

struct iatu {
	int match_type;
	int index;
	int type;
	uint64_t cpu_addr;
	uint64_t pci_addr;
	uint64_t size;
	uint32_t func;
	uint32_t bar;
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

static inline uint32_t sg_pcie_readl_atu(void *atu_base, uint32_t dir,
					uint32_t index, uint32_t reg)
{
	void __iomem *base = atu_base + PCIE_ATU_BASE(dir, index);

	return readl(base + reg);
}

static int find_available_ib_atu(void *atu_base)
{
	for (int i = 0; i < 32; i++) {
		uint32_t val = sg_pcie_readl_atu(atu_base, ATU_IB, i, PCIE_ATU_REGION_CTRL2);
		if (!(val & PCIE_ATU_ENABLE))
			return i;
	}

	return -1;
}

static inline void sg_pcie_writel_atu(void *atu_base, uint32_t dir,
					uint32_t index, uint32_t reg, uint32_t val)
{
	void __iomem *base = atu_base + PCIE_ATU_BASE(dir, index);

	writel(val, base + reg);
}

static int prog_inbound_iatu(void *atu_base, struct iatu *atu)
{
	uint64_t pci_addr = atu->pci_addr;
	uint64_t limit_addr = atu->pci_addr + atu->size - 1;
	uint64_t cpu_addr = atu->cpu_addr;
	uint32_t val = atu->type;
	uint32_t index = atu->index;

	if (atu->match_type == ADDRES_MATCH) {
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_LOWER_BASE,
					lower_32_bits(pci_addr));
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_UPPER_BASE,
					upper_32_bits(pci_addr));

		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_LIMIT,
					lower_32_bits(limit_addr));
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_UPPER_LIMIT,
					upper_32_bits(limit_addr));

		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_LOWER_TARGET,
					lower_32_bits(cpu_addr));
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_UPPER_TARGET,
					upper_32_bits(cpu_addr));

		if (upper_32_bits(limit_addr) > upper_32_bits(pci_addr))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;

		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_REGION_CTRL1, val);
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);
		pr_err("prg ib iatu%2u, 0x%llx -> 0x%llx\n", index, pci_addr, cpu_addr);
	} else if (atu->match_type == BAR_MATCH) {
		pr_err("prg ib iatu%2u, func%d bar%d -> 0x%llx\n", index, atu->func,
			atu->bar, cpu_addr);
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_LOWER_TARGET,
			lower_32_bits(cpu_addr));
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_UPPER_TARGET,
			upper_32_bits(cpu_addr));

		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_REGION_CTRL1, val |
			PCIE_ATU_FUNC_NUM(atu->func));
		sg_pcie_writel_atu(atu_base, ATU_IB, index, PCIE_ATU_REGION_CTRL2,
			PCIE_ATU_ENABLE | PCIE_ATU_FUNC_NUM_MATCH_EN |
			PCIE_ATU_BAR_MODE_ENABLE | (atu->bar << 8));
	} else {
		pr_err("error atu match type:0x%x\n", atu->match_type);
	}

	return 0;
}

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

	return 0;

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
	void __iomem *atu_base_addr = NULL;
	struct iatu atu;
	int atu_index;

	hdev->iatu_mask = 0x1; // BAR0 occupied iATU0
	atu_base_addr = hdev->BarVirt[0] + REG_OFFSET_PCIE_iATU;

	hdev->RegBases.PcieCfgBase = hdev->BarVirt[0];
	hdev->RegBases.PcieIatuBase = hdev->BarVirt[0] + REG_OFFSET_PCIE_iATU;
	pr_info("Start to set atu\n");

	atu_index = find_available_ib_atu(atu_base_addr);
	if (atu_index == -1) {
		dev_err(&hdev->pdev->dev, "no available ib atu for top reg map\n");
		return;
	}
	atu.index = atu_index;
	atu.pci_addr = hdev->BarPhys[1] & 0xffffffff;
	atu.cpu_addr = 0x7050000000;
	atu.size = TOP_REG_SIZE;
	atu.match_type = ADDRES_MATCH;
	prog_inbound_iatu(atu_base_addr, &atu);


	hdev->top_bar_vaddr = hdev->BarVirt[1];
	val = top_reg_read(hdev, 0x0);
	pr_info("link [Top_reg_0] the val = 0x%x\n", val);
	val = top_reg_read(hdev, 0x4);
	pr_info("[Top_reg_1] the val = 0x%x\n", val);
}

static int show_pcie_info(int pcie_id, char *head, struct pcie_info *info)
{
	pr_info("[%s pcie%d]->slot id:0x%llx\n", head, pcie_id, info->slot_id);
	pr_info("[%s pcie%d]->socket id:0x%llx\n", head, pcie_id, info->socket_id);
	pr_info("[%s pcie%d]->send port[%llu]:0x%llx\n", head, pcie_id, info->send_port, info->send_cdma_pa);
	pr_info("[%s pcie%d]->recv port[%llu]:0x%llx\n", head, pcie_id, info->recv_port, info->recv_cdma_pa);
	pr_info("[%s pcie%d]->data link type:%s\n", head, pcie_id,
		info->data_link_type == PCIE_DATA_LINK_C2C ? "c2c": "cascade");
	pr_info("[%s pcie%d]->link role:%s\n", head, pcie_id,
		info->link_role == PCIE_LINK_ROLE_EP ? "ep" : "rc");
	pr_info("[%s pcie%d]->peer slot id:0x%llx\n", head, pcie_id, info->peer_slotid);
	pr_info("[%s pcie%d]->peer socket id:0x%llx\n", head, pcie_id, info->peer_socketid);
	pr_info("[%s pcie%d]->peer pcie id:0x%llx\n", head, pcie_id, info->peer_pcie_id);
	pr_info("[%s pcie%d]->expect gen%llu_x%llu, current gen%llu_x%llu\n", head, pcie_id, info->max_link_speed, info->phy_role,
		info->current_link_speed, info->current_link_width);

	return 0;
}


static int init_cdma_route(void *cdma_reg_base, uint64_t peer_cdma_pa, uint32_t pcie_route_config)
{
	uint32_t tmp;

	tmp = (pcie_route_config << 28) | (peer_cdma_pa >> 32);
	writel(tmp, cdma_reg_base + CDMA_CSR_RCV_ADDR_H32);

	tmp = (peer_cdma_pa & ((1ul << 32) - 1)) >> 16;
	writel(tmp, cdma_reg_base + CDMA_CSR_RCV_ADDR_M16);

	// OS: 2
	tmp = readl(cdma_reg_base + CDMA_CSR_4) | (1 << CDMA_CSR_RCV_CMD_OS);
	writel(tmp, cdma_reg_base + CDMA_CSR_4);

	tmp = (readl(cdma_reg_base + CDMA_CSR_INTER_DIE_RW) &
		~(0xff << CDMA_CSR_INTER_DIE_WRITE_ADDR_L4)) |
		(pcie_route_config << CDMA_CSR_INTER_DIE_WRITE_ADDR_H4) |
		(0b0000 << CDMA_CSR_INTER_DIE_WRITE_ADDR_L4);
	writel(tmp, cdma_reg_base + CDMA_CSR_INTER_DIE_RW);

	tmp = (readl(cdma_reg_base + CDMA_CSR_INTRA_DIE_RW) &
		~(0xff << CDMA_CSR_INTRA_DIE_READ_ADDR_L4)) |
		(AXI_RN << CDMA_CSR_INTRA_DIE_READ_ADDR_H4) |
		(0b0000 << CDMA_CSR_INTRA_DIE_READ_ADDR_L4);
	writel(tmp, cdma_reg_base + CDMA_CSR_INTRA_DIE_RW);

	return 0;
}

static int init_c2c_cdma(struct p_dev *hdev)
{
	void __iomem *atu_base_addr;
	int atu_index;
	struct iatu atu;
	struct pcie_info *peer_pcie_info;
	struct pcie_info *myself_pcie_info;
	uint64_t myself_cdma_pa;
	void *myself_cdma_va;
	uint32_t myself_pcie_route;
	uint64_t peer_cdma_pa;
	void *peer_cdma_va;
	uint32_t peer_pcie_route;
	int ret = 0;

	myself_pcie_info = hdev->pci_info_base + hdev->bus_num * PER_INFO_SIZE;
	peer_pcie_info = myself_pcie_info + 1;
	myself_cdma_pa = myself_pcie_info->send_cdma_pa;
	peer_cdma_pa = peer_pcie_info->send_cdma_pa;
	myself_pcie_route = myself_pcie_info->pcie_route;
	peer_pcie_route = peer_pcie_info->pcie_route;

	dev_err(&hdev->pdev->dev, "myself cdma pa:0x%llx, peer cdma pa:0x%llx, pcie route:0x%x\n",
		myself_cdma_pa, peer_cdma_pa, myself_pcie_route);

	myself_cdma_va = ioremap(myself_cdma_pa, 0x10000);
	if (myself_cdma_va == NULL) {
		dev_err(&hdev->pdev->dev, "failed top map cdma[%llu]:0x%llx\n", myself_pcie_info->send_port,
			myself_cdma_pa);

		return -1;
	}
	init_cdma_route(myself_cdma_va, peer_cdma_pa, peer_pcie_route);

	atu_base_addr = hdev->BarVirt[0] + REG_OFFSET_PCIE_iATU;
	atu_index = find_available_ib_atu(atu_base_addr);
	if (atu_index == -1) {
		dev_err(&hdev->pdev->dev, "no availabe atu for cdma[%llu]:0x%llx map\n", peer_pcie_info->send_port,
			peer_cdma_pa);
		ret = -1;
		goto release_myself_cdma;
	}
	atu.index = atu_index;
	atu.pci_addr = (hdev->BarPhys[1] & 0xffffffff) + TOP_REG_SIZE;
	atu.cpu_addr = peer_cdma_pa;
	atu.size = 0x10000;
	atu.type = ADDRES_MATCH;
	prog_inbound_iatu(atu_base_addr, &atu);
	peer_cdma_va = hdev->BarVirt[1] + TOP_REG_SIZE;
	init_cdma_route(peer_cdma_va, myself_cdma_pa, myself_pcie_route);

release_myself_cdma:
	iounmap(myself_cdma_va);

	return ret;
}

static int build_pcie_info(struct p_dev *hdev)
{
	struct pcie_info *myself_pcie_info;
	struct pcie_info *peer_pcie_info;
	void *info_addr;
	uint64_t current_link_speed;
	uint64_t current_link_width;
	uint16_t status;
	int err;

	err = pcie_capability_read_word(hdev->pdev, PCI_EXP_LNKSTA, &status);
	if (err) {
		pr_err("[pcie device]:pcie%d failed to read current link speed\n", hdev->bus_num);
		return -1;
	}
	current_link_speed = status & PCI_EXP_LNKSTA_CLS;
	current_link_width = FIELD_GET(PCI_EXP_LNKSTA_NLW, status);
	pr_err("[pcie device]:pcie%d current link speed:0x%llx, current link width:0x%llx\n",
		hdev->bus_num, current_link_speed, current_link_width);

	hdev->pci_info_base = ioremap(PCIE_INFO_BASE, PCIE_INFO_SIZE);
	if (!hdev->pci_info_base) {
		pr_err("[pcie device]: failed to map pci info base\n");
		goto failed;
	}
	myself_pcie_info = hdev->pci_info_base + hdev->bus_num * PER_INFO_SIZE;
	peer_pcie_info = myself_pcie_info + 1;

	myself_pcie_info->current_link_speed = current_link_speed;
	myself_pcie_info->current_link_width = current_link_width;

	pr_info("[pcie device]:pcie%d, myself pcie info addr:%px, peer pcie info addr:%px\n", hdev->bus_num,
		myself_pcie_info, peer_pcie_info);
	show_pcie_info(hdev->bus_num, "myself", myself_pcie_info);

	info_addr = hdev->BarVirt[3];
	memcpy_fromio(peer_pcie_info, info_addr, sizeof(struct pcie_info));
	peer_pcie_info->current_link_speed = current_link_speed;
	peer_pcie_info->current_link_width = current_link_width;
	show_pcie_info(peer_pcie_info->peer_pcie_id, "peer", peer_pcie_info);

	if (peer_pcie_info->peer_pcie_id > 10 || peer_pcie_info->send_port > 10) {
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

static int clean_pcie_info(struct p_dev *hdev)
{
	struct pcie_info *myself_info_addr;
	struct pcie_info *peer_info_addr;

	myself_info_addr = hdev->pci_info_base + hdev->bus_num * PER_INFO_SIZE;
	memset_io(myself_info_addr + 1, 0x5a, sizeof(struct pcie_info));

	peer_info_addr = hdev->BarVirt[1] + CONFIG_STRUCT_OFFSET + hdev->bus_num * PER_INFO_SIZE +
			 sizeof(struct pcie_info);
	memset(peer_info_addr, 0x5a, sizeof(struct pcie_info));

	iounmap(hdev->pci_info_base);

	return 0;
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

	writel(0x0, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x314));
	writel(0x0, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x318));
	writel(0x0, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x300));
	writel(0xC0080400, (pcie_dbi_base + C2C_PCIE_ATU_OFFSET + 0x304));

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
		ret = init_c2c_cdma(hdev);

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
	dev_err(&pdev->dev, "remove pci bus-0x%x ep device\n", pdev->bus->number);
	clean_pcie_info(hdev);
#if ((defined(SG2260_PLD)) && ((defined(SG2260_PLD_MSI)) || (defined(SG2260_PLD_MSI_X))))
	for (int i = 0; i < MSI_X_NUM; i++) {
	      //free_irq((pdev->irq+i),hdev);
		free_irq(msix_entries[i].vector, hdev);
	}
	pci_free_irq_vectors(pdev);
#else
	//free_irq(pdev->irq, hdev);
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

