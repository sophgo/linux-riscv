// SPDX-License-Identifier: GPL-2.0-only
/*
 * Multiplex several IPIs over a single HW IPI.
 *
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv: " fmt
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/sbi.h>

static int sbi_ipi_virq;

static u32 __iomem *clint_ipi_base;

static void clint_send_ipi(unsigned int cpu)
{
	writel(1, clint_ipi_base + cpuid_to_hartid_map(cpu));
}

static void clint_clear_ipi(void)
{
	writel(0, clint_ipi_base + cpuid_to_hartid_map(smp_processor_id()));
}

static void sbi_ipi_handle(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	csr_clear(CSR_IP, IE_SIE);
	if (clint_ipi_base)
		clint_clear_ipi();

	ipi_mux_process();

	chained_irq_exit(chip, desc);
}

static int sbi_ipi_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(sbi_ipi_virq, irq_get_trigger_type(sbi_ipi_virq));
	return 0;
}

void __init sbi_ipi_init(void)
{
	int virq;
	struct irq_domain *domain;

	if (riscv_ipi_have_virq_range())
		return;

	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(),
					  DOMAIN_BUS_ANY);
	if (!domain) {
		pr_err("unable to find INTC IRQ domain\n");
		return;
	}

	sbi_ipi_virq = irq_create_mapping(domain, RV_IRQ_SOFT);
	if (!sbi_ipi_virq) {
		pr_err("unable to create INTC IRQ mapping\n");
		return;
	}

	virq = ipi_mux_create(BITS_PER_BYTE,  clint_ipi_base ?
				clint_send_ipi : sbi_send_ipi);
	if (virq <= 0) {
		pr_err("unable to create muxed IPIs\n");
		irq_dispose_mapping(sbi_ipi_virq);
		return;
	}

	irq_set_chained_handler(sbi_ipi_virq, sbi_ipi_handle);

	/*
	 * Don't disable IPI when CPU goes offline because
	 * the masking/unmasking of virtual IPIs is done
	 * via generic IPI-Mux
	 */
	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			  clint_ipi_base ? "irqchip/clint-ipi:starting"
			  : "irqchip/sbi-ipi:starting",
			  sbi_ipi_starting_cpu, NULL);

	riscv_ipi_set_virq_range(virq, BITS_PER_BYTE,
			clint_ipi_base ? true : false);

	pr_info("providing IPIs using %s IPI extension\n",
			clint_ipi_base ? "CLINT" : "SBI");
}

static int __init riscv_clint_probe(struct device_node *node,
				    struct device_node *parent)
{
	clint_ipi_base = of_iomap(node, 0);
	if (!clint_ipi_base) {
		pr_err("%pOFP: could not map registers\n", node);
		return -ENODEV;
	}

	pr_info("clint in s mode probe success: ipi base %px\n", clint_ipi_base);

	return 0;
}

IRQCHIP_DECLARE(thead_clint, "sophgo,sg2044-clint", riscv_clint_probe);
IRQCHIP_DECLARE(riscv_clint, "riscv,smode-clint", riscv_clint_probe);
