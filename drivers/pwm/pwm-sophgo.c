#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/acpi.h>

#define REG_HLPERIOD		0x0
#define REG_PERIOD		0x4
#define REG_GROUP		0x8
#define REG_FREQNUM		0x20
#define REG_FREQDATA		0x24
#define REG_POLARITY		0x40

#define REG_PWMSTART		0x44
#define REG_PWMUPDATE		0x4C
#define REG_SHIFTCOUNT		0x80
#define REG_SHIFTSTART		0x90
#define REG_FREQEN		0x9C
#define REG_FREQ_DONE_NUM	0xC0
#define REG_PWM_OE		0xD0

#define PWM_REG_NUM		0x80
#define PWM_NPWM		4
/**
 * struct sophgo_pwm_channel - private data of PWM channel
 * @period_ns:	current period in nanoseconds programmed to the hardware
 * @duty_ns:	current duty time in nanoseconds programmed to the hardware
 * @tin_ns:	time of one timer tick in nanoseconds with current timer rate
 */
struct sophgo_pwm_channel {
	u32 period;
	u32 hlperiod;
};

/**
 * struct sophgo_pwm_chip - private data of PWM chip
 * @chip:		generic PWM chip
 * @variant:		local copy of hardware variant data
 * @inverter_mask:	inverter status for all channels - one bit per channel
 * @base:		base address of mapped PWM registers
 * @base_clk:		base clock used to drive the timers
 * @tclk0:		external clock 0 (can be ERR_PTR if not present)
 * @tclk1:		external clock 1 (can be ERR_PTR if not present)
 */
struct sophgo_pwm_chip {
//	struct pwm_chip chip;
	void __iomem *base;
	struct clk *base_clk;
	u64 clock;
	u8 polarity_mask;
	bool no_polarity;
	uint32_t pwm_saved_regs[PWM_REG_NUM];
	struct sophgo_pwm_channel channel;
};


static inline
struct sophgo_pwm_chip *to_sophgo_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static unsigned long sophgo_pwm_get_base_clock(struct sophgo_pwm_chip *sophgo_chip)
{
	if (sophgo_chip->base_clk)
		return clk_get_rate(sophgo_chip->base_clk);
	else
		return sophgo_chip->clock;
}

static int pwm_sophgo_request(struct pwm_chip *chip, struct pwm_device *pwm_dev)
{
	struct sophgo_pwm_chip* sophgo_chip = pwmchip_get_drvdata(chip);
	memset(&sophgo_chip->channel, 0, sizeof(sophgo_chip->channel));
	return 0;
}


static int pwm_sophgo_config(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			     int duty_ns, int period_ns)
{
	struct sophgo_pwm_chip *our_chip = to_sophgo_pwm_chip(chip);
	struct sophgo_pwm_channel *channel = &our_chip->channel;
	u64 cycles;

	cycles = sophgo_pwm_get_base_clock(our_chip);
	pr_debug("clk_get_rate=%llu\n", cycles);

	cycles *= period_ns;
	do_div(cycles, NSEC_PER_SEC);

	channel->period = cycles;
	cycles = cycles * duty_ns;
	do_div(cycles, period_ns);

	if (cycles == 0)
		cycles = 1;
	if (cycles == channel->period)
		cycles = channel->period - 1;

	channel->hlperiod = channel->period - cycles;

	pr_debug("period_ns=%d, duty_ns=%d, period=%d, hlperiod=%d\n",
			period_ns, duty_ns, channel->period, channel->hlperiod);

	return 0;
}

static int pwm_sophgo_enable(struct pwm_chip *chip, struct pwm_device *pwm_dev)
{
	struct sophgo_pwm_chip *our_chip = to_sophgo_pwm_chip(chip);
	struct sophgo_pwm_channel *channel = &our_chip->channel;
	uint32_t pwm_start_value;
	uint32_t value;

	writel(channel->period, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_PERIOD);
	if (channel->hlperiod != 0)
		writel(channel->hlperiod, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_HLPERIOD);

	pwm_start_value = readl(our_chip->base + REG_PWMSTART);

	writel(pwm_start_value & (~(1 << (pwm_dev->hwpwm))), our_chip->base + REG_PWMSTART);

	value = pwm_start_value | (1 << pwm_dev->hwpwm);
	pr_debug("pwm_sophgo_enable: value = %x\n", value);

	writel(value, our_chip->base + REG_PWM_OE);
	writel(value, our_chip->base + REG_PWMSTART);

	return 0;
}

