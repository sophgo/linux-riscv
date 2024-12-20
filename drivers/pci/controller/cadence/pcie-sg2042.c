// SPDX-License-Identifier: GPL-2.0
/*
 * pcie-sg2042 - PCIe controller driver for Sophgo SG2042 SoC
 *
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Chen Wang <unicorn_wang@outlook.com>
 */

#include <linux/bits.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "../../../irqchip/irq-msi-lib.h"

#include "pcie-cadence.h"

/*
 * SG2042 PCIe controller supports two ways to report MSI:
 *
 * - Method A, the PCIe controller implements an MSI interrupt controller
 *   inside, and connect to PLIC upward through one interrupt line.
 *   Provides memory-mapped MSI address, and by programming the upper 32
 *   bits of the address to zero, it can be compatible with old PCIe devices
 *   that only support 32-bit MSI address.
 *
 * - Method B, the PCIe controller connects to PLIC upward through an
 *   independent MSI controller "sophgo,sg2042-msi" on the SOC. The MSI
 *   controller provides multiple(up to 32) interrupt sources to PLIC.
 *   Compared with the first method, the advantage is that the interrupt
 *   source is expanded, but because for SG2042, the MSI address provided by
 *   the MSI controller is fixed and only supports 64-bit address(> 2^32),
 *   it is not compatible with old PCIe devices that only support 32-bit MSI
 *   address.
 *
 * Method A & B can be configured in DTS, default is Method B.
 */

#define MAX_MSI_IRQS		8
#define MAX_MSI_IRQS_PER_CTRL	1
#define MAX_MSI_CTRLS		(MAX_MSI_IRQS / MAX_MSI_IRQS_PER_CTRL)
#define MSI_DEF_NUM_VECTORS	MAX_MSI_IRQS
#define BYTE_NUM_PER_MSI_VEC	4

#define REG_CLEAR		0x0804
#define REG_STATUS		0x0810
#define REG_LINK0_MSI_ADDR_SIZE	0x085C
#define REG_LINK1_MSI_ADDR_SIZE	0x080C
#define REG_LINK0_MSI_ADDR_LOW	0x0860
#define REG_LINK0_MSI_ADDR_HIGH	0x0864
#define REG_LINK1_MSI_ADDR_LOW	0x0868
#define REG_LINK1_MSI_ADDR_HIGH	0x086C

#define REG_CLEAR_LINK0_BIT	2
#define REG_CLEAR_LINK1_BIT	3
#define REG_STATUS_LINK0_BIT	2
#define REG_STATUS_LINK1_BIT	3

#define REG_LINK0_MSI_ADDR_SIZE_MASK	GENMASK(15, 0)
#define REG_LINK1_MSI_ADDR_SIZE_MASK	GENMASK(31, 16)

struct sg2042_pcie {
	struct cdns_pcie	*cdns_pcie;

	struct regmap		*syscon;

	u32			port;

	struct irq_domain	*msi_domain;

	int			msi_irq;

	dma_addr_t		msi_phys;
	void			*msi_virt;

	u32			num_applied_vecs; /* used to speed up ISR */

	raw_spinlock_t		msi_lock;
	DECLARE_BITMAP(msi_irq_in_use, MAX_MSI_IRQS);
};

static void sg2042_pcie_msi_clear_status(struct sg2042_pcie *pcie)
{
	u32 status, clr_msi_in_bit;

	if (pcie->port == 1)
		clr_msi_in_bit = BIT(REG_CLEAR_LINK1_BIT);
	else
		clr_msi_in_bit = BIT(REG_CLEAR_LINK0_BIT);

	regmap_read(pcie->syscon, REG_CLEAR, &status);
	status |= clr_msi_in_bit;
	regmap_write(pcie->syscon, REG_CLEAR, status);

	/* need write 0 to reset, hardware can not reset automatically */
	status &= ~clr_msi_in_bit;
	regmap_write(pcie->syscon, REG_CLEAR, status);
}

#ifdef CONFIG_SMP
static int sg2042_pcie_msi_irq_set_affinity(struct irq_data *d,
					    const struct cpumask *mask,
					    bool force)
{
	if (d->parent_data)
		return irq_chip_set_affinity_parent(d, mask, force);

	return -EINVAL;
}
#endif /* CONFIG_SMP */

static void sg2042_pcie_msi_irq_compose_msi_msg(struct irq_data *d,
						struct msi_msg *msg)
{
	struct sg2042_pcie *pcie = irq_data_get_irq_chip_data(d);
	struct device *dev = pcie->cdns_pcie->dev;

	msg->address_lo = lower_32_bits(pcie->msi_phys) + BYTE_NUM_PER_MSI_VEC * d->hwirq;
	msg->address_hi = upper_32_bits(pcie->msi_phys);
	msg->data = 1;

