// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>

#define VTTY_POOL_SIZE (1 << 12)
// VTTY_BUFF_SIZE must be a power of 2
#define VTTY_BUFF_SIZE (1 << 11)

#define VTTY_CIRC_HEAD_OFFSET (0)
#define VTTY_CIRC_TAIL_OFFSET (64)
#define VTTY_AP_STATUS_OFFSET (128)
#define VTTY_BUFF_OFFSET (VTTY_POOL_SIZE - VTTY_BUFF_SIZE)

#define VTTY_MAX 8

#define vtty_circ_empty(circ) ((circ)->head == (circ)->tail)
#define vtty_circ_clear(circ) ((circ)->head = (circ)->tail = 0)

#define vtty_circ_chars_pending(circ) \
	(CIRC_CNT((circ)->head, (circ)->tail, VTTY_BUFF_SIZE))

#define vtty_circ_chars_free(circ) \
	(CIRC_SPACE((circ)->head, (circ)->tail, VTTY_BUFF_SIZE))

typedef enum { TP = 0, AP = 1 } device_type;

typedef enum { CLOSE = 0, OPEN = 1 } ap_status;

struct irq_info {
	uint64_t phy_addr;
	void __iomem *vir_addr;
	uint32_t reg_data;
};

struct sg_vtty {
	char *name;
	unsigned int index;
	unsigned int irq;
	device_type device_type;
	ap_status ap_status;

	void __iomem *tx_base;
	void __iomem *rx_base;
	struct circ_buf xmit;
	struct circ_buf rcv;

	struct irq_info tx_irq;
	struct irq_info rx_irq;

	struct tty_port port;
	struct device *dev;
};

static struct tty_driver *sg_vtty_driver;
static struct sg_vtty *sg_vttys;
static uint32_t sg_vtty_count = 0;

static inline void sg_writel(void __iomem *base, u32 offset, u32 value)
{
	iowrite32(value, (void __iomem *)(((unsigned long long)base) + offset));
}

static u32 sg_readl(void __iomem *base, u32 offset)
{
	return ioread32((void __iomem *)(((unsigned long long)base) + offset));
}

static void flush_tx_head(struct sg_vtty *vtty)
{
	sg_writel(vtty->tx_base, VTTY_CIRC_HEAD_OFFSET, vtty->xmit.head);
}

static void inval_rx_head(struct sg_vtty *vtty)
{
	vtty->rcv.head = sg_readl(vtty->rx_base, VTTY_CIRC_HEAD_OFFSET);
}

static void inval_tx_tail(struct sg_vtty *vtty)
{
	vtty->xmit.tail = sg_readl(vtty->tx_base, VTTY_CIRC_TAIL_OFFSET);
}

static void flush_rx_tail(struct sg_vtty *vtty)
{
	sg_writel(vtty->rx_base, VTTY_CIRC_TAIL_OFFSET, vtty->rcv.tail);
}

static ap_status read_ap_status(struct sg_vtty *vtty)
{
	return sg_readl(vtty->rx_base, VTTY_AP_STATUS_OFFSET) == OPEN ? OPEN :
									CLOSE;
}

static void write_ap_status(struct sg_vtty *vtty, ap_status status)
{
	sg_writel(vtty->tx_base, VTTY_AP_STATUS_OFFSET, status);
}

static void notify(struct sg_vtty *vtty)
{
	sg_writel(vtty->tx_irq.vir_addr, 0, vtty->tx_irq.reg_data);
}

static void clear_irq(struct sg_vtty *vtty)
{
	sg_writel(vtty->rx_irq.vir_addr, 0, vtty->rx_irq.reg_data);
}

static int sg_vtty_open(struct tty_struct *tty, struct file *filp)
{
	struct sg_vtty *vtty = &sg_vttys[tty->index];
	set_bit(TTY_THROTTLED, &tty->flags);
	if (vtty->device_type == AP) {
		write_ap_status(vtty, OPEN);
		notify(vtty);
	}
	return tty_port_open(&vtty->port, tty, filp);
}

