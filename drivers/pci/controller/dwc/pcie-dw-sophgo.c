// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Sophgo SoCs.
 *
 * Copyright (C) 2023 Sophgo Tech Co., Ltd.
 *		http://www.sophgo.com
 *
 * Author: Lionel Li <fengchun.li@sophgo.com>
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include "../../pci.h"
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/pci-ecam.h>
#include <linux/dma-direct.h>

#include "pcie-dw-sophgo.h"

int sophgo_dw_pcie_probe(struct platform_device *pdev);

static void sophgo_dw_intx_mask(struct irq_data *data)
{
	struct sophgo_dw_pcie *pcie = irq_data_get_irq_chip_data(data);
	unsigned long flags = 0;
	u32 val = 0;

	raw_spin_lock_irqsave(&pcie->pp.lock, flags);
	val = sophgo_dw_pcie_read_ctrl(pcie, PCIE_CTRL_IRQ_EN_REG, 4);
	val &= ~BIT(data->hwirq + PCIE_CTRL_IRQ_EN_INTX_SHIFT_BIT);
	sophgo_dw_pcie_write_ctrl(pcie, PCIE_CTRL_IRQ_EN_REG, 4, val);
	raw_spin_unlock_irqrestore(&pcie->pp.lock, flags);
}

static void sophgo_dw_intx_unmask(struct irq_data *data)
{
	struct sophgo_dw_pcie *pcie = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pcie->pp.lock, flags);
	val = sophgo_dw_pcie_read_ctrl(pcie, PCIE_CTRL_IRQ_EN_REG, 4);
	val |= BIT(data->hwirq + PCIE_CTRL_IRQ_EN_INTX_SHIFT_BIT);
	sophgo_dw_pcie_write_ctrl(pcie, PCIE_CTRL_IRQ_EN_REG, 4, val);
	raw_spin_unlock_irqrestore(&pcie->pp.lock, flags);
}

/**
 * mtk_intx_eoi() - Clear INTx IRQ status at the end of interrupt
 * @data: pointer to chip specific data
 *
 * As an emulated level IRQ, its interrupt status will remain
 * until the corresponding de-assert message is received; hence that
 * the status can only be cleared when the interrupt has been serviced.
 */
static void sophgo_dw_intx_eoi(struct irq_data *data)
{

}

static int sophgo_dw_pcie_set_affinity(struct irq_data *data,
				 const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static struct irq_chip sophgo_dw_intx_irq_chip = {
	.irq_mask		= sophgo_dw_intx_mask,
	.irq_unmask		= sophgo_dw_intx_unmask,
	.irq_eoi		= sophgo_dw_intx_eoi,
	.irq_set_affinity	= sophgo_dw_pcie_set_affinity,
	.name			= "sophgo-dw-intx",
};

static int sophgo_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, domain->host_data);
	irq_set_chip_and_handler_name(irq, &sophgo_dw_intx_irq_chip,
				      handle_fasteoi_irq, "sophgo-dw-intx");
	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = sophgo_pcie_intx_map,
};

static int sophgo_dw_pcie_init_intx_domains(struct sophgo_dw_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *intc_node, *node = dev->of_node;
	int ret = 0;

	/* Setup INTx */
	intc_node = of_get_child_by_name(node, "interrupt-controller");
	if (!intc_node) {
		dev_err(dev, "missing interrupt-controller node\n");
		return -ENODEV;
	}

	pcie->intx_domain = irq_domain_add_linear(intc_node, PCI_NUM_INTX,
						  &intx_domain_ops, pcie);
	if (!pcie->intx_domain) {
		dev_err(dev, "failed to create INTx IRQ domain\n");
		ret = -ENODEV;
	}

	of_node_put(intc_node);

	return ret;
}

static void sophgo_dw_pcie_irq_handler(struct irq_desc *desc)
{
	struct sophgo_dw_pcie *pcie = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long status;
	irq_hw_number_t irq_bit = PCIE_CTRL_INT_SIG_0_PCIE_INTX_SHIFT_BIT;

	chained_irq_enter(irqchip, desc);

	status = sophgo_dw_pcie_read_ctrl(pcie, PCIE_CTRL_INT_SIG_0_REG, 4);
	for_each_set_bit_from(irq_bit, &status, PCI_NUM_INTX +
			      PCIE_CTRL_INT_SIG_0_PCIE_INTX_SHIFT_BIT)
		generic_handle_domain_irq(pcie->intx_domain,
					  irq_bit - PCIE_CTRL_INT_SIG_0_PCIE_INTX_SHIFT_BIT);

	chained_irq_exit(irqchip, desc);

}

static int sophgo_dw_pcie_setup_irq(struct sophgo_dw_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int err;

	err = sophgo_dw_pcie_init_intx_domains(pcie);
	if (err)
		return err;

	pcie->irq = platform_get_irq_byname(pdev, "pcie_irq");
	pr_info("%s, irq = %d\n", __func__, pcie->irq);
	if (pcie->irq < 0)
		return pcie->irq;

	irq_set_chained_handler_and_data(pcie->irq, sophgo_dw_pcie_irq_handler, pcie);

	return 0;
}

static void sophgo_dw_pcie_msi_ack_irq(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void sophgo_dw_pcie_msi_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void sophgo_dw_pcie_msi_unmask_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip sophgo_dw_pcie_msi_irq_chip = {
	.name = "sophgo-dw-msi",
	.irq_ack = sophgo_dw_pcie_msi_ack_irq,
	.irq_mask = sophgo_dw_pcie_msi_mask_irq,
	.irq_unmask = sophgo_dw_pcie_msi_unmask_irq,
};

static struct msi_domain_info sophgo_dw_pcie_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &sophgo_dw_pcie_msi_irq_chip,
};

static int sophgo_dw_pcie_msi_setup(struct dw_pcie_rp *pp)
{
	struct irq_domain *irq_parent = sophgo_get_msi_irq_domain();
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	struct fwnode_handle *fwnode = of_node_to_fwnode(pcie->dev->of_node);

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
	if (!acpi_disabled)
		fwnode = acpi_fwnode_handle(to_acpi_device(pcie->dev));
#endif

	pp->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &sophgo_dw_pcie_msi_domain_info,
						   irq_parent);
	if (!pp->msi_domain) {
		dev_err(pcie->dev, "create msi irq domain failed\n");
		return -ENODEV;
	}

	return 0;
}

u32 sophgo_dw_pcie_read_ctrl(struct sophgo_dw_pcie *pcie, u32 reg, size_t size)
{
	int ret = 0;
	u32 val = 0;

	ret = dw_pcie_read(pcie->ctrl_reg_base + reg, size, &val);
	if (ret)
		dev_err(pcie->dev, "Read ctrl address failed\n");

	return val;
}

void sophgo_dw_pcie_write_ctrl(struct sophgo_dw_pcie *pcie, u32 reg, size_t size, u32 val)
{
	int ret = 0;

	ret = dw_pcie_write(pcie->ctrl_reg_base + reg, size, val);
	if (ret)
		dev_err(pcie->dev, "Write ctrl address failed\n");
}

u32 sophgo_dw_pcie_read_dbi(struct sophgo_dw_pcie *pcie, u32 reg, size_t size)
{
	int ret = 0;
	u32 val = 0;

	ret = dw_pcie_read(pcie->dbi_base + reg, size, &val);
	if (ret)
		dev_err(pcie->dev, "Read DBI address failed\n");

	return val;
}

void sophgo_dw_pcie_write_dbi(struct sophgo_dw_pcie *pcie, u32 reg, size_t size, u32 val)
{
	int ret = 0;

	ret = dw_pcie_write(pcie->dbi_base + reg, size, val);
	if (ret)
		dev_err(pcie->dev, "Write DBI address failed\n");
}

static int sophgo_dw_pcie_link_up(struct sophgo_dw_pcie *pcie)
{
	u32 val = 0;

	val = sophgo_dw_pcie_readl_dbi(pcie, PCIE_PORT_DEBUG1);
	return ((val & PCIE_PORT_DEBUG1_LINK_UP) &&
		(!(val & PCIE_PORT_DEBUG1_LINK_IN_TRAINING)));
}

u32 sophgo_dw_pcie_readl_atu(struct sophgo_dw_pcie *pcie, u32 dir, u32 index, u32 reg)
{
	void __iomem *base;
	int ret = 0;
	u32 val = 0;

	base = sophgo_dw_pcie_select_atu(pcie, dir, index);

	ret = dw_pcie_read(base + reg, 4, &val);
	if (ret)
		dev_err(pcie->dev, "Read ATU address failed\n");

	return val;
}

void sophgo_dw_pcie_writel_atu(struct sophgo_dw_pcie *pcie, u32 dir, u32 index,
			       u32 reg, u32 val)
{
	void __iomem *base;
	int ret = 0;

	base = sophgo_dw_pcie_select_atu(pcie, dir, index);

	ret = dw_pcie_write(base + reg, 4, val);
	if (ret)
		dev_err(pcie->dev, "Write ATU address failed\n");
}

