// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/sbitmap.h>
#include "c2c_rc.h"

int sophgo_dw_pcie_probe(struct platform_device *pdev);
void sophgo_pcie_remove(struct platform_device *pdev);
int sophgo_set_all_c2c_rc_num(uint64_t all_rc);
void sophgo_rest_c2c(void);

#define PER_CONFIG_STR_OFFSET	(0x1000)
#define PCIE_INFO_DEF_VAL	0x5a

#define PCIE_DISABLE	0
#define PCIE_ENABLE	1

#define PCIE_DATA_LINK_PCIE	0
#define PCIE_DATA_LINK_C2C	1

#define PCIE_LINK_ROLE_RC	0
#define PCIE_LINK_ROLE_EP	1
#define PCIE_LINK_ROLE_RCEP	2

#define RC_GPIO_LEVEL	1
#define EP_GPIO_LEVEL	0

#define PCIE_PHY_X8	0
#define PCIE_PHY_X44	1

#define CHIP_ID_MAX	5

#define PCIE_BUS_MAX	10

static int c2c_enable;
static uint64_t all_c2c_rc;
static atomic_t ready_c2c_rc;
static uint8_t c2c_link_status[PCIE_BUS_MAX];
static uint8_t want_c2c_link[PCIE_BUS_MAX];

struct pcie_info {
	uint64_t slot_id;
	uint64_t socket_id;
	uint64_t pcie_id;
	uint64_t send_port;
	uint64_t recv_port;
	uint64_t enable;
	uint64_t data_link_type;
	uint64_t link_role;
	uint64_t link_role_gpio;
	uint64_t perst_gpio;
	uint64_t phy_role;
	uint64_t peer_slotid;
	uint64_t peer_socketid;
	uint64_t peer_pcie_id;
};

struct c2c_info {
	uint64_t pcie_info_addr;
	uint64_t pcie_info_size;
	void __iomem *pcie_info;
};

struct c2c_chip_info {
	uint64_t pcie_info_addr;
	uint64_t pcie_info_size;
};

static const struct of_device_id sophgo_dw_c2c_pcie_of_match[] = {
	{ .compatible = "sophgo,bm1690-c2c-pcie-host", },
	{},
};

static struct platform_driver sophgo_dw_c2c_pcie_driver = {
	.driver = {
		.name	= "sophgo-dw-pcie-c2c",
		.of_match_table = sophgo_dw_c2c_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = sophgo_dw_pcie_probe,
	.remove = sophgo_pcie_remove,
};

static struct platform_driver *const c2c_drivers[] = {
	&sophgo_dw_c2c_pcie_driver,
};

static ssize_t c2c_enable_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	char buf[32] = {0};
	int enable;
	int ret;

	pr_err("c2c enable store\n");
	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &enable);

	if (enable == 1 && c2c_enable != 1) {
		pr_err("enable c2c\n");
		c2c_enable = enable;
		platform_register_drivers(c2c_drivers, 1);

		return len;
	} else if (enable == 0 && c2c_enable != 0) {
		pr_err("disable c2c\n");
		c2c_enable = enable;

		platform_unregister_drivers(c2c_drivers, 1);

		return len;
	}

	pr_err("please echo 0 for disable and 1 for enable, now c2c is %s\n", c2c_enable == 1 ? "enable" : "disable");

	return -EINVAL;
}

static ssize_t c2c_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	if (c2c_enable) {
		sprintf(buf, "c2c enable\n");
		return strlen(buf);
	}

	sprintf(buf, "c2c disable\n");
	return strlen(buf);
}

static int dump_pcie_info(struct pcie_info *pcie_info)
{
	pr_err("pcie[%llu]  status:%s\n", pcie_info->pcie_id, pcie_info->enable == 1 ? "enable": "disable");
	pr_err("	slot id:0x%llx\n", pcie_info->slot_id);
	pr_err("	socket id:0x%llx\n", pcie_info->socket_id);
	pr_err("	send port:0x%llx, recv port:0x%llx\n", pcie_info->send_port, pcie_info->recv_port);
	pr_err("	data link type:%s, link role:%s, gpio:%llu\n",
		pcie_info->data_link_type == PCIE_DATA_LINK_C2C ?
		"c2c" : (pcie_info->data_link_type == PCIE_LINK_ROLE_RC ? "cascade" : "error"),
		pcie_info->link_role == PCIE_LINK_ROLE_RC ? "rc" : (pcie_info->link_role == PCIE_LINK_ROLE_EP ?
		"ep" : "error"),
		pcie_info->link_role_gpio);
	pr_err("	perst gpio:%llu\n", pcie_info->perst_gpio);
	pr_err("	peer slot id:%llu, peer socket id:%llu, peer pcie id:%llu\n\n",
		pcie_info->peer_slotid, pcie_info->peer_socketid, pcie_info->peer_pcie_id);

	return 0;
}

