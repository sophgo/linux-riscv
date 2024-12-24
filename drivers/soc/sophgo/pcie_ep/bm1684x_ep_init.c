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
#include "ap_pcie_ep.h"

static uint64_t pcie_base;
static uint32_t pcie_msi_data;
static uint32_t pcie_msi_addr;


static uint32_t cpu_read32(uint32_t addr)
{
	uint64_t va = pcie_base - 0x5fa00000 + addr;

	return readl((void __iomem *)va);
}

static uint32_t cpu_write32(uint32_t addr, uint32_t val)
{
	uint64_t va = pcie_base - 0x5fa00000 + addr;
	// printf("write addr:0x%lx\n", va);
	writel(val, (void __iomem *)va);

	return val;
}

static int bm1684x_set_vector(struct sophgo_pcie_ep *sg_ep)
{
	u32 msi_upper_addr = 0x0;
	u32 msi_low_addr = 0x0;
	u32 remote_msi_iatu_base_addr = 0x0;
	u32 chip_id = 0x0;
	u32 function_num = 0x0;
	u32 value = 0x0;
	u32 pcie_msi_low;
	int i;

	pcie_base = (uint64_t)sg_ep->ctrl_reg_base;
	pr_err("pcie base:0x%llx, config base:%px, config:0x%llx\n",
		pcie_base, sg_ep->ctrl_reg_base, (uint64_t)sg_ep->ctrl_reg_base);

	chip_id = cpu_read32(0x5fb80000);
	chip_id = (chip_id >> 28);
	chip_id &= 0x7;
	if (chip_id > 0)
		function_num = chip_id - 1;
	pr_info("chip id is 0x%x, function_num = 0x%x\n", chip_id, function_num);

	/* set remap table to write msi outbound iatu, use remap table 0x5fbf_1xxx */
	if (chip_id > 1) {
		remote_msi_iatu_base_addr += (0x5fbf1000 + (chip_id - 1) * 0x200);
		cpu_write32(0x5fb8f00c, 0x5fb00000 & ~0xfff);   // write low adddr;
		cpu_write32(0x5fb8f008, (0x3 << 8));
		cpu_write32(0x5fb8f05c, 0x5fb00000 & ~0xfff);  // read low adddr;
		cpu_write32(0x5fb8f058, (0x3 << 8));
	} else {
		remote_msi_iatu_base_addr = 0x5fb00000;
	}
	pr_info("remote_msi_iatu_base_addr = 0x%x\n", remote_msi_iatu_base_addr);

	/*set outbound iatu*/
	value = cpu_read32(remote_msi_iatu_base_addr);
	value &= ~(0x7 << 20);
	value |= (function_num << 20);
	cpu_write32(remote_msi_iatu_base_addr, value);

	value = cpu_read32(remote_msi_iatu_base_addr + 0x4);
	value |= (0x1 << 31);
	cpu_write32(remote_msi_iatu_base_addr + 0x4, value);

	msi_low_addr = cpu_read32(0x5fb8f004);
	pr_info("msg low addr :0x%x\n", msi_low_addr);
	pcie_msi_low = msi_low_addr;
	msi_low_addr &= (~0xfff);
	cpu_write32(remote_msi_iatu_base_addr + 0x8, msi_low_addr);
	cpu_write32(remote_msi_iatu_base_addr + 0x14, msi_low_addr);
	cpu_write32(remote_msi_iatu_base_addr + 0x10, msi_low_addr + 0xfff);

	cpu_write32(0x5fb8f004, msi_low_addr);
	msi_upper_addr = cpu_read32(0x5fb8f000);
	pr_info("msg upper addr:0x%x\n", msi_upper_addr);
	cpu_write32(remote_msi_iatu_base_addr + 0x18, msi_upper_addr);
	msi_upper_addr &= 0xf;
	msi_upper_addr |= ((0x3 << 8) | function_num << 5 | 0x1 << 4);
	cpu_write32(remote_msi_iatu_base_addr + 0xc, msi_upper_addr);

	cpu_write32(0x5fb8f000, msi_upper_addr);

	pcie_msi_data = cpu_read32(0x5fa0103c);
	pr_info("pcie msi data:0x%x\n", pcie_msi_data);
	pcie_msi_low &= (0xfff);
	pcie_msi_addr = 0x5fbf0000 + pcie_msi_low;

	pr_info("msi_high_addr:0x%x, msi low addr:0x%x, data:0x%x\n", msi_upper_addr,  msi_low_addr, pcie_msi_data);

	sg_ep->vector_allocated = 1;

	for (i = 0; i < 1; i++) {
		sg_ep->vector_info[i].vector_index = i;
		sg_ep->vector_info[i].msi_va = (void __iomem *)(pcie_base - 0x5fa00000 + pcie_msi_addr);
		sg_ep->vector_info[i].msi_data = pcie_msi_data;
		sg_ep->vector_info[i].vector_flag = VECTOR_EXCLUSIVE;
		sg_ep->vector_info[i].share_vector_reg = sg_ep->share_vector_reg;
		pr_err("msi:%d, vap:%p, vapx:%px, data:0x%llx\n", i, sg_ep->vector_info[i].msi_va,
			sg_ep->vector_info[i].msi_va, sg_ep->vector_info[i].msi_data);
	}
	for (; i < VECTOR_MAX; i++) {
		sg_ep->vector_info[i].vector_flag = VECTOR_INVALID;
		sg_ep->vector_info[i].vector_index = i;
	}

	return 0;
}

static int bm1684x_reset_vector(struct sophgo_pcie_ep *ep)
{
	return 0;
}

int bm1684x_ep_int(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pcie_ep *sg_ep = dev_get_drvdata(dev);
	struct resource *regs;

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (!regs)
		return pr_err("no config reg find\n");

	sg_ep->ctrl_reg_base = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!sg_ep->ctrl_reg_base) {
		pr_err("config base ioremap failed\n");
		goto failed;
	}

	sg_ep->set_vector = bm1684x_set_vector;
	sg_ep->reset_vector = bm1684x_reset_vector;

	return 0;
failed:
	return -1;

}
EXPORT_SYMBOL_GPL(bm1684x_ep_int);