static void sophgo_dw_pcie_disable_atu(struct sophgo_dw_pcie *pcie, u32 dir, int index)
{
	sophgo_dw_pcie_writel_atu(pcie, dir, index, PCIE_ATU_REGION_CTRL2, 0);
}

static int sophgo_dw_pcie_prog_outbound_atu(struct sophgo_dw_pcie *pcie,
				       int index, int type, u64 cpu_addr,
				       u64 pci_addr, u64 size)
{
	u32 retries = 0;
	u32 val = 0;
	u64 limit_addr = 0;
	u32 func = 0;

	//if (pci->ops && pci->ops->cpu_addr_fixup)
	//	cpu_addr = pci->ops->cpu_addr_fixup(pci, cpu_addr);

	limit_addr = cpu_addr + size - 1;

	if ((limit_addr & ~pcie->region_limit) != (cpu_addr & ~pcie->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pcie->region_align) ||
	    !IS_ALIGNED(pci_addr, pcie->region_align) || !size) {
		return -EINVAL;
	}

	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(cpu_addr));
	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(cpu_addr));

	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie_ver_is_ge(pcie, 460A))
		sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(pci_addr));
	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(pci_addr));

	val = type | PCIE_ATU_FUNC_NUM(func);
	if (upper_32_bits(limit_addr) > upper_32_bits(cpu_addr) &&
	    dw_pcie_ver_is_ge(pcie, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	if (dw_pcie_ver_is(pcie, 490A))
		val |= PCIE_ATU_TD;
	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_REGION_CTRL1, val);

	sophgo_dw_pcie_writel_atu_ob(pcie, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = sophgo_dw_pcie_readl_atu_ob(pcie, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pcie->dev, "Outbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

static int sophgo_dw_pcie_prog_inbound_atu(struct sophgo_dw_pcie *pcie, int index, int type,
			     u64 cpu_addr, u64 pci_addr, u64 size)
{
	u64 limit_addr = pci_addr + size - 1;
	u32 retries = 0;
	u32 val = 0;

	if ((limit_addr & ~pcie->region_limit) != (pci_addr & ~pcie->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pcie->region_align) ||
	    !IS_ALIGNED(pci_addr, pcie->region_align) || !size) {
		return -EINVAL;
	}

	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(pci_addr));
	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(pci_addr));

	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie_ver_is_ge(pcie, 460A))
		sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(cpu_addr));
	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(cpu_addr));

	val = type;
	if (upper_32_bits(limit_addr) > upper_32_bits(pci_addr) &&
	    dw_pcie_ver_is_ge(pcie, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_REGION_CTRL1, val);
	sophgo_dw_pcie_writel_atu_ib(pcie, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = sophgo_dw_pcie_readl_atu_ib(pcie, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pcie->dev, "Inbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}
static void __iomem *sophgo_dw_pcie_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);

	if (!acpi_disabled) {
		struct pci_config_window *cfg = bus->sysdata;
		pp = cfg->priv;
		pcie = to_sophgo_dw_pcie_from_pp(pp);
	}

	int type = 0;
	int ret = 0;
	u32 busdev = 0;

	/*
	 * Checking whether the link is up here is a last line of defense
	 * against platforms that forward errors on the system bus as
	 * SError upon PCI configuration transactions issued when the link
	 * is down. This check is racy by definition and does not stop
	 * the system from triggering an SError if the link goes down
	 * after this check is performed.
	 */
	if (!sophgo_dw_pcie_link_up(pcie))
		return NULL;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	ret = sophgo_dw_pcie_prog_outbound_atu(pcie, 0, type, pp->cfg0_base, busdev,
					pp->cfg0_size);
	if (ret)
		return NULL;

	return pp->va_cfg0_base + where;
}

static int sophgo_pci_generic_config_read(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 *val)
{
	void __iomem *addr;
	int align_where = where & (~0x3);
	uint32_t val_mask = (1UL << size * 8) - 1;
	uint32_t val_shift = (where - align_where) * 8;
	uint32_t read_val;

	addr = bus->ops->map_bus(bus, devfn, align_where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	read_val = readl(addr);
	*val = (read_val >> val_shift) & val_mask;

	if (align_where != where || size != 4) {
		pr_debug("read:where:0x%x, align_where:0x%x, size:0x%x, val:0x%x, return val:0x%x\n",
		where, align_where, size, read_val, *val);
	}

	return PCIBIOS_SUCCESSFUL;
}

static int sophgo_pci_generic_config_write(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 val)
{
	void __iomem *addr;
	int align_where = where & (~0x3);
	uint32_t read_val;
	uint32_t val_shift = (where - align_where) * 8;
	uint32_t val_mask = ((1UL << size * 8) - 1) << val_shift;
	uint32_t old_val;

	addr = bus->ops->map_bus(bus, devfn, align_where);
	if (!addr)
	return PCIBIOS_DEVICE_NOT_FOUND;

	read_val = readl(addr);
	old_val = read_val;
	read_val = (read_val & ~val_mask) | ((val << val_shift) & val_mask);
	writel(read_val, addr);

	if (align_where != where || size != 4) {
		pr_debug(" write: where:0x%x, align_where:0x%x, size:0x%x, old_val:0x%x, val:0x%x, new_val:0x%x\n",
		where, align_where, size, old_val, val, read_val);
	}

	return PCIBIOS_SUCCESSFUL;
}

static int sophgo_dw_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	int ret = 0;

	if (!acpi_disabled){
		struct pci_config_window *cfg = bus->sysdata;
		pp = cfg->priv;
		pcie = to_sophgo_dw_pcie_from_pp(pp);
	}

	ret = sophgo_pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = sophgo_dw_pcie_prog_outbound_atu(pcie, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int sophgo_dw_pcie_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	int ret = 0;

	if (!acpi_disabled){
		struct pci_config_window *cfg = bus->sysdata;
		pp = cfg->priv;
		pcie = to_sophgo_dw_pcie_from_pp(pp);
	}

	ret = sophgo_pci_generic_config_write(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = sophgo_dw_pcie_prog_outbound_atu(pcie, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static void __iomem *sophgo_dw_pcie_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);

	if (!acpi_disabled){
		struct pci_config_window *cfg = bus->sysdata;
		pp = cfg->priv;
		pcie = to_sophgo_dw_pcie_from_pp(pp);
	}

	if (pci_is_root_bus(bus)) {
		if (PCI_SLOT(devfn) > 0)
			return NULL;
		return pcie->dbi_base + where;
	}

	return sophgo_dw_pcie_other_conf_map_bus(bus, devfn, where);
}

static struct pci_ops sophgo_dw_child_pcie_ops = {
	.map_bus = sophgo_dw_pcie_other_conf_map_bus,
	.read = sophgo_dw_pcie_rd_other_conf,
	.write = sophgo_dw_pcie_wr_other_conf,
};

static struct pci_ops sophgo_dw_pcie_ops = {
	.map_bus = sophgo_dw_pcie_own_conf_map_bus,
	.read = sophgo_pci_generic_config_read,
	.write = sophgo_pci_generic_config_write,
};

static int sophgo_dw_pcie_get_resources(struct sophgo_dw_pcie *pcie)
{
	struct platform_device *pdev = to_platform_device(pcie->dev);
	struct device *dev = pcie->dev;
	struct device_node *np = dev_of_node(pcie->dev);
	struct resource *res;
	uint64_t start_addr;
	uint64_t size;
	int ret;

	if (device_property_present(dev, "pcie-card"))
		pcie->pcie_card = 1;

	if (!pcie->dbi_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
		pcie->dbi_base = devm_pci_remap_cfg_resource(pcie->dev, res);
		if (IS_ERR(pcie->dbi_base))
			return PTR_ERR(pcie->dbi_base);
	}

	if (!pcie->ctrl_reg_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ctrl_base");
		pcie->ctrl_reg_base = devm_pci_remap_cfg_resource(pcie->dev, res);
		if (IS_ERR(pcie->ctrl_reg_base))
			return PTR_ERR(pcie->ctrl_reg_base);
		pcie->ctrl_reg_base += 0xc00;
	}

	/* For non-unrolled iATU/eDMA platforms this range will be ignored */
	if (!pcie->atu_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
		if (res) {
			pcie->atu_size = resource_size(res);
			pcie->atu_base = devm_ioremap_resource(pcie->dev, res);
			if (IS_ERR(pcie->atu_base))
				return PTR_ERR(pcie->atu_base);
		} else {
			pcie->atu_base = pcie->dbi_base + DEFAULT_DBI_ATU_OFFSET;
		}
	}

	/* Set a default value suitable for at most 8 in and 8 out windows */
	if (!pcie->atu_size)
		pcie->atu_size = SZ_4K;
#if 0
	/* LLDD is supposed to manually switch the clocks and resets state */
	if (dw_pcie_cap_is(pcie, REQ_RES)) {
		ret = dw_pcie_get_clocks(pcie);
		if (ret)
			return ret;

		ret = dw_pcie_get_resets(pcie);
		if (ret)
			return ret;
	}
#endif

	if (pcie->link_gen < 1) {
		pcie->link_gen = of_pci_get_max_link_speed(np);
		pcie->link_gen += 1;
	}

	of_property_read_u32(np, "num-lanes", &pcie->num_lanes);

	if (of_property_read_bool(np, "snps,enable-cdm-check"))
		dw_pcie_cap_set(pcie, CDM_CHECK);

	if (pcie->pcie_card) {
		if (!pcie->sii_reg_base)
			pcie->sii_reg_base = pcie->ctrl_reg_base - 0x800;

		if (!pcie->c2c_top) {
			ret = of_property_read_u64_index(np, "c2c_top", 0, &start_addr);
			ret = of_property_read_u64_index(np, "c2c_top", 1, &size);
			if (ret) {
				dev_err(dev, "no c2c top find\n");
			} else {
				pcie->c2c_top = devm_ioremap(dev, start_addr, size);
				if (!pcie->c2c_top) {
					pr_err("c2c top base ioremap failed\n");
					return PTR_ERR(pcie->c2c_top);
				}
			}
		}

		ret = of_property_read_u64_index(np, "cfg_range", 0, &pcie->cfg_start_addr);
		ret = of_property_read_u64_index(np, "cfg_range", 1, &pcie->cfg_end_addr);
		if (ret == 0)
			dev_err(dev, "cfg[0x%llx-0x%llx]\n", pcie->cfg_start_addr, pcie->cfg_end_addr);

		ret = of_property_read_u64_index(np, "slv_range", 0, &pcie->slv_start_addr);
		ret = of_property_read_u64_index(np, "slv_range", 1, &pcie->slv_end_addr);
		if (ret == 0)
			dev_err(dev, "slv[0x%llx-0x%llx]\n", pcie->slv_start_addr, pcie->slv_end_addr);

		ret = of_property_read_u64_index(np, "dw_range", 0, &pcie->dw_start);
		ret = of_property_read_u64_index(np, "dw_range", 1, &pcie->dw_end);
		if (ret == 0)
			dev_err(dev, "dw[0x%llx-0x%llx]\n", pcie->dw_start, pcie->dw_end);
		else {
			pcie->dw_start = 0;
			pcie->dw_end = 0;
		}

		pcie->phy = devm_of_phy_get(dev, dev->of_node, "pcie-phy");

		pcie->pe_rst = of_get_named_gpio(dev->of_node, "prst", 0); //TODO:default high? or low?
		dev_err(dev, "perst:[gpio%d]\n", pcie->pe_rst);

		if (device_property_present(dev, "c2c0_x8_1") || device_property_present(dev, "c2c1_x8_1"))
			pcie->pcie_route_config = C2C_PCIE_X8_1;
		else if (device_property_present(dev, "c2c0_x8_0") || device_property_present(dev, "c2c1_x8_0"))
			pcie->pcie_route_config = C2C_PCIE_X8_0;
		else if (device_property_present(dev, "c2c0_x4_1") || device_property_present(dev, "c2c1_x4_1"))
			pcie->pcie_route_config = C2C_PCIE_X4_1;
		else if (device_property_present(dev, "c2c0_x4_0") || device_property_present(dev, "c2c1_x4_0"))
			pcie->pcie_route_config = C2C_PCIE_X4_0;
		else if (device_property_present(dev, "cxp_x8"))
			pcie->pcie_route_config = CXP_PCIE_X8;
		else if (device_property_present(dev, "cxp_x4"))
			pcie->pcie_route_config = CXP_PCIE_X4;
		else
			dev_err(dev, "error pcie type\n");

		ret = of_property_read_u64_index(np, "cdma-reg", 0, &pcie->cdma_pa_start);
		ret = of_property_read_u64_index(np, "cdma-reg", 1, &pcie->cdma_size);
		if (ret) {
			pr_err("cdma reg not found\n");
			return -1;
		}

		ret = of_property_read_u32_index(np, "dst-chip", 0, &pcie->dst_chipid);
		if (ret < 0)
			dev_err(dev, "cannot get dst chipid from dtb\n");

		ret = of_property_read_u64_index(np, "global_chipid", 0, &pcie->global_chipid);
		if (ret < 0)
			dev_err(dev, "cannot get global chipid from dtb\n");

		ret = of_property_read_u64_index(np, "board_size", 0, &pcie->board_size);
		if (ret < 0 || pcie->board_size == 0) {
			dev_err(dev, "error board size:0x%llx\n", pcie->board_size);
			return -1;
		}

		ret = of_property_read_u64_index(np, "socket_id", 0, &pcie->socket_id);
		if (ret < 0)
			dev_err(dev, "cannot get socket id from dtb\n");

		pcie->board_id = pcie->global_chipid / pcie->board_size;
		if (pcie->socket_id != pcie->global_chipid % pcie->board_size) {
			dev_err(dev, "socket id error\n");
			return -1;
		}

		dev_err(dev, "board id:0x%llx, chip id:0x%llx, board size:0x%llx\n", pcie->board_id,
			 pcie->socket_id, pcie->board_size);

		pcie->cdma_reg_base = devm_ioremap(dev, pcie->cdma_pa_start, pcie->cdma_size);
		if (!pcie->cdma_reg_base) {
			dev_err(dev, "failed to map cdma reg\n");
			return -1;
		}

		if (of_device_is_compatible(np, "sophgo,bm1690-c2c-pcie-host")) {
			pcie->c2c_pcie_rc = 1;
			dev_err(dev, "probe c2c pcie host\n");
		} else if (of_device_is_compatible(np, "sophgo,bm1690-pcie-host")) {
			pcie->c2c_pcie_rc = 0;
			pcie->chip_type = CHIP_BM1690;
			pcie->dst_chipid_shift = 57;
			pcie->func_num_shift = 54;
		} else if (of_device_is_compatible(np, "sophgo,bm1690e-pcie-host")) {
			pcie->c2c_pcie_rc = 0;
			pcie->chip_type = CHIP_BM1690E;
			pcie->board_id_shift = 52;
			pcie->dst_chipid_shift = 49;
			pcie->func_num_shift = 45;
		} else {
			pr_err("error compatible\n");
			return -1;
		}
	}

	return 0;
}

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
static int sophgo_dw_pcie_get_resources_acpi(struct sophgo_dw_pcie *pcie, struct resource *res)
{
	struct device_node *np = dev_of_node(pcie->dev);

	pcie->dbi_base = devm_pci_remap_cfg_resource(pcie->dev, &res[0]);
	if (IS_ERR(pcie->dbi_base))
		return PTR_ERR(pcie->dbi_base);

	pcie->ctrl_reg_base = devm_pci_remap_cfg_resource(pcie->dev, &res[1]);
	if (IS_ERR(pcie->ctrl_reg_base))
		return PTR_ERR(pcie->ctrl_reg_base);

	pcie->atu_size = resource_size(&res[2]);
	pcie->atu_base = devm_ioremap_resource(pcie->dev, &res[2]);
	if (IS_ERR(pcie->atu_base))
		return PTR_ERR(pcie->atu_base);

	/* Set a default value suitable for at most 8 in and 8 out windows */
	if (!pcie->atu_size)
		pcie->atu_size = SZ_4K;

	if (pcie->link_gen < 1) {
		pcie->link_gen = of_pci_get_max_link_speed(np);
		pcie->link_gen += 1;
	}

	return 0;
}
#endif

static int sophgo_dw_pcie_setup_outbound_atu(struct sophgo_dw_pcie *pcie,
						struct list_head *list)
{
	struct resource_entry *entry;
	int i, ret = 0;

	resource_list_for_each_entry(entry, list) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pcie->num_ob_windows <= ++i)
			break;

		ret = sophgo_dw_pcie_prog_outbound_atu(pcie, i, PCIE_ATU_TYPE_MEM,
				entry->res->start,
				entry->res->start - entry->offset,
				resource_size(entry->res));
		if (ret) {
			dev_err(pcie->dev, "Failed to set MEM range %pr\n", entry->res);
			return ret;
		}
	}

	return 0;
}

static int sophgo_dw_pcie_setup_inbound_atu(struct sophgo_dw_pcie *pcie,
					struct list_head *list_dma)
{
	struct resource_entry *entry;
	int i, ret = 0;
	struct bus_dma_region *r, **map;
	u64 end, mask;
	struct device *dev = pcie->dev;

	map = &r;
	r = kcalloc(pcie->num_ib_windows + 1, sizeof(*r), GFP_KERNEL);
	if (!r) {
		return -ENOMEM;
	}

	resource_list_for_each_entry(entry, list_dma) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pcie->num_ib_windows <= i)
			break;

		ret = sophgo_dw_pcie_prog_inbound_atu(pcie, i++, PCIE_ATU_TYPE_MEM,
						entry->res->start,
						entry->res->start - entry->offset,
						resource_size(entry->res));
		if (ret) {
			dev_err(pcie->dev, "Failed to set DMA range %pr\n", entry->res);
			kfree(r);
			return ret;
		}

		r->cpu_start = entry->res->start;
		r->dma_start = entry->res->start - entry->offset;
		r->size = resource_size(entry->res);
		r++;

		end = dma_range_map_max(*map);
		mask = DMA_BIT_MASK(ilog2(end) + 1);
		dev->bus_dma_limit = end;
		dev->coherent_dma_mask &= mask;
		if (dev->dma_mask) {
			*dev->dma_mask &= mask;
		}
	}

	return 0;
}

