#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/cdev.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define	GET_RXU_PLIC_BIT(x, bit)	((x & (1 << bit)) >> bit)
#define RXU_INT_PLIC_STATUS_SHIFT   0x240
#define RXU_SW_RST_SHIFT 0x4
#define RXU_MODE		0x7c0
#define CTRL0			0
#define INT_ENABLE_SHIFT	1
#define INT_ENABLE_MASK		0x00000006
#define INT_ENABLE_VALUE	0x1
#define STOP_RXU_SHIFT		10
#define STOP_RXU_MASK		0x00000400
#define STOP_RXU_VALUE		0x1
#define STOP_FINISH_SHIFT	11
#define STOP_FINISH_MASK	0x00000800
#define STOP_FINISH_VALUE	0x1
#define FPRC_SET_SHIFT		16
#define FPRC_SET_MASK		0x00010000
#define FPRC_VALUE_SHIFT	17
#define FPRC_VALUE_MASK		0x00060000
#define MISC_CONFIG0		0x5
#define EXE_STALL_VEC		0x8
#define HANG_FLAG_SHIFT		31
#define HANG_FLAG_MASK		0x80000000
#define CURRENT_NONCE		0xc
#define DATASET_ADDR		0x18
#define MSG_VLD_NONCE		0x54c
#define MSG_VALUE		0x5d0
#define MSG_DIFF		0x624
#define MSG_NONCE_CTRL		0x628
#define MSG_ID_SHIFT		0
#define MSG_ID_MASK		0x000000ff
#define MSG_LEN_SHIFT		8
#define MSG_LEN_MASK		0x0000ff00
#define NONCE_INCR_SHIFT	16
#define NONCE_INCR_MASK		0xffff0000
#define MSG_START_NONCE		0x620
#define LONG_MSG_VALUE		0x6b0
#define LONG_MSG_START_NONCE	0x7b0
#define CTRL1			0x62c
#define MSG_HASH_ID_SHIFT	0
#define MSG_HASH_ID_MASK	0xff
#define MSG_VLD_SHIFT		8
#define MSG_VLD_MASK		0x00000100
#define MSG_HASH_VLD_SHIFT	13
#define MSG_HASH_VLD_MASK	0x00002000
#define MSG_HASH_VALUE		0x630
#define RXU_STOP_CMD 0
#define DDR_PREF_ID		0x62e
#define DDR_PREF_SHIFT		6
#define DDR_PREF_MASK		0xc0

static inline void __iomem *rxu_addr(void __iomem *base, unsigned long offset)
{
	return (void __iomem *)((unsigned long)base + offset);
}

static void rxu_set_dataset(void __iomem *base, u64 addr)
{
	pr_debug("set dataset to %llu\n", addr);

	writeq(addr, rxu_addr(base, DATASET_ADDR));
}

static u32 rxu_get_current_nonce(void __iomem *base)
{
	return readl(rxu_addr(base, CURRENT_NONCE));
}

