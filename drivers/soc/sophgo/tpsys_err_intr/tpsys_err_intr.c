// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "tpsys_err_intr"

#define CLINT_REG_MHART_ID 0x6844001000
#define TPSYS0_SYS_REG_BASE 0x6908050000
#define TPSYS0_SYS_REG_SIZE 0x1000
#define TPUSYS_ERR_INTR_STATUS_REG_ADDR 0x244
#define L2M_ERR_INT_ENABLE_REG_ADDR 0x274
#define L2M_ERR_INT_CLR_REG_ADDR 0x278
#define TPSYS0_GDMA_REG_BASE 0x6908010000
#define TPSYS0_GDMA_REG_SIZE 0x2000
#define GDMA_INTR_DISABLE_REG_ADDR 0x1124
#define GDMA_INTR_CLR_REG0_ADDR 0x1128
#define GDMA_INTR_CLR_REG1_ADDR 0x112c
#define GDMA_INTR_CLR_REG2_ADDR 0x1130

#define TPUSYS_OFFSET 0x10000000

enum tpsys_err_intr_type {
	ERR_TPU_INTR = 0,
	ERR_GDMA_ERR,
	ERR_SDMA_ERR,
	ERR_SORT_ERR,
	ERR_L2M0_ERR,
	ERR_L2M1_ERR,
	ERR_L2M2_ERR
};

const char *err_intr_name[] = {
	"ERR_TPU_INTR", "ERR_GDMA_ERR", "ERR_SDMA_ERR", "ERR_SORT_ERR", "ERR_L2M0_ERR", "ERR_L2M1_ERR", "ERR_L2M2_ERR",
};

struct irq_info {
	int irq;
	const char *name;
	void __iomem *tpsys_sys_reg_base;
	void __iomem *tpsys_gdma_reg_base;
};

static struct irq_info *irq_infos;
static int tpsys_err_intr_count;

static uint32_t err_intr_status(struct irq_info *info)
{
	return readl(info->tpsys_sys_reg_base + TPUSYS_ERR_INTR_STATUS_REG_ADDR);
}

static void clear_gdma_intr(struct irq_info *info)
{
	uint32_t status = readl(info->tpsys_gdma_reg_base + GDMA_INTR_CLR_REG0_ADDR);
	iowrite32(status, info->tpsys_gdma_reg_base + GDMA_INTR_CLR_REG0_ADDR);
	status = readl(info->tpsys_gdma_reg_base + GDMA_INTR_CLR_REG1_ADDR);
	iowrite32(status, info->tpsys_gdma_reg_base + GDMA_INTR_CLR_REG1_ADDR);
	status = readl(info->tpsys_gdma_reg_base + GDMA_INTR_CLR_REG2_ADDR);
	iowrite32(status, info->tpsys_gdma_reg_base + GDMA_INTR_CLR_REG2_ADDR);
}

static irqreturn_t tpsys_err_intr_handler(int irqn, void *priv)
{
	struct irq_info *info = (struct irq_info *)priv;
	uint32_t status = err_intr_status(info);

	for (int i = ERR_TPU_INTR; i <= ERR_L2M2_ERR; i++) {
		if (status & (1 << i)) {
			pr_info("%s: %s triggered\n", info->name, err_intr_name[i]);
			if (i == ERR_GDMA_ERR)
				clear_gdma_intr(info);
			else if (i >= ERR_L2M0_ERR && i <= ERR_L2M2_ERR)
				iowrite32(1 << (i - ERR_L2M0_ERR), info->tpsys_sys_reg_base + L2M_ERR_INT_CLR_REG_ADDR);
		}
	}

	return IRQ_HANDLED;
}

static void intr_preset(struct irq_info *info)
{
	uint32_t status;
	status = ioread32(info->tpsys_sys_reg_base);
	status |= 1 << 4;  // gdma enable
	iowrite32(status, info->tpsys_sys_reg_base);
	status = ioread32(info->tpsys_sys_reg_base + 0x4);
	status &= ~(1 << 5);  // gdma soft reset
	iowrite32(status, info->tpsys_sys_reg_base + 0x4);
	// enable gdma interrupt
	iowrite32(0, info->tpsys_gdma_reg_base + GDMA_INTR_DISABLE_REG_ADDR);
	// enable l2m interrupt
	iowrite32(0x7, info->tpsys_sys_reg_base + L2M_ERR_INT_ENABLE_REG_ADDR);
}

static int tpsys_err_intr_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int i, ret;

	tpsys_err_intr_count = of_irq_count(np);
	if (tpsys_err_intr_count <= 0 || tpsys_err_intr_count > 8) {
		dev_err(&pdev->dev, "Invalid irq numbers\n");
		return -ENODEV;
	}

	irq_infos = devm_kzalloc(&pdev->dev, tpsys_err_intr_count * sizeof(struct irq_info), GFP_KERNEL);
	if (!irq_infos) {
		dev_err(&pdev->dev, "Failed to allocate memory for IRQs\n");
		return -ENOMEM;
	}

	for (i = 0; i < tpsys_err_intr_count; i++) {
		irq_infos[i].irq = of_irq_get(np, i);
		if (irq_infos[i].irq <= 0) {
			dev_err(&pdev->dev, "Failed to map IRQ %d\n", i);
			return -EINVAL;
		}

		if (of_property_read_string_index(np, "interrupt-names", i, &(irq_infos[i].name)) != 0) {
			dev_err(&pdev->dev, "Failed to get name for IRQ %d\n", i);
			return -EINVAL;
		}

		irq_infos[i].tpsys_sys_reg_base =
			devm_ioremap(&pdev->dev, TPSYS0_SYS_REG_BASE + TPUSYS_OFFSET * i, TPSYS0_SYS_REG_SIZE);
		if (!irq_infos[i].tpsys_sys_reg_base) {
			dev_err(&pdev->dev, "Failed to map tpsys_sys_reg_base\n");
			return -ENOMEM;
		}

		irq_infos[i].tpsys_gdma_reg_base =
			devm_ioremap(&pdev->dev, TPSYS0_GDMA_REG_BASE + TPUSYS_OFFSET * i, TPSYS0_GDMA_REG_SIZE);
		if (!irq_infos[i].tpsys_gdma_reg_base) {
			dev_err(&pdev->dev, "Failed to map tpsys_gdma_reg_base\n");
			return -ENOMEM;
		}

		ret =
			devm_request_irq(&pdev->dev, irq_infos[i].irq, tpsys_err_intr_handler, 0, irq_infos[i].name, &irq_infos[i]);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request IRQ %d\n", irq_infos[i].irq);
			return ret;
		}
		intr_preset(&irq_infos[i]);
	}

	dev_info(&pdev->dev, "TPSYS error interrupts registered successfully\n");
	return 0;
}

static void tpsys_err_intr_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < tpsys_err_intr_count; i++)
		devm_free_irq(&pdev->dev, irq_infos[i].irq, &irq_infos[i]);

	return;
}

static const struct of_device_id tpsys_err_intr_of_match[] = {
	{
		.compatible = "sophgo,tpsys-err-intr",
	},
	{},
};
MODULE_DEVICE_TABLE(of, tpsys_err_intr_of_match);

static struct platform_driver tpsys_err_intr_driver = {
	.driver =
		{
			.name = DRIVER_NAME,
			.of_match_table = tpsys_err_intr_of_match,
		},
	.probe = tpsys_err_intr_probe,
	.remove = tpsys_err_intr_remove,
};
module_platform_driver(tpsys_err_intr_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bowen.pang <bowen.pang01@sophgo.com>");
MODULE_DESCRIPTION("TPSYS Error Interrupt Driver");