static int sophgo_dw_pcie_iatu_setup(struct dw_pcie_rp *pp)
{
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	struct list_head *list, *list_dma;
	int i, ret = 0;

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
	if (!acpi_disabled) {
		struct acpi_device *adev = to_acpi_device(pcie->dev);
		unsigned long flags;
		struct list_head resource_list;

		INIT_LIST_HEAD(&resource_list);
		flags = IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_BUS;
		ret = acpi_dev_get_resources(adev, &resource_list, acpi_dev_filter_resource_type_cb, (void *)flags);
		if (ret < 0) {
			pr_err("failed to parse _CRS method, error code %d\n", ret);
			return ret;
		} else if (ret == 0) {
			pr_err("no IO and memory resources present in _CRS\n");
			return -EINVAL;
		}
		list = &resource_list;

	} else
		list = &pp->bridge->windows;
#else
	list = &pp->bridge->windows;
#endif
	/* Note the very first outbound ATU is used for CFG IOs */
	if (!pcie->num_ob_windows) {
		dev_err(pcie->dev, "No outbound iATU found\n");
		return -EINVAL;
	}

	/*
	 * Ensure all out/inbound windows are disabled before proceeding with
	 * the MEM/IO (dma-)ranges setups.
	 */
	for (i = 0; i < pcie->num_ob_windows; i++)
		sophgo_dw_pcie_disable_atu(pcie, PCIE_ATU_REGION_DIR_OB, i);

	for (i = 0; i < pcie->num_ib_windows; i++)
		sophgo_dw_pcie_disable_atu(pcie, PCIE_ATU_REGION_DIR_IB, i);

	/* Setup outbound ATU */
	ret = sophgo_dw_pcie_setup_outbound_atu(pcie, list);
	if (ret)
		return ret;

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
	/* Setup inbound ATU */
	if (!acpi_disabled) {
		struct acpi_device *adev = to_acpi_device(pcie->dev);
		unsigned long flags;
		struct list_head resource_dma_list;

		INIT_LIST_HEAD(&resource_dma_list);
		flags = IORESOURCE_DMA;
		ret = acpi_dev_get_dma_resources(adev, &resource_dma_list);
		if (ret < 0) {
			pr_err("failed to parse _DMA method, error code %d\n", ret);
			return ret;
		} else if (ret == 0) {
			pr_err("no memory resources present in _DMA\n");
			return -EINVAL;
		}

		list_dma = &resource_dma_list;
	} else
		list_dma = &pp->bridge->dma_ranges;
#else
	list_dma = &pp->bridge->dma_ranges;
#endif

	ret = sophgo_dw_pcie_setup_inbound_atu(pcie, list_dma);
	if (ret)
		return ret;

	return 0;
}

