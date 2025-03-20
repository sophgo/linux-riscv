// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_irq.h>

struct cpu_idle_device_data {
	int irq;
};

static irqreturn_t
cpu_idle_interrupt_handler(int irq, void *dev_id)
{
	pr_info("Hello! Interrupt triggered: IRQ %d\n", irq);
	while (true)
		asm volatile ("wfi");

	return IRQ_HANDLED;
}

static int cpu_idle_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpu_idle_device_data *data;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->irq = irq;
	platform_set_drvdata(pdev, data);

	ret = devm_request_irq(dev, irq, cpu_idle_interrupt_handler,
			       IRQF_TRIGGER_HIGH, "cpu_idle_irq", data);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", irq, ret);
		return ret;
	}

	dev_info(dev, "Cpu Idle Driver initialized, IRQ %d registered\n", irq);
	return 0;
}

static const struct of_device_id cpu_idle_dt_ids[] = {
	{.compatible = "sophgo, cpu-idle-intr"},
	{}
};

MODULE_DEVICE_TABLE(of, cpu_idle_dt_ids);

static struct platform_driver cpu_idle_driver = {
	.driver = {
		   .name = "cpu_idle_irq_driver",
		   .of_match_table = cpu_idle_dt_ids,
		   },
	.probe = cpu_idle_probe,
};

module_platform_driver(cpu_idle_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("le.yang");
MODULE_DESCRIPTION("A interrupt-driven kernel module make cpu idle");