static void sg_vtty_close(struct tty_struct *tty, struct file *filp)
{
	struct sg_vtty *vtty = &sg_vttys[tty->index];
	if (vtty->device_type == AP) {
		write_ap_status(vtty, CLOSE);
		notify(vtty);
	}
	tty_port_close(tty->port, tty, filp);
}

static void sg_vtty_hangup(struct tty_struct *tty)
{
	tty_port_hangup(tty->port);
}

static ssize_t sg_vtty_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
	struct sg_vtty *vtty = &sg_vttys[tty->index];
	struct circ_buf *circ = &vtty->xmit;
	char *xmit_buf = vtty->xmit.buf;
	ssize_t ret = 0;

	if (!(vtty->device_type == TP && vtty->ap_status == CLOSE)) {
		inval_tx_tail(vtty);
		while (1) {
			size_t c = CIRC_SPACE_TO_END(circ->head, circ->tail,
						  VTTY_BUFF_SIZE);
			if (count < c)
				c = count;

			if (c == 0) {
				break;
			}
			memcpy(xmit_buf + circ->head, buf, c);
			circ->head = (circ->head + c) & (VTTY_BUFF_SIZE - 1);
			buf += c;
			count -= c;
			ret += c;
		}
		flush_tx_head(vtty);
		notify(vtty);
	}
	return ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 6, 0)
static unsigned int sg_vtty_write_room(struct tty_struct *tty)
#else
static int sg_vtty_write_room(struct tty_struct *tty)
#endif
{
	struct sg_vtty *vtty = &sg_vttys[tty->index];
	struct circ_buf *circ = &vtty->xmit;
	unsigned int ret;

	ret = vtty_circ_chars_free(circ);

	return ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 6, 0)
static unsigned int sg_vtty_chars_in_buffer(struct tty_struct *tty)
#else
static int sg_vtty_chars_in_buffer(struct tty_struct *tty)
#endif
{
	struct sg_vtty *vtty = &sg_vttys[tty->index];
	struct circ_buf *circ = &vtty->xmit;
	unsigned int ret;

	inval_tx_tail(vtty);
	ret = vtty_circ_chars_pending(circ);

	return ret;
}

static int sg_vtty_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	return 0;
}

static void sg_vtty_port_shutdown(struct tty_port *port)
{
	return;
}

static const struct tty_operations sg_vtty_ops = {
	.open = sg_vtty_open,
	.close = sg_vtty_close,
	.hangup = sg_vtty_hangup,
	.write = sg_vtty_write,
	.write_room = sg_vtty_write_room,
	.chars_in_buffer = sg_vtty_chars_in_buffer,
};

static const struct tty_port_operations sg_vtty_port_ops = {
	.activate = sg_vtty_port_activate,
	.shutdown = sg_vtty_port_shutdown
};

static irqreturn_t sg_vtty_rx_interrupt(int irq, void *dev_id)
{
	struct sg_vtty *vtty = (struct sg_vtty *)dev_id;
	struct circ_buf *circ = &vtty->rcv;
	struct tty_port *port = &(vtty->port);
	char *buf = vtty->rcv.buf;
	int c;

	if (vtty->device_type == TP) {
		vtty->ap_status = read_ap_status(vtty);
		if (vtty->ap_status == CLOSE) {
			clear_irq(vtty);
			return IRQ_HANDLED;
		}
	}

	inval_rx_head(vtty);
	while (1) {
		c = CIRC_CNT_TO_END(circ->head, circ->tail, VTTY_BUFF_SIZE);
		if (c == 0)
			break;

		tty_insert_flip_string(port, buf + circ->tail, c);
		circ->tail = (circ->tail + c) & (VTTY_BUFF_SIZE - 1);
	}

	tty_flip_buffer_push(port);
	flush_rx_tail(vtty);
	clear_irq(vtty);

	return IRQ_HANDLED;
}

