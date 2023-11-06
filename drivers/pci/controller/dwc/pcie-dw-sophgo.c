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
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "pcie-designware.h"
#include "pcie-dw-sophgo.h"


static void dw_pcie_msi_ack_irq(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void dw_pcie_msi_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void dw_pcie_msi_unmask_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip dw_pcie_msi_irq_chip = {
	.name = "dw-msi",
	.irq_ack = dw_pcie_msi_ack_irq,
	.irq_mask = dw_pcie_msi_mask_irq,
	.irq_unmask = dw_pcie_msi_unmask_irq,
};

static struct msi_domain_info dw_pcie_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &dw_pcie_msi_irq_chip,
};

static int dw_pcie_msi_setup_for_top_intc(struct sophgo_dw_pcie *pcie)
{
#if 0
	struct irq_domain *irq_parent = cdns_pcie_get_parent_irq_domain();
	struct fwnode_handle *fwnode = of_node_to_fwnode(rc->dev->of_node);

	rc->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &cdns_pcie_msi_domain_info,
						   irq_parent);
	if (!rc->msi_domain) {
		dev_err(rc->dev, "create msi irq domain failed\n");
		return -ENODEV;
	}
#endif

	return 0;
}

static void __iomem *dw_pcie_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int type, ret;
	u32 busdev;

	/*
	 * Checking whether the link is up here is a last line of defense
	 * against platforms that forward errors on the system bus as
	 * SError upon PCI configuration transactions issued when the link
	 * is down. This check is racy by definition and does not stop
	 * the system from triggering an SError if the link goes down
	 * after this check is performed.
	 */
	if (!dw_pcie_link_up(pci))
		return NULL;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	ret = dw_pcie_prog_outbound_atu(pci, 0, type, pp->cfg0_base, busdev,
					pp->cfg0_size);
	if (ret)
		return NULL;

	return pp->va_cfg0_base + where;
}

static int dw_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = dw_pcie_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int dw_pcie_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret;

	ret = pci_generic_config_write(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = dw_pcie_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops dw_child_pcie_ops = {
	.map_bus = dw_pcie_other_conf_map_bus,
	.read = dw_pcie_rd_other_conf,
	.write = dw_pcie_wr_other_conf,
};

static struct pci_ops dw_pcie_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int sophgo_dw_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_dw_pcie *sophgo_dw_pcie;
	struct dw_pcie *pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device_node *np = dev->of_node;
	struct resource_entry *win;
	struct pci_host_bridge *bridge;
	struct resource *res;
	int ret;

	sophgo_dw_pcie = devm_kzalloc(dev, sizeof(*sophgo_dw_pcie), GFP_KERNEL);
	if (!sophgo_dw_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;

	sophgo_dw_pcie->pci = pci;
	platform_set_drvdata(pdev, sophgo_dw_pcie);


	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0)
		return pp->irq;

	//pp->num_vectors = MAX_MSI_IRQS;
	//pp->ops = &dw_plat_pcie_host_ops;

	raw_spin_lock_init(&pp->lock);

	ret = dw_pcie_get_resources(pci);
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
	bridge->ops = &dw_pcie_ops;
	bridge->child_ops = &dw_child_pcie_ops;

	if (pp->ops->host_init) {
		ret = pp->ops->host_init(pp);
		if (ret)
			return ret;
	}
#if 0
	if (pci_msi_enabled()) {
		pp->has_msi_ctrl = !(pp->ops->msi_host_init ||
				     of_property_read_bool(np, "msi-parent") ||
				     of_property_read_bool(np, "msi-map"));

		/*
		 * For the has_msi_ctrl case the default assignment is handled
		 * in the dw_pcie_msi_host_init().
		 */
		if (!pp->has_msi_ctrl && !pp->num_vectors) {
			pp->num_vectors = MSI_DEF_NUM_VECTORS;
		} else if (pp->num_vectors > MAX_MSI_IRQS) {
			dev_err(dev, "Invalid number of vectors\n");
			ret = -EINVAL;
			goto err_deinit_host;
		}

		if (pp->ops->msi_host_init) {
			ret = pp->ops->msi_host_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		} else if (pp->has_msi_ctrl) {
			ret = dw_pcie_msi_host_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		}
	}
#endif
	dw_pcie_version_detect(pci);

	dw_pcie_iatu_detect(pci);

	ret = dw_pcie_edma_detect(pci);
	if (ret)
		goto err_free_msi;

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		goto err_remove_edma;

	if (!dw_pcie_link_up(pci)) {
		ret = dw_pcie_start_link(pci);
		if (ret)
			goto err_remove_edma;
	}

	/* Ignore errors, the link may come up later */
	dw_pcie_wait_for_link(pci);

	bridge->sysdata = pp;

	ret = pci_host_probe(bridge);
	if (ret)
		goto err_stop_link;

	return 0;

err_stop_link:
	dw_pcie_stop_link(pci);

err_remove_edma:
	dw_pcie_edma_remove(pci);

err_free_msi:
	//if (pp->has_msi_ctrl)
		//dw_pcie_free_msi(pp);

//err_deinit_host:
	//if (pp->ops->host_deinit)
		//pp->ops->host_deinit(pp);

	return ret;
}

static const struct of_device_id sophgo_pcie_of_match[] = {
	{ .compatible = "sophgo,sg2260-pcie", },
	{},
};

static struct platform_driver sophgo_dw_pcie_driver = {
	.driver = {
		.name	= "sophgo-dw-pcie",
		.of_match_table = sophgo_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = sophgo_dw_pcie_probe,
};
builtin_platform_driver(sophgo_dw_pcie_driver);