	if (d->hwirq > pcie->num_applied_vecs)
		pcie->num_applied_vecs = d->hwirq;

	dev_dbg(dev, "compose MSI msg hwirq[%ld] address_hi[%#x] address_lo[%#x]\n",
		d->hwirq, msg->address_hi, msg->address_lo);
}

static void sg2042_pcie_msi_irq_ack(struct irq_data *d)
{
	struct sg2042_pcie *pcie = irq_data_get_irq_chip_data(d);

	sg2042_pcie_msi_clear_status(pcie);
}

static struct irq_chip sg2042_pcie_msi_bottom_chip = {
	.name			= "SG2042 PCIe PLIC-MSI translator",
	.irq_ack		= sg2042_pcie_msi_irq_ack,
	.irq_compose_msi_msg	= sg2042_pcie_msi_irq_compose_msi_msg,
#ifdef CONFIG_SMP
	.irq_set_affinity	= sg2042_pcie_msi_irq_set_affinity,
#endif
};

static int sg2042_pcie_irq_domain_alloc(struct irq_domain *domain,
					unsigned int virq, unsigned int nr_irqs,
					void *args)
{
	struct sg2042_pcie *pcie = domain->host_data;
	unsigned long flags;
	u32 i;
	int bit;

	raw_spin_lock_irqsave(&pcie->msi_lock, flags);

	bit = bitmap_find_free_region(pcie->msi_irq_in_use, MSI_DEF_NUM_VECTORS,
				      order_base_2(nr_irqs));

	raw_spin_unlock_irqrestore(&pcie->msi_lock, flags);

	if (bit < 0)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, bit + i,
				    &sg2042_pcie_msi_bottom_chip,
				    pcie, handle_edge_irq,
				    NULL, NULL);

	return 0;
}

static void sg2042_pcie_irq_domain_free(struct irq_domain *domain,
					unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct sg2042_pcie *pcie = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	raw_spin_lock_irqsave(&pcie->msi_lock, flags);

	bitmap_release_region(pcie->msi_irq_in_use, d->hwirq,
			      order_base_2(nr_irqs));

	raw_spin_unlock_irqrestore(&pcie->msi_lock, flags);
}

static const struct irq_domain_ops sg2042_pcie_msi_domain_ops = {
	.alloc	= sg2042_pcie_irq_domain_alloc,
	.free	= sg2042_pcie_irq_domain_free,
};

static int sg2042_pcie_init_msi_data(struct sg2042_pcie *pcie)
{
	struct device *dev = pcie->cdns_pcie->dev;
	u32 value;
	int ret;

	raw_spin_lock_init(&pcie->msi_lock);

	/*
	 * Though the PCIe controller can address >32-bit address space, to
	 * facilitate endpoints that support only 32-bit MSI target address,
	 * the mask is set to 32-bit to make sure that MSI target address is
	 * always a 32-bit address
	 */
	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret < 0)
		return ret;

	pcie->msi_virt = dma_alloc_coherent(dev, BYTE_NUM_PER_MSI_VEC * MAX_MSI_IRQS,
					    &pcie->msi_phys, GFP_KERNEL);
	if (!pcie->msi_virt)
		return -ENOMEM;

	/* Program the MSI address and size */
	if (pcie->port == 1) {
		regmap_write(pcie->syscon, REG_LINK1_MSI_ADDR_LOW,
			     lower_32_bits(pcie->msi_phys));
		regmap_write(pcie->syscon, REG_LINK1_MSI_ADDR_HIGH,
			     upper_32_bits(pcie->msi_phys));

		regmap_read(pcie->syscon, REG_LINK1_MSI_ADDR_SIZE, &value);
		value = (value & REG_LINK1_MSI_ADDR_SIZE_MASK) | MAX_MSI_IRQS;
		regmap_write(pcie->syscon, REG_LINK1_MSI_ADDR_SIZE, value);
	} else {
		regmap_write(pcie->syscon, REG_LINK0_MSI_ADDR_LOW,
			     lower_32_bits(pcie->msi_phys));
		regmap_write(pcie->syscon, REG_LINK0_MSI_ADDR_HIGH,
			     upper_32_bits(pcie->msi_phys));

		regmap_read(pcie->syscon, REG_LINK0_MSI_ADDR_SIZE, &value);
		value = (value & REG_LINK0_MSI_ADDR_SIZE_MASK) | (MAX_MSI_IRQS << 16);
		regmap_write(pcie->syscon, REG_LINK0_MSI_ADDR_SIZE, value);
	}

	return 0;
}