static int sophgo_dw_pcie_setup_rc(struct dw_pcie_rp *pp)
{
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	struct device *dev = pcie->dev;
	u32 val = 0;
	u32 ctrl = 0;
	u32 num_ctrls = 0;
	int ret = 0;

	/*
	 * Enable DBI read-only registers for writing/updating configuration.
	 * Write permission gets disabled towards the end of this function.
	 */
	sophgo_dw_pcie_dbi_ro_wr_en(pcie);

	if (pci_msi_enabled()) {
		pp->has_msi_ctrl = !(of_property_read_bool(dev->of_node, "msi-parent"));
		if (pp->has_msi_ctrl) {
			num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

			/* Initialize IRQ Status array */
			for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
				sophgo_dw_pcie_writel_dbi(pcie, PCIE_MSI_INTR0_MASK +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    pp->irq_mask[ctrl]);
				sophgo_dw_pcie_writel_dbi(pcie, PCIE_MSI_INTR0_ENABLE +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    ~0);
			}
		}
	}

	ret = sophgo_dw_pcie_msi_setup(pp);
	if (ret)
		return ret;

	/* Setup RC BARs */
	//sophgo_dw_pcie_writel_dbi(pcie, PCI_BASE_ADDRESS_0, 0x00000004);
	//sophgo_dw_pcie_writel_dbi(pcie, PCI_BASE_ADDRESS_1, 0x00000000);

	/* Setup interrupt pins */
	//val = sophgo_dw_pcie_readl_dbi(pcie, PCI_INTERRUPT_LINE);
	//val &= 0xffff00ff;
	//val |= 0x00000100;
	//sophgo_dw_pcie_writel_dbi(pcie, PCI_INTERRUPT_LINE, val);

	/* Setup bus numbers */
	val = sophgo_dw_pcie_readl_dbi(pcie, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00ff0100;
	sophgo_dw_pcie_writel_dbi(pcie, PCI_PRIMARY_BUS, val);

	/* Setup command register */
	val = sophgo_dw_pcie_readl_dbi(pcie, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	sophgo_dw_pcie_writel_dbi(pcie, PCI_COMMAND, val);

	/*
	 * If the platform provides its own child bus config accesses, it means
	 * the platform uses its own address translation component rather than
	 * ATU, so we should not program the ATU here.
	 */
	if (!acpi_disabled) {
		ret = sophgo_dw_pcie_iatu_setup(pp);
		if (ret)
			return ret;
	} else if (pp->bridge->child_ops == &sophgo_dw_child_pcie_ops) {
		ret = sophgo_dw_pcie_iatu_setup(pp);
		if (ret)
			return ret;
	}

	//sophgo_dw_pcie_writel_dbi(pcie, PCI_BASE_ADDRESS_0, 0);

	/* Program correct class for RC */
	sophgo_dw_pcie_writew_dbi(pcie, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	//val = sophgo_dw_pcie_readl_dbi(pcie, PCIE_LINK_WIDTH_SPEED_CONTROL);
	//val |= PORT_LOGIC_SPEED_CHANGE;
	//sophgo_dw_pcie_writel_dbi(pcie, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	sophgo_dw_pcie_dbi_ro_wr_dis(pcie);

	return 0;
}

static void sophgo_dw_pcie_version_detect(struct sophgo_dw_pcie *pcie)
{
	u32 ver = 0;

	/* The content of the CSR is zero on DWC PCIe older than v4.70a */
	ver = sophgo_dw_pcie_readl_dbi(pcie, PCIE_VERSION_NUMBER);
	if (!ver)
		return;

	if (pcie->version && pcie->version != ver)
		dev_warn(pcie->dev, "Versions don't match (%08x != %08x)\n",
			 pcie->version, ver);
	else
		pcie->version = ver;

	ver = sophgo_dw_pcie_readl_dbi(pcie, PCIE_VERSION_TYPE);

	if (pcie->type && pcie->type != ver)
		dev_warn(pcie->dev, "Types don't match (%08x != %08x)\n",
			 pcie->type, ver);
	else
		pcie->type = ver;
}

static void sophgo_dw_pcie_iatu_detect(struct sophgo_dw_pcie *pcie)
{
	int max_region = 0;
	int ob = 0;
	int ib = 0;
	u32 val = 0;
	u32 min = 0;
	u32 dir = 0;
	u64 max = 0;

	val = sophgo_dw_pcie_readl_dbi(pcie, PCIE_ATU_VIEWPORT);
	if (val == 0xFFFFFFFF) {
		dw_pcie_cap_set(pcie, IATU_UNROLL);

		max_region = min((int)pcie->atu_size / 512, 256);
	} else {
		pcie->atu_base = pcie->dbi_base + PCIE_ATU_VIEWPORT_BASE;
		pcie->atu_size = PCIE_ATU_VIEWPORT_SIZE;

		sophgo_dw_pcie_writel_dbi(pcie, PCIE_ATU_VIEWPORT, 0xFF);
		max_region = sophgo_dw_pcie_readl_dbi(pcie, PCIE_ATU_VIEWPORT) + 1;
	}

	for (ob = 0; ob < max_region; ob++) {
		sophgo_dw_pcie_writel_atu_ob(pcie, ob, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = sophgo_dw_pcie_readl_atu_ob(pcie, ob, PCIE_ATU_LOWER_TARGET);
		if (val != 0x11110000)
			break;
	}

	for (ib = 0; ib < max_region; ib++) {
		sophgo_dw_pcie_writel_atu_ib(pcie, ib, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = sophgo_dw_pcie_readl_atu_ib(pcie, ib, PCIE_ATU_LOWER_TARGET);
		if (val != 0x11110000)
			break;
	}

	if (ob) {
		dir = PCIE_ATU_REGION_DIR_OB;
	} else if (ib) {
		dir = PCIE_ATU_REGION_DIR_IB;
	} else {
		dev_err(pcie->dev, "No iATU regions found\n");
		return;
	}

	sophgo_dw_pcie_writel_atu(pcie, dir, 0, PCIE_ATU_LIMIT, 0x0);
	min = sophgo_dw_pcie_readl_atu(pcie, dir, 0, PCIE_ATU_LIMIT);

	if (dw_pcie_ver_is_ge(pcie, 460A)) {
		sophgo_dw_pcie_writel_atu(pcie, dir, 0, PCIE_ATU_UPPER_LIMIT, 0xFFFFFFFF);
		max = sophgo_dw_pcie_readl_atu(pcie, dir, 0, PCIE_ATU_UPPER_LIMIT);
	} else {
		max = 0;
	}

	pcie->num_ob_windows = ob;
	pcie->num_ib_windows = ib;
	pcie->region_align = 1 << fls(min);
	pcie->region_limit = (max << 32) | (SZ_4G - 1);

	dev_info(pcie->dev, "iATU: unroll %s, %u ob, %u ib, align %uK, limit %lluG\n",
		 dw_pcie_cap_is(pcie, IATU_UNROLL) ? "T" : "F",
		 pcie->num_ob_windows, pcie->num_ib_windows,
		 pcie->region_align / SZ_1K, (pcie->region_limit + 1) / SZ_1G);
}

static int pcie_config_eq(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	uint32_t speed = 0;
	uint32_t pset_id = 0;
	void __iomem *pcie_dbi_base = pcie->dbi_base;
	struct PCIE_EQ_COEF eq_coef_tbl[11] = { //p0 ~ p10
		{36, 0, 12}, {40, 0, 8}, {38, 0, 10}, {42, 0, 6}, {48, 0, 0}, {44, 4, 0},
		{42, 6,  0}, {34, 5, 9}, {36, 6,  6}, {40, 8, 0}, {32, 0, 16}
	};

	for (speed = 0; speed < 3; speed++) {
		val = readl(pcie_dbi_base + 0x890); //set speed
		val &= 0xfcffffff;
		val |= (speed << 24);
		writel(val, (pcie_dbi_base + 0x890));

		val = readl(pcie_dbi_base + 0x894);
		val &= 0xfffff000; //bit[11, 0]
		val |= 16;
		val |= (48 << 6);
		writel(val, (pcie_dbi_base + 0x894));

		for (pset_id = 0; pset_id < 11; pset_id++) {
			val = readl(pcie_dbi_base + 0x89c);
			val &= 0xfffffff0; //bit[3, 0]
			val |= pset_id;
			writel(val, (pcie_dbi_base + 0x89c));

			val = readl(pcie_dbi_base + 0x898);
			val &= 0xfffc0000; //bit[17, 0]
			val |= eq_coef_tbl[pset_id].pre_cursor;
			val |= (eq_coef_tbl[pset_id].cursor << 6);
			val |= (eq_coef_tbl[pset_id].post_cursor << 12);
			writel(val, (pcie_dbi_base + 0x898));

			val = readl(pcie_dbi_base + 0x8a4);
			if (val & 0x1) //bit0
				pr_info("illegal coef pragrammed, speed[%d], pset[%d].\n", speed, pset_id);
		}
	}

	return 0;
}

static int pcie_config_link(struct sophgo_dw_pcie *pcie)
{
	void __iomem *dbi_base = pcie->dbi_base;
	uint32_t val;

	//config lane_count
	val = readl(dbi_base + 0x8c0);
	val = (val & 0xffffffc0) | pcie->num_lanes;
	writel(val, (dbi_base + 0x8c0));

	//config eq bypass highest rate disable
	val = readl(dbi_base + 0x1c0);
	val |= 0x1;
	writel(val, (dbi_base + 0x1c0));

	return 0;
}

static int pcie_enable_ltssm(struct sophgo_dw_pcie *pcie)
{
	void __iomem *sii_reg_base = pcie->sii_reg_base;
	uint32_t val;

	val = readl(sii_reg_base + PCIE_SII_GENERAL_CTRL3_REG);
	val |= 0x1;
	writel(val, sii_reg_base + PCIE_SII_GENERAL_CTRL3_REG);

	return 0;
}

static int pcie_wait_link(struct sophgo_dw_pcie *pcie)
{
	void __iomem *sii_reg_base = pcie->sii_reg_base;
	uint32_t status;
	int err;
	int timeout = 500;

	err = readl_poll_timeout(sii_reg_base + 0xb4, status, ((status >> 6) & 0x3), 20, timeout * USEC_PER_MSEC);
	if (err) {
		pr_err("[sg2260] failed to poll link ready\n");
		return -ETIMEDOUT;
	}

	pr_err("link status reg: 0x%x\n", readl(sii_reg_base + 0xb4));
	return 0;
}

static int pcie_config_ctrl(struct sophgo_dw_pcie *pcie)
{
	void __iomem *dbi_reg_base = pcie->dbi_base;
	void __iomem *sii_reg_base = pcie->sii_reg_base;
	uint32_t val;

	//config device_type
	val = readl(sii_reg_base + PCIE_SII_GENERAL_CTRL1_REG);
	val &= (~PCIE_SII_GENERAL_CTRL1_DEVICE_TYPE_MASK);
	val |= (4 << 9);//RC MODE
	writel(val, (sii_reg_base + PCIE_SII_GENERAL_CTRL1_REG));

	//config Directed Speed Change	Writing '1' to this field instructs the LTSSM to initiate
	//a speed change to Gen2 or Gen3 after the link is initialized at Gen1 speed
	val = readl(dbi_reg_base + 0x80c);
	val = val | 0x20000;
	writel(val, (dbi_reg_base + 0x80c));

	//config generation_select-pcie_cap_target_link_speed
	val = readl(dbi_reg_base + 0xa0);
	val = (val & 0xfffffff0) | pcie->link_gen;
	writel(val, (dbi_reg_base + 0xa0));

	// config ecrc generation enable
	val = readl(dbi_reg_base + 0x118);
	val = 0x3e0;
	writel(val, (dbi_reg_base + 0x118));

	return 0;
}

static int pcie_check_link_status(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	uint32_t speed = 0;
	uint32_t width = 0;
	uint32_t ltssm_state = 0;
	void __iomem *pcie_sii_base = pcie->sii_reg_base;
	void __iomem *pcie_dbi_base = pcie->dbi_base;

	val = readl(pcie_sii_base + 0xb4); //LNK_DBG_2
	ltssm_state = val & 0x3f; //bit[5,0]
	if (ltssm_state != 0x11)
		pr_err("PCIe link fail, ltssm_state = 0x%x\n", ltssm_state);

	speed = (val >> 8) & 0x7; //bit[10,8]
	if ((speed + 1) != pcie->link_gen)
		pr_err("link speed, expect gen%d, current gen%d\n", pcie->link_gen, (speed + 1));

	val = readl(pcie_dbi_base + 0x80);
	width = (val >> 20) & 0x3f; //bit[25:20]
	if (width != pcie->num_lanes)
		pr_err("link width, expect x%d, current x%d\n", pcie->num_lanes, width);

	pr_info("PCIe Link status, ltssm[0x%x], gen%d, x%d.\n", ltssm_state, (speed + 1), width);

	return 0;
}

static int pcie_config_soft_phy_reset(struct sophgo_dw_pcie *pcie, uint32_t rst_status)
{
	uint32_t val = 0;
	void __iomem *reg_base;

	//deassert = 1; assert = 0;
	if ((rst_status != 0) && (rst_status != 1))
		return -1;

	reg_base = pcie->ctrl_reg_base;

	//cfg soft_phy_rst_n , first cfg 1
	val = readl(reg_base + PCIE_CTRL_SFT_RST_SIG_REG);
	if (rst_status == 1)
		val |= (0x1 << PCIE_CTRL_SFT_RST_SIG_PHY_RSTN_BIT);
	else
		val &= (~PCIE_CTRL_SFT_RST_SIG_PHY_RSTN_BIT);

	writel(val, (reg_base + PCIE_CTRL_SFT_RST_SIG_REG));

	udelay(1);

	return 0;
}

static int pcie_config_soft_cold_reset(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	void __iomem  *reg_base;


	reg_base = pcie->ctrl_reg_base;

	//cfg soft_cold_rst_n , first cfg 0
	val = readl(reg_base + PCIE_CTRL_SFT_RST_SIG_REG);
	val &= (~PCIE_CTRL_SFT_RST_SIG_COLD_RSTN_BIT);
	writel(val, (reg_base + PCIE_CTRL_SFT_RST_SIG_REG));

	//cfg soft_cold_rst_n , second cfg 1
	val = readl(reg_base + PCIE_CTRL_SFT_RST_SIG_REG);
	val |= (0x1 << PCIE_CTRL_SFT_RST_SIG_COLD_RSTN_BIT);
	writel(val, (reg_base + PCIE_CTRL_SFT_RST_SIG_REG));

	return 0;
}

static void pcie_check_radm_status(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	void __iomem *base_addr = pcie->ctrl_reg_base;
	int timeout = 0;

	do {
		udelay(30);
		if (pcie->num_lanes == 8) {
			val = readl(base_addr + 0xfc);
			val = (val >> 29) & 0x1; //bit29, radm_idle
		} else {
			val = readl(base_addr + 0xe8);
			val = (val >> 21) & 0x1; //bit21, radm_idle
		}
		timeout++;
		if (timeout == 200) {
			pr_err("failed check radm status\n");
			return;
		}
	} while (val != 1);
}

static void pcie_clear_slv_mapping(struct sophgo_dw_pcie *pcie)
{
	void __iomem  *ctrl_reg_base = pcie->ctrl_reg_base;

	writel(0x0, (ctrl_reg_base + PCIE_CTRL_REMAPPING_EN_REG));
	writel(0x0, (ctrl_reg_base + PCIE_CTRL_SN_UP_START_ADDR_REG));
	writel(0x0, (ctrl_reg_base + PCIE_CTRL_SN_UP_END_ADDR_REG));
	writel(0x0, (ctrl_reg_base + PCIE_CTRL_SN_DW_ADDR_REG));
}

static void pcie_config_slv_mapping(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	void __iomem  *ctrl_reg_base = pcie->ctrl_reg_base;
	uint64_t up_start_addr = 0;
	uint64_t up_end_addr = 0;
	uint32_t dw_start_addr = 0;
	uint32_t dw_end_addr = 0;
	uint32_t full_addr = 0;

	up_start_addr = pcie->slv_start_addr;
	up_end_addr = pcie->slv_end_addr;

	dw_start_addr = pcie->dw_start;
	dw_end_addr = pcie->dw_end;

	full_addr = (((dw_end_addr >> 16) & 0xffff) << 16) | ((dw_start_addr >> 16) & 0xffff);

	val = readl(ctrl_reg_base + PCIE_CTRL_REMAPPING_EN_REG);
	if (full_addr)
		val |= 0x3 << PCIE_CTRL_REMAP_EN_SN_TO_PCIE_UP4G_EN_BIT;
	else
		val |= 0x1 << PCIE_CTRL_REMAP_EN_SN_TO_PCIE_UP4G_EN_BIT;
	writel(val, (ctrl_reg_base + PCIE_CTRL_REMAPPING_EN_REG));
	up_start_addr = up_start_addr >> 16;
	writel((up_start_addr & 0xffffffff), (ctrl_reg_base + PCIE_CTRL_SN_UP_START_ADDR_REG));
	up_end_addr = up_end_addr >> 16;
	writel((up_end_addr & 0xffffffff), (ctrl_reg_base + PCIE_CTRL_SN_UP_END_ADDR_REG));
	// cfg sn_to_pcie_dw4g_start_addr and end addr
	writel(full_addr, (ctrl_reg_base + PCIE_CTRL_SN_DW_ADDR_REG));
}

static void pcie_config_mps(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	int mps = 1;
	void __iomem *base_addr = pcie->dbi_base;

	val = readl(base_addr + 0x78);
	val &= 0xffffff1f;
	val |= ((mps & 0x7) << 5);
	writel(val, (base_addr + 0x78));
}

static void pcie_config_mrrs(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	void __iomem  *base_addr = pcie->dbi_base;
	int mrrs = 1;

	val = readl(base_addr + 0x78);
	val &= 0xffff8fff;
	val |= ((mrrs & 0x7) << 12);
	writel(val, (base_addr + 0x78));
}

static void bm1690_pcie_init_route(struct sophgo_dw_pcie *pcie)
{
	pcie_clear_slv_mapping(pcie);
	pcie_config_slv_mapping(pcie);
	pcie_config_mps(pcie);
	pcie_config_mrrs(pcie);
}

static void pcie_config_axi_route(struct sophgo_dw_pcie *pcie)
{
	uint64_t cfg_start_addr;
	uint64_t cfg_end_addr;

	cfg_start_addr = pcie->cfg_start_addr;
	cfg_end_addr = pcie->cfg_end_addr;


	writel((cfg_start_addr & 0xffffffff), (pcie->c2c_top + 0x24));
	writel(((cfg_start_addr >> 32) & 0xffffffff), (pcie->c2c_top + 0x28));
	writel((cfg_end_addr & 0xffffffff), (pcie->c2c_top + 0x2c));
	writel(((cfg_end_addr >> 32) & 0xffffffff), (pcie->c2c_top + 0x30));
	//writel((C2C_PCIE_TOP_REG(c2c_id) + 0xcc), 0xf0000);
}

static int sophgo_pcie_host_init_port(struct sophgo_dw_pcie *pcie)
{
	int ret;
	int err;
	uint32_t val;
	void __iomem *sii_reg_base = pcie->sii_reg_base;
	int timeout = 0;

	phy_init(pcie->phy);
	pcie_config_soft_phy_reset(pcie, PCIE_RST_ASSERT);
	pcie_config_soft_phy_reset(pcie, PCIE_RST_DE_ASSERT);
	pcie_config_soft_cold_reset(pcie);

	gpio_direction_output(pcie->pe_rst, 0);
	gpio_set_value(pcie->pe_rst, 0);
	msleep(100);
	gpio_set_value(pcie->pe_rst, 1);
	ret = phy_configure(pcie->phy, NULL);
	if (ret) {
		dev_err(pcie->dev, "phy config failed\n");
		return ret;
	}

	/*pcie wait core clk*/
	do {
		val = readl(sii_reg_base + 0x5c); //GEN_CTRL_4
		val = (val >> 8) & 1;
		if (val == 1) {
			dev_err(pcie->dev, "phy config success\n");
			break;
		} else {
			timeout++;
			msleep(10);
		}

		if (timeout == 50) {
			dev_err(pcie->dev, "wait core clk failed\n");
			return -1;
		}

	} while (1);

	pcie_check_radm_status(pcie);

	pcie_config_ctrl(pcie);
	pcie_config_eq(pcie);
	pcie_config_link(pcie);
	pcie_enable_ltssm(pcie);

	err = pcie_wait_link(pcie);
	if (err) {
		pr_err("pcie wait link failed\n");
		return err;
	}

	pcie_check_link_status(pcie);
	pcie_config_axi_route(pcie);
	bm1690_pcie_init_route(pcie);

	return 0;
}

static int sophgo_pcie_config_cdma_route(struct sophgo_dw_pcie *pcie)
{
	uint32_t tmp;

	pr_err("cdma config, pcie route config:0x%x\n", pcie->pcie_route_config);

	tmp = (pcie->pcie_route_config << 28) | (pcie->cdma_pa_start >> 32);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_RCV_ADDR_H32);

	tmp = (pcie->cdma_pa_start & ((1ul << 32) - 1)) >> 16;
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_RCV_ADDR_M16);

	// OS: 2
	tmp = readl(pcie->cdma_reg_base + CDMA_CSR_4) | (1 << CDMA_CSR_RCV_CMD_OS);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_4);

	tmp = (readl(pcie->cdma_reg_base + CDMA_CSR_INTER_DIE_RW) &
		~(0xff << CDMA_CSR_INTER_DIE_WRITE_ADDR_L4)) |
		(pcie->pcie_route_config << CDMA_CSR_INTER_DIE_WRITE_ADDR_H4) |
		(0b0000 << CDMA_CSR_INTER_DIE_WRITE_ADDR_L4);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_INTER_DIE_RW);

	tmp = (readl(pcie->cdma_reg_base + CDMA_CSR_INTRA_DIE_RW) &
		~(0xff << CDMA_CSR_INTRA_DIE_READ_ADDR_L4)) |
		(AXI_RN << CDMA_CSR_INTRA_DIE_READ_ADDR_H4) |
		(0b0000 << CDMA_CSR_INTRA_DIE_READ_ADDR_L4);
	writel(tmp, pcie->cdma_reg_base + CDMA_CSR_INTRA_DIE_RW);

	return 0;
}