static int check_all_rc(struct c2c_info *info)
{
	struct pcie_info *pcie_info;

	for (int i = 0; i < 10; i++) {
		pcie_info = (struct pcie_info *)(info->pcie_info + i * PER_CONFIG_STR_OFFSET);
		dump_pcie_info(pcie_info);
		if (pcie_info->data_link_type == PCIE_DATA_LINK_C2C && pcie_info->link_role == PCIE_LINK_ROLE_RC) {
			all_c2c_rc++;
			want_c2c_link[i] = 1;
		}
	}

	return all_c2c_rc;
}

static DEVICE_ATTR_RW(c2c_enable);
static int sophgo_c2c_enable_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct c2c_chip_info *chip_info;
	struct c2c_info *info;
	int ret;

	chip_info = of_device_get_match_data(dev);
	if (!chip_info) {
		pr_err("failed to get match data\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(struct c2c_info), GFP_KERNEL);
	if (!info)
		return -EINVAL;

	info->pcie_info = ioremap(chip_info->pcie_info_addr, chip_info->pcie_info_size);
	if (!info->pcie_info) {
		pr_err("failed to map pcie info\n");
		return -EINVAL;
	}

	ret = check_all_rc(info);
	pr_err("%d c2c pcie rc find\n", ret);

	ret = device_create_file(dev, &dev_attr_c2c_enable);
	pr_info("[c2c_enable]: create c2c_enable success\n");

	return ret;
}

static void sophgo_c2c_enable_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_remove_file(dev, &dev_attr_c2c_enable);
	pr_info("[c2c_enable]: remove c2c_enable success\n");

	return;
}

static struct c2c_chip_info bm1690_c2c_if = {
	.pcie_info_addr = 0x70101f4000,
	.pcie_info_size = 10 * 1024 * 1024,
};

static const struct of_device_id c2c_enable_of_match[] = {
	{ .compatible = "sophgo,c2c_enable", .data = &bm1690_c2c_if},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, c2c_enable_of_match);

static struct platform_driver sophgo_c2c_enable_driver = {
	.driver = {
		.name		= "sophgo-c2c-enable",
		.of_match_table	= c2c_enable_of_match,
	},
	.probe			= sophgo_c2c_enable_probe,
	.remove			= sophgo_c2c_enable_remove,
};

module_platform_driver(sophgo_c2c_enable_driver);

void sophgo_setup_c2c(void)
{
	c2c_enable = 1;
	platform_register_drivers(c2c_drivers, 1);
}
EXPORT_SYMBOL_GPL(sophgo_setup_c2c);

static void sophgo_deinit_c2c(void)
{
	platform_unregister_drivers(c2c_drivers, 1);

	atomic_set(&ready_c2c_rc, 0);
	for (int i = 0; i < PCIE_BUS_MAX; i++)
		want_c2c_link[i] = 0;
}

void sophgo_rest_c2c(void)
{
	if (c2c_enable == 1) {
		pr_err("c2c already setup, so we reset it\n");
		sophgo_deinit_c2c();
	}
}
EXPORT_SYMBOL_GPL(sophgo_rest_c2c);

int sophgo_check_c2c(void)
{
	uint64_t all_ready;

	all_ready = atomic_read(&ready_c2c_rc);

	if (all_ready == all_c2c_rc)
		return -1;

	for (int i = 0; i < PCIE_BUS_MAX; i++) {
		if (want_c2c_link[i] != c2c_link_status[i]) {
			pr_err("pcie[%d] c2c link failed\n", i);
			return i;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sophgo_check_c2c);

int sophgo_set_c2c_ready(int bus_num)
{
	atomic_add(1, &ready_c2c_rc);
	c2c_link_status[bus_num] = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(sophgo_set_c2c_ready);

int sophgo_set_all_c2c_rc_num(uint64_t all_rc)
{
	all_c2c_rc = all_rc;

	return 0;
}
EXPORT_SYMBOL_GPL(sophgo_set_all_c2c_rc_num);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for c2c rc driver");