static int sg_vtty_create_driver(void)
{
	int ret;
	struct tty_driver *tty;

	sg_vttys = kcalloc(VTTY_MAX, sizeof(*sg_vttys), GFP_KERNEL);
	if (sg_vttys == NULL) {
		ret = -ENOMEM;
		goto err_alloc_sg_vttys_failed;
	}
	tty = tty_alloc_driver(VTTY_MAX, TTY_DRIVER_RESET_TERMIOS |
						 TTY_DRIVER_REAL_RAW |
						 TTY_DRIVER_DYNAMIC_DEV |
						 TTY_DRIVER_HARDWARE_BREAK);
	;
	if (tty == NULL) {
		ret = -ENOMEM;
		goto err_alloc_tty_driver_failed;
	}
	tty->driver_name = "sg_vtty";
	tty->name = "ttyV";
	tty->type = TTY_DRIVER_TYPE_SERIAL;
	tty->subtype = SERIAL_TYPE_NORMAL;
	tty->init_termios = tty_std_termios;

	tty_set_operations(tty, &sg_vtty_ops);
	ret = tty_register_driver(tty);
	if (ret)
		goto err_tty_register_driver_failed;

	sg_vtty_driver = tty;

	return 0;

err_tty_register_driver_failed:
	tty_driver_kref_put(tty);
err_alloc_tty_driver_failed:
	kfree(sg_vttys);
	sg_vttys = NULL;
err_alloc_sg_vttys_failed:
	return ret;
}

static void sg_vtty_delete_driver(void)
{
	tty_unregister_driver(sg_vtty_driver);
	tty_driver_kref_put(sg_vtty_driver);
	sg_vtty_driver = NULL;
	kfree(sg_vttys);
	sg_vttys = NULL;
}

static int vtty_probe_dt(struct sg_vtty *vtty, struct platform_device *pdev,
			 unsigned int line)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	struct resource *r;
	void __iomem *base;
	int ret;

	vtty->dev = dev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(dev, "No MEM resource available!\n");
		return -ENODEV;
	}

	base = devm_ioremap(dev, r->start + VTTY_BUFF_OFFSET, VTTY_BUFF_SIZE);
	if (!base) {
		dev_err(dev, "Unable to memremap tx buff!\n");
		return -ENOMEM;
	}
	vtty->xmit.buf = (char *)(base);
	vtty_circ_clear(&vtty->xmit);
	vtty->tx_base = devm_ioremap(dev, r->start, VTTY_BUFF_OFFSET);
	if (!vtty->tx_base) {
		dev_err(dev, "Map tx_base addr failed!\n");
		return -ENOMEM;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!r) {
		dev_err(dev, "No MEM resource available!\n");
		return -ENODEV;
	}

	base = devm_ioremap(dev, r->start + VTTY_BUFF_OFFSET, VTTY_BUFF_SIZE);

	if (!base) {
		dev_err(dev, "Unable to memremap tx buff!\n");
		return -ENOMEM;
	}
	vtty->rcv.buf = (char *)(base);
	vtty_circ_clear(&vtty->rcv);
	vtty->rx_base = devm_ioremap(dev, r->start, VTTY_BUFF_OFFSET);
	if (!vtty->rx_base) {
		dev_err(dev, "Map rx_base addr failed!\n");
		return -ENOMEM;
	}
	ret = of_property_read_u64_index(dev_node, "virtaul-msi", 0,
					 &vtty->tx_irq.phy_addr);
	if (ret) {
		dev_err(dev, "Get tx_irq addr failed\n");
		return ret;
	}
	vtty->tx_irq.vir_addr =
		devm_ioremap(dev, vtty->tx_irq.phy_addr, PAGE_SIZE);
	if (!vtty->tx_irq.vir_addr) {
		dev_err(dev, "Map tx_irq addr failed\n");
		return -ENOMEM;
	}
	ret = of_property_read_u32_index(dev_node, "virtaul-msi", 2,
					 &vtty->tx_irq.reg_data);
	if (ret) {
		dev_err(dev, "Get tx_irq data failed\n");
		return ret;
	}
	ret = of_property_read_u64_index(dev_node, "clr-irq", 0,
					 &vtty->rx_irq.phy_addr);
	if (ret) {
		dev_err(dev, "Get rx_irq addr failed\n");
		return ret;
	}
	vtty->rx_irq.vir_addr =
		devm_ioremap(dev, vtty->rx_irq.phy_addr, PAGE_SIZE);
	if (!vtty->rx_irq.vir_addr) {
		dev_err(dev, "Map rx_irq addr failed\n");
		return -ENOMEM;
	}
	ret = of_property_read_u32_index(dev_node, "clr-irq", 2,
					 &vtty->rx_irq.reg_data);
	if (ret) {
		dev_err(dev, "Get rx_irq data failed\n");
		return ret;
	}

	vtty->device_type =
		of_device_is_compatible(dev_node, "sophgo,vtty-ap") ? AP : TP;
	vtty->xmit.head = vtty->xmit.tail = 0;
	vtty->rcv.head = vtty->rcv.tail = 0;
	flush_tx_head(vtty);
	flush_rx_tail(vtty);
	vtty->ap_status = CLOSE;
	if (vtty->device_type == AP) {
		write_ap_status(vtty, CLOSE);
	}

	return 0;
}