static void pwm_sophgo_disable(struct pwm_chip *chip,
			       struct pwm_device *pwm_dev)
{
	struct sophgo_pwm_chip *our_chip = to_sophgo_pwm_chip(chip);
	uint32_t value;

	value = readl(our_chip->base + REG_PWMSTART) & (~(1 << (pwm_dev->hwpwm)));
	pr_debug("pwm_sophgo_disable: value = %x\n", value);
	writel(value, our_chip->base + REG_PWM_OE);
	writel(value, our_chip->base + REG_PWMSTART);

	writel(1, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_PERIOD);
	writel(2, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_HLPERIOD);
}

static int pwm_sophgo_set_polarity(struct pwm_chip *chip,
				    struct pwm_device *pwm_dev,
				    enum pwm_polarity polarity)
{
	struct sophgo_pwm_chip *our_chip = to_sophgo_pwm_chip(chip);

	if (our_chip->no_polarity) {
		dev_err(&chip->dev, "no polarity\n");
		return -ENOTSUPP;
	}

	if (polarity == PWM_POLARITY_NORMAL)
		our_chip->polarity_mask &= ~(1 << pwm_dev->hwpwm);
	else
		our_chip->polarity_mask |= 1 << pwm_dev->hwpwm;

	writel(our_chip->polarity_mask, our_chip->base + REG_POLARITY);

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static int pwm_sophgo_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
#else
static int pwm_sophgo_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      struct pwm_state *state)
#endif
{
	int ret;

	ret = pwm_sophgo_config(chip, pwm, state->duty_cycle, state->period);
	if (ret) {
		dev_err(&chip->dev, "pwm apply err\n");
		return ret;
	}

	ret = pwm_sophgo_set_polarity(chip, pwm, state->polarity);
	if (ret) {
		dev_err(&chip->dev, "pwm apply err\n");
		return ret;
	}

	dev_dbg(&chip->dev, "pwm_sophgo_apply state->enabled = %d\n", state->enabled);
	if (state->enabled)
		ret = pwm_sophgo_enable(chip, pwm);
	else
		pwm_sophgo_disable(chip, pwm);

	if (ret) {
		dev_err(&chip->dev, "pwm apply failed\n");
		return ret;
	}
	return ret;
}

static int pwm_sophgo_capture(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			   struct pwm_capture *result, unsigned long timeout)
{
	struct sophgo_pwm_chip *our_chip = to_sophgo_pwm_chip(chip);
	uint32_t value;
	u64 cycles;
	u64 cycle_cnt;

	// Set corresponding bit in PWM_OE to 0
	value = readl(our_chip->base + REG_PWM_OE) & (~(1 << (pwm_dev->hwpwm)));
	writel(value, our_chip->base + REG_PWM_OE);
	pr_debug("pwm_sophgo_capture: REG_PWM_OE = %x\n", value);

	// Enable capture
	writel(1, our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_FREQNUM);
	writel(1 << pwm_dev->hwpwm, our_chip->base + REG_FREQEN);
	pr_debug("pwm_sophgo_capture: REG_FREQEN = %x\n", readl(our_chip->base + REG_FREQEN));

	// Wait for done status
	while (timeout--) {
		mdelay(1);
		pr_debug("delay 1ms\n");
		value = readl(our_chip->base + REG_FREQ_DONE_NUM + pwm_dev->hwpwm * 4);
		if (value != 0)
			break;
	}

	// Read cycle count
	cycle_cnt = readl(our_chip->base + REG_GROUP * pwm_dev->hwpwm + REG_FREQDATA) + 1;
	pr_debug("pwm_sophgo_capture: cycle_cnt = %llu\n", cycle_cnt);

	// Convert from cycle count to period ns
	cycles = sophgo_pwm_get_base_clock(our_chip);
	cycle_cnt *= NSEC_PER_SEC;
	do_div(cycle_cnt, cycles);

	result->period = cycle_cnt;
	result->duty_cycle = 0;

	// Disable capture
	value = readl(our_chip->base + REG_FREQEN) & (~(1 << (pwm_dev->hwpwm)));
	writel(value, our_chip->base + REG_FREQEN);

	return 0;
}

