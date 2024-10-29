#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/acpi.h>

#include "../pinctrl-utils.h"
#include "pinctrl-sophgo.h"

#define DRV_PINCTRL_NAME "sg2044_pinctrl"
#define DRV_PINMUX_NAME "sg2044_pinmux"

#define FUNCTION(fname, gname, fmode) \
	{ \
		.name = #fname, \
		.groups = gname##_group, \
		.num_groups = ARRAY_SIZE(gname##_group), \
		.mode = fmode, \
	}

#define PIN_GROUP(gname) \
	{ \
		.name = #gname "_grp", \
		.pins = gname##_pins, \
		.num_pins = ARRAY_SIZE(gname##_pins), \
	}

static const struct pinctrl_pin_desc sg2044_pins[] = {
	PINCTRL_PIN(0,   "MIO0"),
	PINCTRL_PIN(1,   "MIO1"),
	PINCTRL_PIN(2,   "MIO2"),
	PINCTRL_PIN(3,   "MIO3"),
	PINCTRL_PIN(4,   "MIO4"),
	PINCTRL_PIN(5,   "MIO5"),
	PINCTRL_PIN(6,   "MIO6"),
	PINCTRL_PIN(7,   "MIO7"),
	PINCTRL_PIN(8,   "MIO8"),
	PINCTRL_PIN(9,   "MIO9"),
	PINCTRL_PIN(10,   "MIO10"),
	PINCTRL_PIN(11,   "MIO11"),
	PINCTRL_PIN(12,   "MIO12"),
	PINCTRL_PIN(13,   "MIO13"),
	PINCTRL_PIN(14,   "MIO14"),
	PINCTRL_PIN(15,   "MIO15"),
	PINCTRL_PIN(16,   "MIO16"),
	PINCTRL_PIN(17,   "MIO17"),
	PINCTRL_PIN(18,   "MIO18"),
	PINCTRL_PIN(19,   "MIO19"),
	PINCTRL_PIN(20,   "MIO20"),
	PINCTRL_PIN(21,   "MIO21"),
	PINCTRL_PIN(22,   "MIO22"),
	PINCTRL_PIN(23,   "MIO23"),
	PINCTRL_PIN(24,   "MIO24"),
	PINCTRL_PIN(25,   "MIO25"),
	PINCTRL_PIN(26,   "MIO26"),
	PINCTRL_PIN(27,   "MIO27"),
	PINCTRL_PIN(28,   "MIO28"),
	PINCTRL_PIN(29,   "MIO29"),
	PINCTRL_PIN(30,   "MIO30"),
	PINCTRL_PIN(31,   "MIO31"),
	PINCTRL_PIN(32,   "MIO32"),
	PINCTRL_PIN(33,   "MIO33"),
	PINCTRL_PIN(34,   "MIO34"),
	PINCTRL_PIN(35,   "MIO35"),
	PINCTRL_PIN(36,   "MIO36"),
	PINCTRL_PIN(37,   "MIO37"),
	PINCTRL_PIN(38,   "MIO38"),
	PINCTRL_PIN(39,   "MIO39"),
	PINCTRL_PIN(40,   "MIO40"),
	PINCTRL_PIN(41,   "MIO41"),
	PINCTRL_PIN(42,   "MIO42"),
	PINCTRL_PIN(43,   "MIO43"),
	PINCTRL_PIN(44,   "MIO44"),
	PINCTRL_PIN(45,   "MIO45"),
	PINCTRL_PIN(46,   "MIO46"),
	PINCTRL_PIN(47,   "MIO47"),
	PINCTRL_PIN(48,   "MIO48"),
	PINCTRL_PIN(49,   "MIO49"),
	PINCTRL_PIN(50,   "MIO50"),
	PINCTRL_PIN(51,   "MIO51"),
	PINCTRL_PIN(52,   "MIO52"),
	PINCTRL_PIN(53,   "MIO53"),
	PINCTRL_PIN(54,   "MIO54"),
	PINCTRL_PIN(55,   "MIO55"),
	PINCTRL_PIN(56,   "MIO56"),
	PINCTRL_PIN(57,   "MIO57"),
	PINCTRL_PIN(58,   "MIO58"),
	PINCTRL_PIN(59,   "MIO59"),
	PINCTRL_PIN(60,   "MIO60"),
	PINCTRL_PIN(61,   "MIO61"),
	PINCTRL_PIN(62,   "MIO62"),
	PINCTRL_PIN(63,   "MIO63"),
};

static const unsigned int smbus0_pins[] = {0, 1, 2};    //GPIO32~34
static const unsigned int smbus1_pins[] = {3, 4, 5};    //GPIO35~37
static const unsigned int smbus2_pins[] = {6, 7, 8};    //GPIO38~40
static const unsigned int smbus3_pins[] = {9, 10, 11};  //GPIO41~43
static const unsigned int pwm2_pins[] = {12};				//GPIO45
static const unsigned int pwm3_pins[] = {13};				//GPIO46
static const unsigned int fan2_pins[] = {14};				//GPIO47
static const unsigned int fan3_pins[] = {15};				//GPIO48
static const unsigned int iic0_pins[] = {16, 17};			//GPIO49~50
static const unsigned int iic1_pins[] = {18, 19};			//GPIO51~52
static const unsigned int iic2_pins[] = {20, 21};			//GPIO53~54
static const unsigned int iic3_pins[] = {22, 23};			//GPIO55~56
static const unsigned int uart0_pins[] = {24, 25};			//GPIO57~58
static const unsigned int uart1_pins[] = {26, 27};			//GPIO59~60
static const unsigned int uart2_pins[] = {28, 29};			//GPIO61~62
static const unsigned int uart3_pins[] = {30, 31};			//GPIO63~64
static const unsigned int spi0_pins[] = {32, 33, 34, 35, 36}; //GPIO65~69
static const unsigned int spi1_pins[] = {37, 38, 39, 40, 41}; //GPIO70~74
static const unsigned int jtag0_pins[] = {42, 43, 44, 45, 46, 47}; //GPIO75~80
static const unsigned int jtag1_pins[] = {48, 49, 50, 51, 52, 53}; //GPIO81~86
static const unsigned int jtag2_pins[] = {54, 55, 56, 57, 58, 59}; //GPIO87~92
static const unsigned int plllock_pins[] = {60};			//GPIO4
static const unsigned int dbgiic_pins[] = {61, 62, 63};		//GPIO29~31


static const char * const smbus0_group[] = {"smbus0_grp"};
static const char * const smbus1_group[] = {"smbus1_grp"};
static const char * const smbus2_group[] = {"smbus2_grp"};
static const char * const smbus3_group[] = {"smbus3_grp"};
static const char * const pwm2_group[] = {"pwm2_grp"};
static const char * const pwm3_group[] = {"pwm3_grp"};
static const char * const fan2_group[] = {"fan2_grp"};
static const char * const fan3_group[] = {"fan3_grp"};
static const char * const iic0_group[] = {"iic0_grp"};
static const char * const iic1_group[] = {"iic1_grp"};
static const char * const iic2_group[] = {"iic2_grp"};
static const char * const iic3_group[] = {"iic3_grp"};
static const char * const uart0_group[] = {"uart0_grp"};
static const char * const uart1_group[] = {"uart1_grp"};
static const char * const uart2_group[] = {"uart2_grp"};
static const char * const uart3_group[] = {"uart3_grp"};
static const char * const spi0_group[] = {"spi0_grp"};
static const char * const spi1_group[] = {"spi1_grp"};
static const char * const jtag0_group[] = {"jtag0_grp"};
static const char * const jtag1_group[] = {"jtag1_grp"};
static const char * const jtag2_group[] = {"jtag2_grp"};
static const char * const plllock_group[] = {"plllock_grp"};
static const char * const dbgiic_group[] = {"dbgiic_grp"};

static struct sg_group sg2044_groups[] = {
	PIN_GROUP(smbus0),
	PIN_GROUP(smbus1),
	PIN_GROUP(smbus2),
	PIN_GROUP(smbus3),
	PIN_GROUP(pwm2),
	PIN_GROUP(pwm3),
	PIN_GROUP(fan2),
	PIN_GROUP(fan3),
	PIN_GROUP(iic0),
	PIN_GROUP(iic1),
	PIN_GROUP(iic2),
	PIN_GROUP(iic3),
	PIN_GROUP(uart0),
	PIN_GROUP(uart1),
	PIN_GROUP(uart2),
	PIN_GROUP(uart3),
	PIN_GROUP(spi0),
	PIN_GROUP(spi1),
	PIN_GROUP(jtag0),
	PIN_GROUP(jtag1),
	PIN_GROUP(jtag2),
	PIN_GROUP(plllock),
	PIN_GROUP(dbgiic),
};

static const struct sg_pmx_func sg2044_funcs[] = {
	FUNCTION(smbus0_a, smbus0, FUNC_MODE0),
	FUNCTION(smbus0_r, smbus0, FUNC_MODE1),
	FUNCTION(smbus1_a, smbus1, FUNC_MODE0),
	FUNCTION(smbus1_r, smbus1, FUNC_MODE1),
	FUNCTION(smbus2_a, smbus2, FUNC_MODE0),
	FUNCTION(smbus2_r, smbus2, FUNC_MODE1),
	FUNCTION(smbus3_a, smbus3, FUNC_MODE0),
	FUNCTION(smbus3_r, smbus3, FUNC_MODE1),
	FUNCTION(pwm2_a, pwm2, FUNC_MODE0),
	FUNCTION(pwm2_r, pwm2, FUNC_MODE1),
	FUNCTION(pwm3_a, pwm3, FUNC_MODE0),
	FUNCTION(pwm3_r, pwm3, FUNC_MODE1),
	FUNCTION(fan2_a, fan2, FUNC_MODE0),
	FUNCTION(fan2_r, fan2, FUNC_MODE1),
	FUNCTION(fan3_a, fan3, FUNC_MODE0),
	FUNCTION(fan3_r, fan3, FUNC_MODE1),
	FUNCTION(iic0_a, iic0, FUNC_MODE0),
	FUNCTION(iic0_r, iic0, FUNC_MODE1),
	FUNCTION(iic1_a, iic1, FUNC_MODE0),
	FUNCTION(iic1_r, iic1, FUNC_MODE1),
	FUNCTION(iic2_a, iic2, FUNC_MODE0),
	FUNCTION(iic2_r, iic2, FUNC_MODE1),
	FUNCTION(iic3_a, iic3, FUNC_MODE0),
	FUNCTION(iic3_r, iic3, FUNC_MODE1),
	FUNCTION(uart0_a, uart0, FUNC_MODE0),
	FUNCTION(uart0_r, uart0, FUNC_MODE1),
	FUNCTION(uart1_a, uart1, FUNC_MODE0),
	FUNCTION(uart1_r, uart1, FUNC_MODE1),
	FUNCTION(uart2_a, uart2, FUNC_MODE0),
	FUNCTION(uart2_r, uart2, FUNC_MODE1),
	FUNCTION(uart3_a, uart3, FUNC_MODE0),
	FUNCTION(uart3_r, uart3, FUNC_MODE1),
	FUNCTION(jtag0_a, jtag0, FUNC_MODE0),
	FUNCTION(jtag0_r, jtag0, FUNC_MODE1),
	FUNCTION(jtag1_a, jtag1, FUNC_MODE0),
	FUNCTION(jtag1_r, jtag1, FUNC_MODE1),
	FUNCTION(jtag2_a, jtag2, FUNC_MODE0),
	FUNCTION(jtag2_r, jtag2, FUNC_MODE1),
	FUNCTION(plllock_a, plllock, FUNC_MODE0),
	FUNCTION(plllock_r, plllock, FUNC_MODE1),
	FUNCTION(dbgiic_a, dbgiic, FUNC_MODE0),
	FUNCTION(dbgiic_r, dbgiic, FUNC_MODE1),
};

static struct device_attribute smbus0_attr =	__ATTR(smbus0, 0664, pinmux_show, pinmux_store);
static struct device_attribute smbus1_attr =	__ATTR(smbus1, 0664, pinmux_show, pinmux_store);
static struct device_attribute smbus2_attr =	__ATTR(smbus2, 0664, pinmux_show, pinmux_store);
static struct device_attribute smbus3_attr =	__ATTR(smbus3, 0664, pinmux_show, pinmux_store);
static struct device_attribute pwm2_attr =	__ATTR(pwm2, 0664, pinmux_show, pinmux_store);
static struct device_attribute pwm3_attr =	__ATTR(pwm3, 0664, pinmux_show, pinmux_store);
static struct device_attribute fan2_attr =	__ATTR(fan2, 0664, pinmux_show, pinmux_store);
static struct device_attribute fan3_attr =	__ATTR(fan3, 0664, pinmux_show, pinmux_store);
static struct device_attribute iic0_attr =	__ATTR(iic0, 0664, pinmux_show, pinmux_store);
static struct device_attribute iic1_attr =	__ATTR(iic1, 0664, pinmux_show, pinmux_store);
static struct device_attribute iic2_attr =	__ATTR(iic2, 0664, pinmux_show, pinmux_store);
static struct device_attribute iic3_attr =	__ATTR(iic3, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart0_attr =	__ATTR(uart0, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart1_attr =	__ATTR(uart1, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart2_attr =	__ATTR(uart2, 0664, pinmux_show, pinmux_store);
static struct device_attribute uart3_attr =	__ATTR(uart3, 0664, pinmux_show, pinmux_store);
static struct device_attribute jtag0_attr =	__ATTR(jtag0, 0664, pinmux_show, pinmux_store);
static struct device_attribute jtag1_attr =	__ATTR(jtag1, 0664, pinmux_show, pinmux_store);
static struct device_attribute jtag2_attr =	__ATTR(jtag2, 0664, pinmux_show, pinmux_store);
static struct device_attribute plllock_attr =	__ATTR(plllock, 0664, pinmux_show, pinmux_store);
static struct device_attribute dbgiic_attr =	__ATTR(dbgiic, 0664, pinmux_show, pinmux_store);

static struct attribute *pinmux_attrs[] = {
	&smbus0_attr.attr,
	&smbus1_attr.attr,
	&smbus2_attr.attr,
	&smbus3_attr.attr,
	&pwm2_attr.attr,
	&pwm3_attr.attr,
	&fan2_attr.attr,
	&fan3_attr.attr,
	&iic0_attr.attr,
	&iic1_attr.attr,
	&iic2_attr.attr,
	&iic3_attr.attr,
	&uart0_attr.attr,
	&uart1_attr.attr,
	&uart2_attr.attr,
	&uart3_attr.attr,
	&jtag0_attr.attr,
	&jtag1_attr.attr,
	&jtag2_attr.attr,
	&plllock_attr.attr,
	&dbgiic_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pinmux);

static struct class  pinmux_class = {
	.name = "pinmux",
	.dev_groups = pinmux_groups,
};

static struct sg_soc_pinctrl_data sg2044_pinctrl_data = {
	.pins = sg2044_pins,
	.npins = ARRAY_SIZE(sg2044_pins),
	.groups = sg2044_groups,
	.groups_count = ARRAY_SIZE(sg2044_groups),
	.functions = sg2044_funcs,
	.functions_count = ARRAY_SIZE(sg2044_funcs),
	.p_class = &pinmux_class,
};

static const struct of_device_id sg2044_pinctrl_of_table[] = {
	{
		.compatible = "sophgo, pinctrl-sg2044",
		.data = &sg2044_pinctrl_data,
	},
	{},
};

static const struct acpi_device_id pinctrl_acpi_match[] = {
	{
		.id = "SOPH0010",
		.driver_data = (kernel_ulong_t)&sg2044_pinctrl_data,
	},
	{},
};
MODULE_DEVICE_TABLE(acpi, pinctrl_acpi_match);

static int sg2044_pinctrl_probe(struct platform_device *pdev)
{
	return sophgo_pinctrl_probe(pdev);
}

static struct platform_driver sg2044_pinctrl_driver = {
	.probe = sg2044_pinctrl_probe,
	.driver = {
		.name = DRV_PINCTRL_NAME,
		.of_match_table = of_match_ptr(sg2044_pinctrl_of_table),
		.acpi_match_table = ACPI_PTR(pinctrl_acpi_match),
	},
};

static int __init sg2044_pinctrl_init(void)
{
	return platform_driver_register(&sg2044_pinctrl_driver);
}
postcore_initcall(sg2044_pinctrl_init);