static int find_available_ob_atu(struct sophgo_dw_pcie *pcie)
{
	for (int i = 0; i < pcie->num_ob_windows; i++) {
		uint32_t val = sophgo_dw_pcie_readl_atu_ob(pcie, i, PCIE_ATU_REGION_CTRL2);
		if (!(val & PCIE_ATU_ENABLE))
			return i;
	}

	return -1;
}

static uint64_t get_atu_cpu_addr(struct sophgo_dw_pcie *pcie, int func_num, int barid)
{
	uint64_t atu_cpu_addr;
	uint64_t dst_chipid = pcie->dst_chipid + func_num;
	uint64_t base_barid = (dst_chipid - 1) * 3;
	uint64_t bar2addr[4] = {0x5, 0x6, 0x7, 0x0};

	if (pcie->chip_type == CHIP_BM1690) {
		atu_cpu_addr = (dst_chipid << pcie->dst_chipid_shift) | (bar2addr[barid] << pcie->func_num_shift);
	} else if (pcie->chip_type == CHIP_BM1690E) {
		atu_cpu_addr = (pcie->board_id << pcie->board_id_shift) | (dst_chipid << pcie->dst_chipid_shift) | ((base_barid + barid) << pcie->func_num_shift);
	} else {
		dev_err(pcie->dev, "%s Unknown chip type\n", __func__);
		return 0;
	}

	return atu_cpu_addr;
}