static const struct pwm_ops pwm_sophgo_ops = {
	.request	= pwm_sophgo_request,
	.enable		= pwm_sophgo_enable,
	.disable	= pwm_sophgo_disable,
	.config		= pwm_sophgo_config,
	.set_polarity	= pwm_sophgo_set_polarity,
	.apply		= pwm_sophgo_apply,
	.capture	= pwm_sophgo_capture,
	.owner		= THIS_MODULE,
};

static const struct of_device_id sophgo_pwm_match[] = {
	{
		.compatible = "sophgo,sg-pwm",
		.data = &pwm_sophgo_ops,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sophgo_pwm_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id pwm_sophgo_acpi_ids[] = {
	{
		.id = "SOPH0007",
		.driver_data = (kernel_ulong_t)&pwm_sophgo_ops
	},
	{},
};
MODULE_DEVICE_TABLE(acpi, pwm_sophgo_acpi_ids);
#endif

static int pwm_sophgo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pwm_chip *sophgo_chip;
	struct pwm_chip *chip;
	int ret;

	pr_debug("%s\n", __func__);

	// fixed npwm to 4.
	chip = devm_pwmchip_alloc(dev, PWM_NPWM, sizeof(*sophgo_chip));
	if (IS_ERR(chip))
		return -PTR_ERR(chip);

	sophgo_chip = (struct sophgo_pwm_chip*) pwmchip_get_drvdata(chip);
	chip->ops = &pwm_sophgo_ops;
	sophgo_chip->polarity_mask = 0;
	chip->of_xlate = of_pwm_xlate_with_flags;

	sophgo_chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sophgo_chip->base))
		return PTR_ERR(sophgo_chip->base);
	if (dev->of_node) {
		sophgo_chip->base_clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(sophgo_chip->base_clk)) {
			dev_err(dev, "failed to get pwm source clk\n");
			return PTR_ERR(sophgo_chip->base_clk);
		}

		ret = clk_prepare_enable(sophgo_chip->base_clk);
		if (ret < 0) {
			dev_err(dev, "failed to enable base clock\n");
			return ret;
		}
	} else {
		ret = device_property_read_u64(dev, "base-clk", &sophgo_chip->clock);
		if (ret < 0) {
			dev_err(dev, "failed to get base clock from ACPI Table\n");
			return ret;
		}
	}

	if (of_property_read_bool(pdev->dev.of_node, "no-polarity") || \
		device_property_read_bool(dev, "no-polarity"))
		sophgo_chip->no_polarity = true;
	else
		sophgo_chip->no_polarity = false;
	// pr_debug("chip->chip.npwm = %d  chip->no_polarity = %d\n", chip->chip.npwm, chip->no_polarity);

	platform_set_drvdata(pdev, chip);

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(dev, "failed to register PWM chip\n");
		if (dev->of_node)
			clk_disable_unprepare(sophgo_chip->base_clk);
		return ret;
	}

	return 0;
}

static void pwm_sophgo_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip = platform_get_drvdata(pdev);
	struct sophgo_pwm_chip *sophgo_chip = pwmchip_get_drvdata(chip);

	pwmchip_remove(chip);
	if (dev->of_node)
		clk_disable_unprepare(sophgo_chip->base_clk);
}

#ifdef CONFIG_PM_SLEEP
static int pwm_sophgo_suspend(struct device *dev)
{
	struct sophgo_pwm_chip *chip = dev_get_drvdata(dev);

	memcpy_fromio(chip->pwm_saved_regs, chip->base, PWM_REG_NUM * 4);

	return 0;
}

static int pwm_sophgo_resume(struct device *dev)
{
	struct sophgo_pwm_chip *chip = dev_get_drvdata(dev);

	memcpy_toio(chip->base, chip->pwm_saved_regs, PWM_REG_NUM * 4);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pwm_sophgo_pm_ops, pwm_sophgo_suspend,
			 pwm_sophgo_resume);

static struct platform_driver pwm_sophgo_driver = {
	.driver		= {
		.name	= "sophgo-pwm",
		.pm	= &pwm_sophgo_pm_ops,
		.of_match_table = of_match_ptr(sophgo_pwm_match),
		.acpi_match_table = ACPI_PTR(pwm_sophgo_acpi_ids),
	},
	.probe		= pwm_sophgo_probe,
	.remove		= pwm_sophgo_remove,
};
module_platform_driver(pwm_sophgo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kun.Chang");
MODULE_DESCRIPTION("Sophgo PWM driver");