static irqreturn_t sg2042_pcie_msi_handle_irq(struct sg2042_pcie *pcie)
{
	u32 i, pos;
	unsigned long val;
	u32 status, num_vectors;
	irqreturn_t ret = IRQ_NONE;

	num_vectors = pcie->num_applied_vecs;
	for (i = 0; i <= num_vectors; i++) {
		status = readl((void *)(pcie->msi_virt + i * BYTE_NUM_PER_MSI_VEC));
		if (!status)
			continue;

		ret = IRQ_HANDLED;
		val = status;
		pos = 0;
		while ((pos = find_next_bit(&val, MAX_MSI_IRQS_PER_CTRL,
					    pos)) != MAX_MSI_IRQS_PER_CTRL) {
			generic_handle_domain_irq(pcie->msi_domain,
						  (i * MAX_MSI_IRQS_PER_CTRL) +
						  pos);
			pos++;
		}
		writel(0, ((void *)(pcie->msi_virt) + i * BYTE_NUM_PER_MSI_VEC));
	}
	return ret;
}

static void sg2042_pcie_msi_chained_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 status, st_msi_in_bit;
	struct sg2042_pcie *pcie;

	chained_irq_enter(chip, desc);

	pcie = irq_desc_get_handler_data(desc);
	if (pcie->port == 1)
		st_msi_in_bit = REG_STATUS_LINK1_BIT;
	else
		st_msi_in_bit = REG_STATUS_LINK0_BIT;

	regmap_read(pcie->syscon, REG_STATUS, &status);
	if ((status >> st_msi_in_bit) & 0x1) {
		sg2042_pcie_msi_clear_status(pcie);

		sg2042_pcie_msi_handle_irq(pcie);
	}

	chained_irq_exit(chip, desc);
}

#define SG2042_PCIE_MSI_FLAGS_REQUIRED (MSI_FLAG_USE_DEF_DOM_OPS |	\
					MSI_FLAG_USE_DEF_CHIP_OPS)

#define SG2042_PCIE_MSI_FLAGS_SUPPORTED MSI_GENERIC_FLAGS_MASK

static struct msi_parent_ops sg2042_pcie_msi_parent_ops = {
	.required_flags		= SG2042_PCIE_MSI_FLAGS_REQUIRED,
	.supported_flags	= SG2042_PCIE_MSI_FLAGS_SUPPORTED,
	.bus_select_mask	= MATCH_PCI_MSI,
	.bus_select_token	= DOMAIN_BUS_NEXUS,
	.prefix			= "SG2042-",
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};

static int sg2042_pcie_setup_msi(struct sg2042_pcie *pcie, struct device_node *msi_node)
{
	struct device *dev = pcie->cdns_pcie->dev;
	struct fwnode_handle *fwnode = of_node_to_fwnode(dev->of_node);
	struct irq_domain *parent_domain;
	int ret = 0;

	if (!of_property_read_bool(msi_node, "msi-controller"))
		return -ENODEV;

	ret = of_irq_get_byname(msi_node, "msi");
	if (ret <= 0) {
		dev_err(dev, "%pOF: failed to get MSI irq\n", msi_node);
		return ret;
	}
	pcie->msi_irq = ret;

	irq_set_chained_handler_and_data(pcie->msi_irq,
					 sg2042_pcie_msi_chained_isr, pcie);

	parent_domain = irq_domain_create_linear(fwnode, MSI_DEF_NUM_VECTORS,
						 &sg2042_pcie_msi_domain_ops, pcie);
	if (!parent_domain) {
		dev_err(dev, "%pfw: Failed to create IRQ domain\n", fwnode);
		return -ENOMEM;
	}
	irq_domain_update_bus_token(parent_domain, DOMAIN_BUS_NEXUS);

	parent_domain->flags |= IRQ_DOMAIN_FLAG_MSI_PARENT;
	parent_domain->msi_parent_ops = &sg2042_pcie_msi_parent_ops;

	pcie->msi_domain = parent_domain;

	ret = sg2042_pcie_init_msi_data(pcie);
	if (ret) {
		dev_err(dev, "Failed to initialize MSI data!\n");
		return ret;
	}

	return 0;
}

static void sg2042_pcie_free_msi(struct sg2042_pcie *pcie)
{
	struct device *dev = pcie->cdns_pcie->dev;

	if (pcie->msi_irq)
		irq_set_chained_handler_and_data(pcie->msi_irq, NULL, NULL);

	if (pcie->msi_virt)
		dma_free_coherent(dev, BYTE_NUM_PER_MSI_VEC * MAX_MSI_IRQS,
				  pcie->msi_virt, pcie->msi_phys);
}

