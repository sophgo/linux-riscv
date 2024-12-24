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

#define TPSYS0_MSG_REG_BASE 0x6908040000
#define TPSYS0_MSG_REG_SIZE 0x1000
#define INTR_CTL_REG_ADDR 0x464
#define ERROR1_REG0_ADDR 0x42c
#define ERROR1_REG1_ADDR 0x430
#define ERROR1_REG2_ADDR 0x434
#define ERROR1_REG3_ADDR 0x438
#define ERROR1_REG4_ADDR 0x43c

#define ERROR2_REG0_ADDR 0x41c
#define ERROR2_REG1_ADDR 0x420

#define ERROR3_REG0_ADDR 0x424
#define ERROR3_REG1_ADDR 0x428

#define ERROR4_REG0_ADDR 0x468

#define MSG_TABLE_CLR_REG_ADDR 0x410
#define MSG_TABLE_CLR_RANGE_REG_ADDR 0x414

#define TPUSYS_OFFSET 0x10000000

struct irq_info {
	int irq;
	int core_id;
	void __iomem *msg_reg_base;
};

static struct irq_info irq_info;

static irqreturn_t msg_err_intr_handler(int irqn, void *priv)
{
	struct irq_info *info = (struct irq_info *)priv;
	uint32_t status;
	uint32_t clr_flag = 0;
	// condition 1
	for (int i = 0; (ERROR1_REG0_ADDR + i * 4) <= ERROR1_REG4_ADDR; i++) {
		status =
			ioread32(info->msg_reg_base + ERROR1_REG0_ADDR + i * 4);
		if (status) {
			clr_flag |= 0x1 << 1;
			pr_err("MSG_CMD_CONFLICT: ERROR1_REG%d: 0x%x\n", i,
			       status);
		}
	}

	// condition 2
	for (int i = 0; (ERROR2_REG0_ADDR + i * 4) <= ERROR2_REG1_ADDR; i++) {
		status =
			ioread32(info->msg_reg_base + ERROR2_REG0_ADDR + i * 4);
		if (status) {
			clr_flag |= 0x1 << 3;
			pr_err("MSG_CMD_CONFLICT: ERROR2_REG%d: 0x%x\n", i,
			       status);
		}
	}

	// condition 3
	for (int i = 0; (ERROR3_REG0_ADDR + i * 4) <= ERROR3_REG1_ADDR; i++) {
		status =
			ioread32(info->msg_reg_base + ERROR3_REG0_ADDR + i * 4);
		if (status) {
			clr_flag |= 0x1 << 5;
			iowrite32(1, info->msg_reg_base + MSG_TABLE_CLR_REG_ADDR);
			iowrite32(0x2, info->msg_reg_base +
					       MSG_TABLE_CLR_RANGE_REG_ADDR);
			iowrite32(0x5 << 16, info->msg_reg_base +
					       MSG_TABLE_CLR_RANGE_REG_ADDR);
			pr_err("MSG_CMD_CONFLICT: ERROR3_REG%d: 0x%x\n", i,
			       status);
		}
	}

	// condition 4
	status = ioread32(info->msg_reg_base + ERROR4_REG0_ADDR);
	if (status) {
		clr_flag |= 0x1 << 7;
		pr_err("MSG_CMD_CONFLICT: ERROR4_REG0: 0x%x\n", status);
	}

	iowrite32(clr_flag, info->msg_reg_base + INTR_CTL_REG_ADDR);

	return IRQ_HANDLED;
}

static void intr_preset(struct irq_info *info)
{
	iowrite32(0x0, info->msg_reg_base + INTR_CTL_REG_ADDR);
}

static int msg_err_intr_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;
	void __iomem *clint_base = ioremap(CLINT_REG_MHART_ID, 0x100);

	irq_info.core_id = readl(clint_base);
	dev_info(&pdev->dev, "core_id: %d\n", irq_info.core_id);
	if (irq_info.core_id < 0 || irq_info.core_id > 7) {
		dev_err(&pdev->dev, "Failed to get core_id\n");
		return -EINVAL;
	}

	irq_info.irq = of_irq_get(np, 0);
	if (irq_info.irq <= 0) {
		dev_err(&pdev->dev, "Failed to map IRQ\n");
		return -EINVAL;
	}
	irq_info.msg_reg_base = devm_ioremap(
		&pdev->dev,
		TPSYS0_MSG_REG_BASE + TPUSYS_OFFSET * irq_info.core_id,
		TPSYS0_MSG_REG_SIZE);
	if (!irq_info.msg_reg_base) {
		dev_err(&pdev->dev, "Failed to map msg_reg_base\n");
		return -ENOMEM;
	}

	ret = devm_request_irq(&pdev->dev, irq_info.irq, msg_err_intr_handler,
			       0, "msg_conflict_intr", &irq_info);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ %d\n", irq_info.irq);
		return ret;
	}
	intr_preset(&irq_info);

	dev_info(&pdev->dev,
		 "Msg conflict intr registered successfully\n");
	return 0;
}

static void msg_err_intr_remove(struct platform_device *pdev)
{
	devm_free_irq(&pdev->dev, irq_info.irq, &irq_info);

	return;
}

static const struct of_device_id tpsys_err_intr_of_match[] = {
	{
		.compatible = "sophgo,msg-cmd-conflict-intr",
	},
	{},
};
MODULE_DEVICE_TABLE(of, tpsys_err_intr_of_match);

static struct platform_driver msg_err_intr_driver = {
	.driver =
		{
			.name = DRIVER_NAME,
			.of_match_table = tpsys_err_intr_of_match,
		},
	.probe = msg_err_intr_probe,
	.remove = msg_err_intr_remove,
};
module_platform_driver(msg_err_intr_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bowen.pang <bowen.pang01@sophgo.com>");
MODULE_DESCRIPTION("Msg Cmd Conflict Interrupt Driver");