static void rxu_set_msg_value(void __iomem *base, void *msg, u8 len)
{
	u32 tmp;

	pr_debug("msg value set len=%d\n", len);

	memcpy_toio(rxu_addr(base, MSG_VALUE), msg, len);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~MSG_LEN_MASK;
	tmp |= (len << MSG_LEN_SHIFT) & MSG_LEN_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_long_msg_value(void __iomem *base, void *msg, u8 len)
{
	u32 tmp;

	pr_debug("msg value set len=%d\n", len);

	memcpy_toio(rxu_addr(base, LONG_MSG_VALUE), msg, len);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~MSG_LEN_MASK;
	tmp |= (len << MSG_LEN_SHIFT) & MSG_LEN_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_msg_diff(void __iomem *base, u32 diff)
{
	pr_debug("msg diff set %u\n", diff);

	writel(diff, rxu_addr(base, MSG_DIFF));
}

static void rxu_set_msg_nonce_incr(void __iomem *base, u16 incr)
{
	u32 tmp;

	pr_debug("msg nonce incr set %u\n", incr);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~NONCE_INCR_MASK;
	tmp |= (incr << NONCE_INCR_SHIFT) & NONCE_INCR_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_msg_id(void __iomem *base, u8 id)
{
	u32 tmp;

	pr_debug("msg id set %u\n", id);

	tmp = readl(rxu_addr(base, MSG_NONCE_CTRL));
	tmp &= ~MSG_ID_MASK;
	tmp |= (id << MSG_ID_SHIFT) & MSG_ID_MASK;
	writel(tmp, rxu_addr(base, MSG_NONCE_CTRL));
}

static void rxu_set_msg_start_nonce(void __iomem *base, u32 nonce)
{
	pr_debug("msg start nonce set %u\n", nonce);

	writel(nonce, rxu_addr(base, MSG_START_NONCE));
}

static void rxu_set_hash_vld(void *base, unsigned int val)
{
	u32 tmp;

	pr_debug("hash vld set %d\n", val);

	tmp = readl(rxu_addr(base, CTRL1));
	if (val)
		tmp |= MSG_HASH_VLD_MASK;
	else
		tmp &= ~MSG_HASH_VLD_MASK;
	writel(tmp, rxu_addr(base, CTRL1));
}

static void rxu_get_hash(void *base, unsigned int *id, u32 *nonce, void *hash)
{
	*id = (readl(rxu_addr(base, CTRL1)) & MSG_HASH_ID_MASK)
		>> MSG_HASH_ID_SHIFT;
	*nonce = readl(rxu_addr(base, MSG_VLD_NONCE));
	memcpy_fromio(hash, rxu_addr(base, MSG_HASH_VALUE), 32);
}

static void rxu_set_rxu_mode(void __iomem *base, u8 mode)
{
	writeb(mode, rxu_addr(base, RXU_MODE));
}

static void rxu_set_rxu_id(void __iomem *base, u8 id)
{
	writeb(id, rxu_addr(base, MISC_CONFIG0));
}

static void rxu_set_ddr_id(void __iomem *base, u8 id)
{
	u8 tmp;

	tmp = readb(rxu_addr(base, DDR_PREF_ID));
	tmp &= ~DDR_PREF_MASK;
	tmp |= id << DDR_PREF_SHIFT;
	writeb(tmp, rxu_addr(base, DDR_PREF_ID));
}

static bool rxu_is_hang(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, EXE_STALL_VEC);

	tmp = readl(reg);
	if (tmp & HANG_FLAG_MASK)
		pr_debug("RXU stall vec = 0x%x\n", tmp);

	return tmp & HANG_FLAG_MASK;
}

static void rxu_interrupt_enable(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	tmp &= ~INT_ENABLE_MASK;
	tmp |= (INT_ENABLE_VALUE << INT_ENABLE_SHIFT);
	writel(tmp, reg);
}

static void rxu_interrupt_disable(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	tmp &= ~INT_ENABLE_MASK;
	writel(tmp, reg);
}

static int rxu_get_stop_finish(void __iomem *base)
{
	void __iomem *reg = rxu_addr(base, CTRL0);

	return !!(readl(reg) & STOP_FINISH_MASK);
}

static void rxu_clean_stop_finish(void __iomem *base)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	tmp &= ~STOP_FINISH_MASK;
	writel(tmp, reg);
}

static void rxu_set_stop(void __iomem *base, int val)
{
	u32 tmp;
	void __iomem *reg = rxu_addr(base, CTRL0);

	tmp = readl(reg);
	if (val)
		tmp |= STOP_RXU_VALUE << STOP_RXU_SHIFT;
	else
		tmp &= ~STOP_RXU_MASK;

	writel(tmp, reg);
}

static void rxu_start(void __iomem *base)
{
	u32 tmp;

	pr_debug("rxu start\n");

	tmp = readl(rxu_addr(base, CTRL1));
	tmp |= MSG_VLD_MASK;
	writel(tmp, rxu_addr(base, CTRL1));
}

static void rxu_stop(void __iomem *base)
{
	/* enable rxu stop */
	u32 count = 2000;

	rxu_set_stop(base, true);

	/* wati rxu stop finished */
	while (!rxu_get_stop_finish(base) && count --)
		;

	rxu_set_hash_vld(base, false);
}

struct msg_info {
	uint8_t rxu_mode;
	uint8_t msg_id;
	uint8_t msg_len;
	uint8_t diff;
	uint8_t rxu_id;
	uint32_t start_nonce;
	uint16_t nonce_incr;
	uint8_t reserve1[5];
	uint8_t ddr_id;
	uint64_t dataset_pa;
	uint8_t msg[256];
};

struct rxu_result {
	uint8_t msg_id;
	uint8_t reserved[3];
	uint32_t nonce;
	uint8_t hash[32];
};

int rxu_major	   = 290;
int rxu_minor	   = 0;
int number_of_devices = 9;
struct class *rxu_class;

struct rxu_dev {
	struct cdev cdev;
	struct rxu_reg __iomem *reg;

	/* rxu watchdog */
	struct timer_list timer;
	wait_queue_head_t wq;
	int hash_vld;
	spinlock_t lock;

	/* rxu init flag*/
	uint8_t rxu_mode;
	uint8_t rxu_id;
	int init_flag;
	int hang_count;
	/* msg info backup */
	struct msg_info current_msg;
	struct work_struct mywork;

};

struct rxu_cluster {
	struct device *dev;
	struct rxu_dev rxu_dev[9];
	struct intr_reg __iomem *intr_reg;
	long cluster_offset;
	char class_name[32];

};

static void rxu_config(struct rxu_dev *rxu_dev, struct msg_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&rxu_dev->lock, flags);
	rxu_set_rxu_mode(rxu_dev->reg, info->rxu_mode);
	if(info->rxu_mode == 0) {
		rxu_set_ddr_id(rxu_dev->reg, info->ddr_id);
		rxu_set_rxu_id(rxu_dev->reg, info->rxu_id);
		rxu_dev->rxu_id = info->rxu_id;
		rxu_set_dataset(rxu_dev->reg, info->dataset_pa);
		rxu_set_msg_id(rxu_dev->reg, info->msg_id);
		rxu_set_msg_diff(rxu_dev->reg, info->diff);
		rxu_set_msg_start_nonce(rxu_dev->reg, info->start_nonce);
		rxu_set_msg_value(rxu_dev->reg, info->msg, info->msg_len);
		rxu_set_msg_nonce_incr(rxu_dev->reg, info->nonce_incr);
	} else {
		rxu_set_ddr_id(rxu_dev->reg, info->ddr_id);
		rxu_set_rxu_id(rxu_dev->reg, info->rxu_id);
		rxu_dev->rxu_id = info->rxu_id;
		rxu_set_dataset(rxu_dev->reg, info->dataset_pa);
		rxu_set_msg_id(rxu_dev->reg, info->msg_id);
		rxu_set_msg_diff(rxu_dev->reg, info->diff);
		rxu_set_msg_start_nonce(rxu_dev->reg, info->start_nonce);
		rxu_set_long_msg_value(rxu_dev->reg, info->msg, info->msg_len);
		rxu_set_msg_nonce_incr(rxu_dev->reg, info->nonce_incr);
	}
	spin_unlock_irqrestore(&rxu_dev->lock, flags);
}