static int config_rc_atu(struct sophgo_dw_pcie *pcie, struct pci_host_bridge *bridge)
{
	struct pci_bus *bus = bridge->bus;
	struct list_head *child_pci_bus;
	struct pci_bus *child_bus;
	struct list_head *dev_list;
	struct pci_dev *dev;
	struct resource_entry *window;
	uint64_t cpu_addr;
	uint64_t pcie_addr;
	uint64_t atu_cpu_addr;
	uint64_t dst_chipid = pcie->dst_chipid;
	int bar_num[4] = {0, 1, 2, 4};
	int cfg_barid;
	int i = 0;
	uint64_t offset;
	int ret;
	int index;
	int func_num = 0;

	dev_err(bus->bridge, "bridge bus number:0x%x\n", bus->number);

	list_for_each(child_pci_bus, &bus->children) {
		child_bus = container_of(child_pci_bus, struct pci_bus, node);
		dev_err(child_bus->bridge, "child pci bus number:0x%x\n", child_bus->number);

		list_for_each(dev_list, &child_bus->devices) {
			dev = container_of(dev_list, struct pci_dev, bus_list);
			dev_err(&dev->dev, "devfn:0x%x, class:0x%x\n", dev->devfn, dev->class);

			cfg_barid = 0;
			for (i = 0; i < 4; i++) {
				if (bar_num[i] == 2)
					continue;

				cpu_addr = pci_resource_start(dev, bar_num[i]);
				dev_dbg(&dev->dev, "bar%d addr:0x%llx\n", bar_num[i], cpu_addr);

				resource_list_for_each_entry(window, &bridge->windows) {
					dev_dbg(&dev->dev, "windown start:0x%llx, end:0x%llx, offset:0x%llx\n", window->res->start,
							window->res->end, window->offset);
					if (window->res->start <= cpu_addr && ( window->res->end >= cpu_addr)) {
						offset = window->offset;
						dev_dbg(&dev->dev, "bar%d match window\n", bar_num[i]);
						break;
					}
				}
				pcie_addr = cpu_addr - offset;

				atu_cpu_addr = get_atu_cpu_addr(pcie, func_num, cfg_barid++);

				index = find_available_ob_atu(pcie);
				if (index < 0) {
					dev_err(pcie->dev, "No available OB ATU\n");
					return -1;
				}
				ret = sophgo_dw_pcie_prog_outbound_atu(pcie, index, PCIE_ATU_TYPE_MEM,
						atu_cpu_addr, pcie_addr, pci_resource_len(dev, bar_num[i]));
				if (ret) {
					dev_err(pcie->dev, "failed to set ob atu 0x%llx\n", atu_cpu_addr);
					return ret;
				}

				dev_err(pcie->dev, "prg ob iatu%d: cpu 0x%llx -> PCIe 0x%llx, size 0x%llx\n",
						index, atu_cpu_addr, pcie_addr, pci_resource_len(dev, bar_num[i]));
			}

			dst_chipid++;
			func_num++;
		}
	}

	return 0;
}