/*
 * SG2042 only support 4-byte aligned access, so for the rootbus (i.e. to read
 * the PCIe controller itself, read32 is required. For non-rootbus (i.e. to read
 * the PCIe peripheral registers, supports 1/2/4 byte aligned access, so
 * directly use read should be fine.
 *
 * The same is true for write.
 */
static int sg2042_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *value)
{
	if (pci_is_root_bus(bus))
		return pci_generic_config_read32(bus, devfn, where, size,
						 value);

	return pci_generic_config_read(bus, devfn, where, size, value);
}

static int sg2042_pcie_config_write(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 value)
{
	if (pci_is_root_bus(bus))
		return pci_generic_config_write32(bus, devfn, where, size,
						  value);

	return pci_generic_config_write(bus, devfn, where, size, value);
}

static struct pci_ops sg2042_pcie_host_ops = {
	.map_bus	= cdns_pci_map_bus,
	.read		= sg2042_pcie_config_read,
	.write		= sg2042_pcie_config_write,
};

/* Dummy ops which will be assigned to cdns_pcie.ops, which must be !NULL. */
static const struct cdns_pcie_ops sg2042_cdns_pcie_ops = {};

static int sg2042_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pci_host_bridge *bridge;
	struct device_node *np_syscon;
	struct device_node *msi_node;
	struct cdns_pcie *cdns_pcie;
	struct sg2042_pcie *pcie;
	struct cdns_pcie_rc *rc;
	struct regmap *syscon;
	int ret;

	if (!IS_ENABLED(CONFIG_PCIE_CADENCE_HOST))
		return -ENODEV;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rc));
	if (!bridge) {
		dev_err(dev, "Failed to alloc host bridge!\n");
		return -ENOMEM;
	}

	bridge->ops = &sg2042_pcie_host_ops;

	rc = pci_host_bridge_priv(bridge);
	cdns_pcie = &rc->pcie;
	cdns_pcie->dev = dev;
	cdns_pcie->ops = &sg2042_cdns_pcie_ops;
	pcie->cdns_pcie = cdns_pcie;

	np_syscon = of_parse_phandle(np, "sophgo,syscon-pcie-ctrl", 0);
	if (!np_syscon) {
		dev_err(dev, "Failed to get syscon node\n");
		return -ENOMEM;
	}
	syscon = syscon_node_to_regmap(np_syscon);
	if (IS_ERR(syscon)) {
		dev_err(dev, "Failed to get regmap for syscon\n");
		return -ENOMEM;
	}
	pcie->syscon = syscon;

	if (of_property_read_u32(np, "sophgo,pcie-port", &pcie->port)) {
		dev_err(dev, "Unable to parse sophgo,pcie-port\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, pcie);

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_get_sync;
	}

	msi_node = of_parse_phandle(dev->of_node, "msi-parent", 0);
	if (!msi_node) {
		dev_err(dev, "Failed to get msi-parent!\n");
		return -1;
	}

	if (of_device_is_compatible(msi_node, "sophgo,sg2042-pcie-msi")) {
		ret = sg2042_pcie_setup_msi(pcie, msi_node);
		if (ret < 0)
			goto err_setup_msi;
	}

	ret = cdns_pcie_init_phy(dev, cdns_pcie);
	if (ret) {
		dev_err(dev, "Failed to init phy!\n");
		goto err_setup_msi;
	}

	ret = cdns_pcie_host_setup(rc);
	if (ret < 0) {
		dev_err(dev, "Failed to setup host!\n");
		goto err_host_setup;
	}

	return 0;

err_host_setup:
	cdns_pcie_disable_phy(cdns_pcie);

err_setup_msi:
	sg2042_pcie_free_msi(pcie);

err_get_sync:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void sg2042_pcie_shutdown(struct platform_device *pdev)
{
	struct sg2042_pcie *pcie = platform_get_drvdata(pdev);
	struct cdns_pcie *cdns_pcie = pcie->cdns_pcie;
	struct device *dev = &pdev->dev;

	sg2042_pcie_free_msi(pcie);

	cdns_pcie_disable_phy(cdns_pcie);

	pm_runtime_put(dev);
	pm_runtime_disable(dev);
}

static const struct of_device_id sg2042_pcie_of_match[] = {
	{ .compatible = "sophgo,sg2042-pcie-host" },
	{},
};

static struct platform_driver sg2042_pcie_driver = {
	.driver = {
		.name		= "sg2042-pcie",
		.of_match_table	= sg2042_pcie_of_match,
		.pm		= &cdns_pcie_pm_ops,
	},
	.probe		= sg2042_pcie_probe,
	.shutdown	= sg2042_pcie_shutdown,
};
builtin_platform_driver(sg2042_pcie_driver);