static int sg_vtty_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	struct sg_vtty *vtty;
	int ret;
	int irq;
	struct device *ttydev;
	unsigned int line;

	if (sg_vtty_count == 0) {
		ret = sg_vtty_create_driver();
		if (ret)
			goto err_ret;
	}
	sg_vtty_count++;

	if (sg_vtty_count > VTTY_MAX) {
		dev_err(dev, "Reached maximum tty number of %d.\n",
			sg_vtty_count);
		ret = -ENOMEM;
		goto err_ret;
	}

	ret = of_alias_get_id(dev_node, "vtty");
	if (ret < 0) {
		dev_err(dev, "Fail to get alias id, errno %d\n", ret);
		return ret;
	}
	line = ret;

	vtty = &sg_vttys[line];
	vtty->index = line;
	ret = vtty_probe_dt(vtty, pdev, line);
	if (ret) {
		dev_err(dev, "Fail to get dt resource!\n");
		return -ENODEV;
	}

	tty_port_init(&vtty->port);
	tty_buffer_set_limit(&vtty->port, 8192);
	vtty->port.ops = &sg_vtty_port_ops;

	ttydev = tty_port_register_device(&vtty->port, sg_vtty_driver, line,
					  dev);
	if (IS_ERR(ttydev)) {
		ret = PTR_ERR(ttydev);
		dev_err(dev, "Fail to register device!\n");
		goto err_tty_register_device_failed;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "No IRQ resource available!\n");
		return -EINVAL;
	}
	vtty->name = devm_kzalloc(dev, 10, GFP_KERNEL);
	if (!vtty->name) {
		dev_err(dev, "malloc name memory failed\n");
		return -ENOMEM;
	}
	sprintf(vtty->name, "vtty%u", line);
	ret = devm_request_irq(dev, irq, sg_vtty_rx_interrupt, 0, vtty->name,
			       vtty);
	if (ret) {
		dev_err(dev, "Request irq failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, vtty);
	dev_info(dev, "Succeed to probe vtty%d!\n", line);
	return 0;

err_tty_register_device_failed:
	tty_port_destroy(&vtty->port);
	sg_vtty_count--;
	if (sg_vtty_count == 0)
		sg_vtty_delete_driver();
err_ret:
	return ret;
}

static void sg_vtty_remove(struct platform_device *pdev)
{
	struct sg_vtty *vtty = platform_get_drvdata(pdev);
	tty_unregister_device(sg_vtty_driver, vtty->index);
	tty_port_destroy(&vtty->port);
	sg_vtty_count--;
	if (sg_vtty_count == 0)
		sg_vtty_delete_driver();
	return;
}

static const struct of_device_id sg_vtty_of_match[] = {
	{
		.compatible = "sophgo,vtty-tp",
	},
	{
		.compatible = "sophgo,vtty-ap",
	},
	{},
};

MODULE_DEVICE_TABLE(of, sg_vtty_of_match);

static struct platform_driver
	sg_vtty_platform_driver = { .probe = sg_vtty_probe,
				    .remove = sg_vtty_remove,
				    .driver = {
					    .name = "sg_vtty",
					    .of_match_table = sg_vtty_of_match,
				    } };

module_platform_driver(sg_vtty_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bowen.pang");
MODULE_DESCRIPTION("sg driver for vtty");