static void rxu_recovery(struct work_struct *work)
{
	struct rxu_dev *rxu_dev = container_of(work, struct rxu_dev, mywork);
	u32 id_offset = (rxu_dev->rxu_id) % 9;
	struct rxu_cluster *rxu_cluster;
	rxu_cluster = container_of(rxu_dev, struct rxu_cluster, rxu_dev[id_offset]);

	u32 current_nonce;

	current_nonce = rxu_get_current_nonce(rxu_dev->reg);
	rxu_dev->current_msg.start_nonce = current_nonce +
						rxu_dev->current_msg.nonce_incr;
	rxu_dev->hang_count++;
	if (id_offset > 5) {
		u32 rxu_sw_rst = readl(rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst &= ~(1 << (id_offset + 10));
		writel(rxu_sw_rst, rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst = readl(rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst |= 1 << (id_offset + 11);
		writel(rxu_sw_rst, rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		udelay(5);
		rxu_sw_rst = readl(rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst &= ~(1 << (id_offset + 11));
		writel(rxu_sw_rst, rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
	} else {
		u32 rxu_sw_rst = readl(rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst &= ~(1 << (id_offset + 10));
		writel(rxu_sw_rst, rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst = readl(rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst |= 1 << (id_offset + 10);
		writel(rxu_sw_rst, rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		udelay(5);
		rxu_sw_rst = readl(rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
		rxu_sw_rst &= ~(1 << (id_offset + 10));
		writel(rxu_sw_rst, rxu_addr(rxu_cluster->intr_reg, RXU_SW_RST_SHIFT));
	}
	rxu_config(rxu_dev, &rxu_dev->current_msg);
	rxu_interrupt_enable(rxu_dev->reg);
	rxu_start(rxu_dev->reg);
}

static void rxu_wtd(struct timer_list *timer)
{
	struct rxu_dev *rxu_dev = from_timer(rxu_dev, timer, timer);

	if (rxu_is_hang(rxu_dev->reg)) {
	schedule_work(&rxu_dev->mywork);
	}

	mod_timer(&rxu_dev->timer, jiffies + 200);
}

static ssize_t rxu_read(struct file *filp, char __user *buf, size_t size,
			loff_t *ppos)
{
	struct rxu_result res;
	struct rxu_dev *rxu_dev;
	unsigned int id;
	unsigned int nonce;
	int err;

	rxu_dev = filp->private_data;

	wait_event_interruptible(rxu_dev->wq, rxu_dev->hash_vld != false);
	if (rxu_dev->hash_vld == false)
		return 0;

	rxu_dev->hash_vld = false;
	rxu_get_hash(rxu_dev->reg, &id, &nonce, res.hash);
	rxu_set_hash_vld(rxu_dev->reg, false);
	res.msg_id = id;
	res.nonce = nonce;

	err = copy_to_user(buf, &res, size);

	return err ? -EFAULT : size;
}

static ssize_t rxu_write(struct file *filp, const char __user *buf, size_t size,
			 loff_t *ppos)
{
	struct rxu_dev *rxu_dev;
	struct msg_info *msg;
	int err;

	rxu_dev = filp->private_data;
	msg = &rxu_dev->current_msg;

	err = copy_from_user(msg, buf, size);
	if (err)
		return -EFAULT;

	rxu_config(rxu_dev, msg);
	rxu_start(rxu_dev->reg);

	return 0;
}

static int rxu_open(struct inode *inode, struct file *filp)
{
	struct rxu_dev *rxu_dev = container_of(inode->i_cdev, struct rxu_dev, cdev);

	filp->private_data = rxu_dev;
	rxu_interrupt_enable(rxu_dev->reg);
	rxu_dev->timer.expires = jiffies + 200;
	add_timer(&rxu_dev->timer);

	return 0;
}

static int rxu_close(struct inode *inode, struct file *filp)
{
	struct rxu_dev *rxu_dev = container_of(inode->i_cdev, struct rxu_dev, cdev);

	rxu_interrupt_disable(rxu_dev->reg);
	rxu_stop(rxu_dev->reg);

	del_timer_sync(&rxu_dev->timer);

	return 0;
}

static long rxu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct rxu_dev *rxu_dev = filp->private_data;

	switch (cmd) {
	case RXU_STOP_CMD:
		rxu_stop(rxu_dev->reg);
		break;
	default:
		rxu_set_stop(rxu_dev->reg, false);
		rxu_clean_stop_finish(rxu_dev->reg);
		break;
	}

	return 0;
}

static const struct file_operations rxu_fops = {
	.owner = THIS_MODULE,
	.open =  rxu_open,
	.read = rxu_read,
	.write = rxu_write,
	.release = rxu_close,
	.unlocked_ioctl = rxu_ioctl,
};

static irqreturn_t rxu_irq(int irq, void *dev)
{
	int i = 0;
	struct rxu_cluster *rxu_cluster = (struct rxu_cluster *)dev;
	u32 plic_src = readl(rxu_addr(rxu_cluster->intr_reg, RXU_INT_PLIC_STATUS_SHIFT));
	for (i = 0; i < 10; i++) {
		if (GET_RXU_PLIC_BIT(plic_src, i) == 1) {
			if (i > 5) {
				rxu_set_hash_vld(rxu_cluster->rxu_dev[i - 1].reg, false);
				rxu_cluster->rxu_dev[i - 1].hash_vld = true;
				wake_up_interruptible(&(rxu_cluster->rxu_dev[i - 1].wq));
			} else {
				rxu_set_hash_vld(rxu_cluster->rxu_dev[i].reg, false);
				rxu_cluster->rxu_dev[i].hash_vld = true;
				wake_up_interruptible(&(rxu_cluster->rxu_dev[i].wq));
			}
		}
	}

	return IRQ_HANDLED;
};

static int dts_rxu_probe(struct rxu_cluster *rxu_cluster)
{
	const char *str;
	int err;

	rxu_cluster->cluster_offset = 0;
	err = of_property_read_string(dev_of_node(rxu_cluster->dev),
					  "cluster_offset", &str);

	if (!err) {
		err = kstrtol(str, 0, &rxu_cluster->cluster_offset);
		if (err)
			return err;
	}
	sprintf(rxu_cluster->class_name, "%s%ld", "rxucluster", rxu_cluster->cluster_offset);

	return 0;
}

static void setup_rxu_cdev(struct cdev *cdev, dev_t devno)
{
	int err;
 
	cdev_init(cdev, &rxu_fops);
	cdev->owner = THIS_MODULE;
	err = cdev_add(cdev, devno , 1);
	if (err)
		pr_err("Setup error: %d\n", err);
}

static int rxu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rxu_reg __iomem *reg;
	struct intr_reg __iomem *intr_reg;
	struct rxu_cluster *rxu_cluster;
	int irq;
	int err;
	int i;
	int result;
	long rxu_num_offset;
	dev_t devno;

	rxu_cluster = devm_kzalloc(dev, sizeof(struct rxu_cluster), GFP_KERNEL);
	if (!rxu_cluster)
		return -ENOMEM;

	err = dma_set_mask(dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(dev, "cannot set dma mask\n");
		return err;
	}

	err = dma_set_coherent_mask(dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(dev, "cannot set dma coherent mask\n");
		return err;
	}

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg)) {
		dev_err(dev, "get register base failed\n");
		return PTR_ERR(reg);
	}

	intr_reg = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(intr_reg)) {
		dev_err(dev, "get intr register base failed\n");
		return PTR_ERR(intr_reg);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "get irq number failed\n");
		return irq;
	}

	rxu_cluster->dev = dev;
	rxu_cluster->intr_reg = intr_reg;

	err = devm_request_irq(dev, irq, rxu_irq, 0, dev_name(dev), rxu_cluster);
	if (err) {
		dev_err(dev, "request irq failed\n");
		return err;
	}

	dts_rxu_probe(rxu_cluster);

	rxu_minor += 9 * (rxu_cluster->cluster_offset);
	rxu_major += rxu_cluster->cluster_offset;
	devno = MKDEV(rxu_major, rxu_minor);
	result = register_chrdev_region(devno, number_of_devices, rxu_cluster->class_name);
 
	if (result < 0) {
		pr_err("can't get major number %d\n", rxu_major);
		return result;
	}

	rxu_class = class_create(rxu_cluster->class_name);
	if(IS_ERR(rxu_class)) {
		pr_err("Err: failed in creating class.\n");
		return -1; 
	}

	for (i = 0; i < number_of_devices; i++) {
		rxu_num_offset = i + (rxu_cluster->cluster_offset) * 9;
		device_create(rxu_class, NULL, devno + i, NULL, "rxu%ld", rxu_num_offset);
		setup_rxu_cdev(&(rxu_cluster->rxu_dev[i].cdev), devno + i);
		spin_lock_init(&(rxu_cluster->rxu_dev[i].lock));
		init_waitqueue_head(&(rxu_cluster->rxu_dev[i].wq));
		INIT_WORK(&(rxu_cluster->rxu_dev[i].mywork), rxu_recovery);
		if (i > 5)
			rxu_cluster->rxu_dev[i].reg = rxu_addr(reg, ((i + 1) * 0x400000));
		else
			rxu_cluster->rxu_dev[i].reg = rxu_addr(reg, (i * 0x400000));
		rxu_cluster->rxu_dev[i].init_flag = 0;
		dev_set_drvdata(dev, rxu_cluster);
		timer_setup(&(rxu_cluster->rxu_dev[i].timer), rxu_wtd, 0);
	}
	
	return 0;
}

static void rxu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rxu_cluster *rxu_cluster;
	int i = 0;

	dev_dbg(dev, "removed\n");

	rxu_cluster = dev_get_drvdata(dev);
	dev_t devno = MKDEV(rxu_major, rxu_minor);

	for (i = 0; i < number_of_devices; i++) {
		cdev_del (&rxu_cluster->rxu_dev[i].cdev);
		device_destroy(rxu_class, devno + i);
	}
	class_destroy(rxu_class);
	unregister_chrdev_region(devno, number_of_devices);

	return;
}

static const struct of_device_id rxu_match[] = {
	{.compatible = "sophgo,rxu"},
	{},
};

static struct platform_driver rxu_driver = {
	.probe = rxu_probe,
	.remove = rxu_remove,
	.driver = {
		.name = "rxu",
		.of_match_table = rxu_match,
	},
};

static int __init rxu_init(void)
{
	int err;

	pr_debug("RXU init!\n");

	err = platform_driver_register(&rxu_driver);

	if (err)
		return err;

	return 0;
}

static void __exit rxu_exit(void)
{
	platform_driver_unregister(&rxu_driver);

	pr_debug("RXU exit!\n");
}

module_init(rxu_init);
module_exit(rxu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chao.Wei");
MODULE_DESCRIPTION("SOPHGO RXU");
MODULE_VERSION("0.01");