static inline void *get_wr_order_addr(struct sophgo_dw_pcie *pcie, uint32_t index)
{
	return pcie->c2c_top + 0x1120 + (index * 0x10);
}

static inline void *get_wr_order_en_addr(struct sophgo_dw_pcie *pcie)
{
	return pcie->c2c_top + 0x1320;
}

static inline void *get_wr_order_mode_addr(struct sophgo_dw_pcie *pcie)
{
	return pcie->c2c_top + 0x1324;
}

static inline int find_avaliable_wr_order(struct sophgo_dw_pcie *pcie)
{
	uint32_t wr_order_en_val = readl(get_wr_order_en_addr(pcie));

	for (int i = 0; i < 32; i++) {
		if (!(wr_order_en_val & (1 << i)))
			return i;
	}

	return -1;
}

static inline void enable_wr_order(struct sophgo_dw_pcie *pcie, uint32_t index)
{
	uint32_t wr_order_en_val = readl(get_wr_order_en_addr(pcie));

	wr_order_en_val |= (1 << index);
	writel(wr_order_en_val, get_wr_order_en_addr(pcie));
}

static inline void set_wr_order_mode(struct sophgo_dw_pcie *pcie, uint32_t index, uint32_t mode)
{
	void *wr_order_mode_addr = get_wr_order_mode_addr(pcie);
	uint32_t wr_order_mode_val;
	uint32_t atu_index = index % 16;

	wr_order_mode_addr = index < 16 ?  wr_order_mode_addr + 0x0 : wr_order_mode_addr + 0x4;
	wr_order_mode_val = readl(wr_order_mode_addr);
	wr_order_mode_val &= ~(0x3 << (atu_index * 2));
	wr_order_mode_val |= (mode << (atu_index * 2));
	writel(wr_order_mode_val, wr_order_mode_addr);
}

static int prog_wr_order(struct sophgo_dw_pcie *pcie, uint32_t index, uint32_t mode,
	uint64_t barid, uint64_t start_addr, uint64_t size)
{
	void *wr_order = get_wr_order_addr(pcie, index);
	uint64_t end_addr;

	if (mode == WR_ORDER_CHIP_MODE)
		start_addr |= (barid << 40);

	end_addr = start_addr + size - 1;

	writel(lower_32_bits(start_addr), wr_order + WR_ORDER_START_LOWER);
	writel(upper_32_bits(start_addr), wr_order + WR_ORDER_START_UPPER);
	writel(lower_32_bits(end_addr), wr_order + WR_ORDER_END_LOWER);
	writel(upper_32_bits(end_addr), wr_order + WR_ORDER_END_UPPER);

	set_wr_order_mode(pcie, index, mode);
	enable_wr_order(pcie, index);

	dev_err(pcie->dev, "wr_order %d:0x%llx -> 0x%llx, mode:0x%x\n", index, start_addr,
		end_addr, mode);

	return 0;
}

static int config_wr_order(struct sophgo_dw_pcie *pcie)
{
	int wr_order_index;

	struct wr_order_list cdma_access_order[] = {
		[0] = {
			.start_addr = 0x6c00000000,
			.size = 0x1000000,
		},
		[1] = {
			.start_addr = 0x6e00000000,
			.size = 0x1000000,
		},
	};

	struct wr_order_list ap_access_order[] = {
		[0] = {
			.start_addr = 0x4800000000,
			.size = 0x1000000,
		},
		[1] = {
			.start_addr = 0x5000000000,
			.size = 0x1000000,
		},
		[2] = {
			.start_addr = 0x4900000000,
			.size = 0x1000000,
		},
		[3] = {
			.start_addr = 0x5100000000,
			.size = 0x1000000,
		},
	};

	struct wr_order_list msi_access_order[] = {
		[0] = {
			.start_addr = 0x7100000000,
			.size = 0x1000000,
		},
		[1] = {
			.start_addr = 0x7200000000,
			.size = 0x1000000,
		},
		[2] = {
			.start_addr = 0x7300000000,
			.size = 0x1000000,
		},
		[3] = {
			.start_addr = 0x7400000000,
			.size = 0x1000000,
		},
	};

	if (pcie->chip_type == CHIP_BM1690)
		return 0;
	else if (pcie->chip_type == CHIP_BM1690E) {
		for (int i = 0; i < sizeof(cdma_access_order) / sizeof(struct wr_order_list); i++) {
			wr_order_index = find_avaliable_wr_order(pcie);
			if (wr_order_index < 0) {
				dev_err(pcie->dev, "cdma access order no available wr order\n");
				return -1;
			}
			prog_wr_order(pcie, wr_order_index, WR_ORDER_CHIP_MODE,
				0, cdma_access_order[i].start_addr, cdma_access_order[i].size);
		}

		for (int i = 0; i < sizeof(ap_access_order) / sizeof(struct wr_order_list); i++) {
			wr_order_index = find_avaliable_wr_order(pcie);
			if (wr_order_index < 0) {
				dev_err(pcie->dev, "ap access order no available wr order\n");
				return -1;
			}
			prog_wr_order(pcie, wr_order_index, WR_ORDER_CHIP_MODE,
				0, ap_access_order[i].start_addr, ap_access_order[i].size);
		}

		for (int i = 0; i < sizeof(msi_access_order) / sizeof(struct wr_order_list); i++) {
			wr_order_index = find_avaliable_wr_order(pcie);
			if (wr_order_index < 0) {
				dev_err(pcie->dev, "msi access order no available wr order\n");
				return -1;
			}
			prog_wr_order(pcie, wr_order_index, WR_ORDER_CHIP_MODE,
				0, msi_access_order[i].start_addr, msi_access_order[i].size);
		}
	} else {
		dev_err(pcie->dev, "%s Unknown chip type\n", __func__);
		return -1;
	}

	return 0;
}

