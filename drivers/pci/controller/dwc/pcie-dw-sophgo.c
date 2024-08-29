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

#include "pcie-dw-sophgo.h"


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
	struct irq_domain *irq_parent = sophgo_dw_pcie_get_parent_irq_domain();
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	struct fwnode_handle *fwnode = of_node_to_fwnode(pcie->dev->of_node);

	pp->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &sophgo_dw_pcie_msi_domain_info,
						   irq_parent);
	if (!pp->msi_domain) {
		dev_err(pcie->dev, "create msi irq domain failed\n");
		return -ENODEV;
	}

	return 0;
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

static int sophgo_dw_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	int ret = 0;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
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

	ret = pci_generic_config_write(bus, devfn, where, size, val);
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

void __iomem *sophgo_dw_pcie_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);

	if (PCI_SLOT(devfn) > 0)
		return NULL;

	return pcie->dbi_base + where;
}

static struct pci_ops sophgo_dw_child_pcie_ops = {
	.map_bus = sophgo_dw_pcie_other_conf_map_bus,
	.read = sophgo_dw_pcie_rd_other_conf,
	.write = sophgo_dw_pcie_wr_other_conf,
};

static struct pci_ops sophgo_dw_pcie_ops = {
	.map_bus = sophgo_dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int sophgo_dw_pcie_get_resources(struct sophgo_dw_pcie *pcie)
{
	struct platform_device *pdev = to_platform_device(pcie->dev);
	struct device_node *np = dev_of_node(pcie->dev);
	struct resource *res;
	//int ret;

	if (!pcie->dbi_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
		pcie->dbi_base = devm_pci_remap_cfg_resource(pcie->dev, res);
		if (IS_ERR(pcie->dbi_base))
			return PTR_ERR(pcie->dbi_base);
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
	if (pcie->link_gen < 1)
		pcie->link_gen = of_pci_get_max_link_speed(np);

	of_property_read_u32(np, "num-lanes", &pcie->num_lanes);

	if (of_property_read_bool(np, "snps,enable-cdm-check"))
		dw_pcie_cap_set(pcie, CDM_CHECK);

	return 0;
}

static int sophgo_dw_pcie_iatu_setup(struct dw_pcie_rp *pp)
{
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	struct resource_entry *entry;
	int i = 0;
	int ret = 0;

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

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->windows) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pcie->num_ob_windows <= ++i)
			break;

		ret = sophgo_dw_pcie_prog_outbound_atu(pcie, i, PCIE_ATU_TYPE_MEM,
						entry->res->start,
						entry->res->start - entry->offset,
						resource_size(entry->res));
		if (ret) {
			dev_err(pcie->dev, "Failed to set MEM range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pp->io_size) {
		if (pcie->num_ob_windows > ++i) {
			ret = sophgo_dw_pcie_prog_outbound_atu(pcie, i, PCIE_ATU_TYPE_IO,
							pp->io_base,
							pp->io_bus_addr,
							pp->io_size);
			if (ret) {
				dev_err(pcie->dev, "Failed to set IO range %pr\n",
					entry->res);
				return ret;
			}
		} else {
			pp->cfg0_io_shared = true;
		}
	}

	if (pcie->num_ob_windows <= i)
		dev_warn(pcie->dev, "Ranges exceed outbound iATU size (%d)\n",
			 pcie->num_ob_windows);

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->dma_ranges) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pcie->num_ib_windows <= i)
			break;

		ret = sophgo_dw_pcie_prog_inbound_atu(pcie, i++, PCIE_ATU_TYPE_MEM,
					       entry->res->start,
					       entry->res->start - entry->offset,
					       resource_size(entry->res));
		if (ret) {
			dev_err(pcie->dev, "Failed to set DMA range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pcie->num_ib_windows <= i)
		dev_warn(pcie->dev, "Dma-ranges exceed inbound iATU size (%u)\n",
			 pcie->num_ib_windows);

	return 0;
}
static int sophgo_dw_pcie_setup_rc(struct dw_pcie_rp *pp)
{
	struct sophgo_dw_pcie *pcie = to_sophgo_dw_pcie_from_pp(pp);
	u32 val = 0;
	u32 ctrl = 0;
	u32 num_ctrls = 0;
	int ret = 0;

	/*
	 * Enable DBI read-only registers for writing/updating configuration.
	 * Write permission gets disabled towards the end of this function.
	 */
	sophgo_dw_pcie_dbi_ro_wr_en(pcie);

	//sophgo_dw_pcie_setup(pcie);

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

	sophgo_dw_pcie_msi_setup(pp);

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
	if (pp->bridge->child_ops == &sophgo_dw_child_pcie_ops) {
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

int sophgo_dw_pcie_parse_irq_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return 0; /* Proper return code 0 == NO_IRQ */
}

static int sophgo_dw_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_dw_pcie *pcie;
	struct dw_pcie_rp *pp;
	struct resource_entry *win;
	struct pci_host_bridge *bridge;
	struct resource *res;
	int ret = 0;

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

	bridge->sysdata = pp;
	bridge->dev.parent = dev;
	bridge->ops = &sophgo_dw_pcie_ops;
	bridge->map_irq = sophgo_dw_pcie_parse_irq_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_host_probe(bridge);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id sophgo_dw_pcie_of_match[] = {
	{ .compatible = "sophgo,sg2044-pcie-host", },
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