static inline void *get_portcode_addr(struct sophgo_dw_pcie *pcie)
{
	return pcie->c2c_top;
}

static int bm1690e_pcie_config_port_code(struct sophgo_dw_pcie *pcie)
{
	void *portcode = get_portcode_addr(pcie);
	uint32_t portcode_val = 0;
	__attribute__((unused)) uint32_t pld_portcode_route_bits[4] = {
		0x25550,
		0x35503,
		0x25022,
	};
	uint32_t portcode_route_bits[4] = {
		0xd5550,
		0xc550c,
		0x35033,
	};

	portcode_val = ((pcie->board_size << PORTCODE_BOARDSIZE_SHIFT) |
			(pcie->board_id << PORTCODE_BOARDID_SHIFT) |
			portcode_route_bits[pcie->socket_id]);

	writel(portcode_val, portcode);
	dev_err(pcie->dev, "portcode_val:0x%x\n", portcode_val);

	return 0;
}

static void bm1690_pcie_config_port_code(struct sophgo_dw_pcie *pcie)
{
	uint32_t val = 0;
	int c2c_id = 1;

	//config port code
	if (c2c_id == 0)
		val = (0xf) | (0x0 << 4) | (0xf << 8) | (0xf << 12) | (0xf << 16);
	else if (c2c_id == 1)
		val = (0x0) | (0x5 << 4) | (0xf << 8) | (0xf << 12) | (0x7 << 16);

	writel(val, pcie->c2c_top);
	dev_err(pcie->dev, "pcie port code:0x%x\n", val);
}

static int config_port_code(struct sophgo_dw_pcie *pcie)
{
	if (pcie->chip_type == CHIP_BM1690)
		bm1690_pcie_config_port_code(pcie);
	else if (pcie->chip_type == CHIP_BM1690E)
		bm1690e_pcie_config_port_code(pcie);
	else
		dev_err(pcie->dev, "%s Unknown chip type\n", __func__);

	return 0;
}

static int config_device_id(struct sophgo_dw_pcie *pcie, uint32_t device_id)
{
	uint32_t val = 0;

	//enable DBI_RO_WR_EN
	val = readl(pcie->dbi_base + 0x8bc);
	val = (val & 0xfffffffe) | 0x1;
	writel(val, (pcie->dbi_base + 0x8bc));

	val = readl(pcie->dbi_base);
	val &= 0xffff;
	val |= (device_id << 16);
	writel(val, pcie->dbi_base);
	val = readl(pcie->dbi_base);
	pr_err("pcie id:0x%x", val);

	// disable DBI_RO_WR_EN
	val = readl(pcie->dbi_base + 0x8bc);
	val &= 0xfffffffe;
	writel(val, (pcie->dbi_base + 0x8bc));

	return 0;
}

int sophgo_dw_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_dw_pcie *pcie;
	struct dw_pcie_rp *pp;
	struct resource_entry *win;
	struct pci_host_bridge *bridge;
	struct resource *res;
	int ret = 0;

	dev_err(dev, "probe\n");

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = dev;

	platform_set_drvdata(pdev, pcie);

	pp = &pcie->pp;

	raw_spin_lock_init(&pp->lock);

	ret = sophgo_dw_pcie_get_resources(pcie);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (res) {
		pp->cfg0_size = resource_size(res);
		pp->cfg0_base = res->start;

		pp->va_cfg0_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(pp->va_cfg0_base))
			return PTR_ERR(pp->va_cfg0_base);
	} else {
		dev_err(dev, "Missing *config* reg space\n");
		return -ENODEV;
	}

	if (pcie->pcie_card) {
		dev_err(dev, "pcie card mode, begin init pcie bus\n");
		ret = sophgo_pcie_host_init_port(pcie);
		if (ret)
			return ret;

		if (pcie->c2c_pcie_rc)
			sophgo_pcie_config_cdma_route(pcie);

		config_device_id(pcie, 0x2044);
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	pp->bridge = bridge;

	/* Get the I/O range from DT */
	win = resource_list_first_type(&bridge->windows, IORESOURCE_IO);
	if (win) {
		pp->io_size = resource_size(win->res);
		pp->io_bus_addr = win->res->start - win->offset;
		pp->io_base = pci_pio_to_address(win->res->start);
	}

	/* Set default bus ops */
	bridge->ops = &sophgo_dw_pcie_ops;
	bridge->child_ops = &sophgo_dw_child_pcie_ops;
	sophgo_dw_pcie_version_detect(pcie);
	sophgo_dw_pcie_iatu_detect(pcie);
	ret = sophgo_dw_pcie_setup_rc(pp);
	if (ret)
		return ret;

	ret = sophgo_dw_pcie_setup_irq(pcie);
	if (ret)
		dev_err(dev, "pcie intx interrupt request fail, ret = %d\n", ret);

	bridge->sysdata = pp;
	bridge->dev.parent = dev;
	bridge->ops = &sophgo_dw_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_host_probe(bridge);
	if (ret)
		return ret;

	if (pcie->pcie_card && pcie->c2c_pcie_rc == 0) {
		config_rc_atu(pcie, bridge);
		config_port_code(pcie);
		config_wr_order(pcie);
	}

	return 0;
}

static const struct of_device_id sophgo_dw_pcie_of_match[] = {
	{ .compatible = "sophgo,sg2044-pcie-host", },
	{ .compatible = "sophgo,bm1690-pcie-host", },
	{ .compatible = "sophgo,bm1690e-pcie-host", },
	{},
};

static struct platform_driver sophgo_dw_pcie_driver = {
	.driver = {
		.name	= "sophgo-dw-pcie",
		.of_match_table = sophgo_dw_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = sophgo_dw_pcie_probe,
};
builtin_platform_driver(sophgo_dw_pcie_driver);


#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)

static struct fwnode_handle *pci_host_bridge_acpi_get_fwnode(struct device *dev)
{
	struct pci_bus *bus = container_of(dev, struct pci_bus, dev);
	struct pci_config_window *cfg = bus->sysdata;
	struct device *target_dev = cfg->parent;

	return acpi_fwnode_handle(to_acpi_device(target_dev));
}

static int sophgo_pcie_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct acpi_device *adev = to_acpi_device(dev);
	struct acpi_pci_root *root = acpi_driver_data(adev);
	struct sophgo_dw_pcie *pcie;
	struct dw_pcie_rp *pp;
	struct resource res[4];
	int ret = 0;

	struct list_head resource_list, *list;
	struct resource_entry *win;
	unsigned long flags;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = dev;
	pp = &pcie->pp;

	ret = acpi_get_rc_target_num_resources(dev, "SOPH0000", root->segment, res, 4);

	ret = sophgo_dw_pcie_get_resources_acpi(pcie, res);
	if (ret)
		return ret;
	pp->cfg0_size = resource_size(&cfg->res);
	pp->cfg0_base = cfg->res.start;
	pp->va_cfg0_base = cfg->win;

	// Get the I/O range from ACPI Table
	INIT_LIST_HEAD(&resource_list);
	flags = IORESOURCE_IO;
	ret = acpi_dev_get_resources(adev, &resource_list, acpi_dev_filter_resource_type_cb, (void *)flags);
	if (ret < 0) {
		pr_err("failed to parse _CRS method, error code %d\n", ret);
		return ret;
	} else if (ret == 0) {
		pr_err("no IO and memory resources present in _CRS\n");
		return -EINVAL;
	}
	list = &resource_list;

	win = resource_list_first_type(list, IORESOURCE_IO);
	if (win) {
		pp->io_size = resource_size(win->res);
		pp->io_bus_addr = win->res->start - win->offset;
		pp->io_base = win->res->start;
	}

	sophgo_dw_pcie_version_detect(pcie);
	sophgo_dw_pcie_iatu_detect(pcie);
	ret = sophgo_dw_pcie_setup_rc(pp);

	pci_msi_register_fwnode_provider(&pci_host_bridge_acpi_get_fwnode);

	cfg->priv = pp;
	return 0;
}

const struct pci_ecam_ops sophgo_pci_ecam_ops = {
	.init         = sophgo_pcie_init,
	.pci_ops      = {
		.map_bus    = sophgo_dw_pcie_own_conf_map_bus,
		.read       = sophgo_dw_pcie_rd_other_conf,
		.write      = sophgo_dw_pcie_wr_other_conf,
	}
};

#endif
