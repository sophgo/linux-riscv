#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/cpufreq.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/parser.h>
#endif

#define DEV_NAME "mango-top"

#define VMON_TMON_MUX_SEL	0x0C
#define AUTO_CLOCK_GATING	0x20

#define MIN(a, b)	((a) > (b) ? (b) : (a))

struct bm_top_device {
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	struct class *class;
	dev_t devno;
	struct regmap *syscon;
	void *priv;
};

#ifdef CONFIG_DEBUG_FS
struct bm_clock_dbg {
	struct list_head head;
	char *clk_name;
	const char *parent_name;
	struct clk *clk;
	unsigned long clk_rate;
};

struct bm_top_clk_list {
	struct list_head head;
};

struct bm_top_clk_list clk_list = {
	.head = LIST_HEAD_INIT(clk_list.head),
};

#define STR_LEN_MAX		64

static struct dentry *bmtop_debugfs_dir;
#endif

static struct bm_top_device bm_top_dev;

static void bm_set_vtmon_sel(uint16_t vsel, uint16_t tsel)
{
	u32 reg_val;
	u32 mask = ~(0x1f<<8|0x1f);

	regmap_read(bm_top_dev.syscon, VMON_TMON_MUX_SEL, &reg_val);
	reg_val = (reg_val & mask) | (vsel << 8) | tsel;
	pr_info("adjust VTMON_SEL to 0x%x\n", reg_val);
	regmap_write(bm_top_dev.syscon,  VMON_TMON_MUX_SEL,
		reg_val);
}

static void bm_set_auto_clock_gating(uint32_t val)
{
	regmap_write(bm_top_dev.syscon, AUTO_CLOCK_GATING, val);
	pr_info("enable auto clock gating 0x%x\n", val);
}

static ssize_t bm_top_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int ret, len;
	char str_buf[512];
	static int print_end;

	if (print_end) {
		print_end = 0;
		return 0;
	}

	len = snprintf(str_buf, sizeof(str_buf), "to do");
	if (len <= 0 || len > count) {
		pr_err("snprintf failed %d\n", len);
		return -EFAULT;
	}
	ret = copy_to_user(buf, str_buf, len);
	if (ret) {
		pr_err("copy from user fail\n");
		return -EFAULT;
	}

	if (!print_end) {
		print_end = 1;
		return len;
	}
	return 0;
}

static ssize_t bm_top_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int ret, len;
	char str_buf[512];

	len = MIN(count, sizeof(str_buf));
	memset(str_buf, 0, sizeof(str_buf));
	ret = copy_from_user(str_buf, buf, len);
	if (ret) {
		pr_err("copy from user fail\n");
		return -EFAULT;
	}

	return count;
}

static int bm_top_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int bm_top_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations bm_top_fops = {
	.read = bm_top_read,
	.write = bm_top_write,
	.open = bm_top_open,
	.release = bm_top_close,
	.owner = THIS_MODULE,
};

static int create_bm_top_cdev(void)
{
	int ret;

	ret = alloc_chrdev_region(&bm_top_dev.devno, 0, 1, DEV_NAME);
	if (ret < 0) {
		pr_err("bm top register chrdev error\n");
		return ret;
	}
	bm_top_dev.class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(bm_top_dev.class)) {
		pr_err("bm top create class error\n");
		return -ENOENT;
	}
	bm_top_dev.dev = device_create(bm_top_dev.class,
				bm_top_dev.parent, bm_top_dev.devno, NULL, DEV_NAME);
	cdev_init(&bm_top_dev.cdev, &bm_top_fops);
	bm_top_dev.cdev.owner = THIS_MODULE;
	cdev_add(&bm_top_dev.cdev, bm_top_dev.devno, 1);

	return 0;
}

static void destroy_bm_top_cdev(void)
{
	cdev_del(&bm_top_dev.cdev);
	device_destroy(bm_top_dev.class, bm_top_dev.devno);
	class_destroy(bm_top_dev.class);
	unregister_chrdev_region(bm_top_dev.devno, 1);
}

#ifdef CONFIG_DEBUG_FS
enum clk_ops_token {
	SET_RATE,
	GET_RATE,
	SET_PARENT,
	GET_PARENT,
	ENABLE,
	NOPS
};

static const match_table_t key_tokens = {
	{SET_RATE, "setr"},
	{GET_RATE, "getr"},
	{SET_PARENT, "setp"},
	{GET_PARENT, "getp"},
	{ENABLE, "en"},
	{NOPS, NULL}
};

static ssize_t clock_write(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char *buf, *tmp, *parent_clk_name;
	int token;
	unsigned int rate, en;
	substring_t args[MAX_OPT_ARGS];
	struct clk *_clk, *pclk;
	struct bm_clock_dbg *pmt;

	buf = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return count;

	if (copy_from_user(buf, user_buf, count - 1))
		goto error;

	buf[count - 1] = '\0';

	/* get ops token */
	tmp = strsep(&buf, " ");
	if (!tmp || *tmp == '\0' || *tmp == ' ')
		goto error;

	token = match_token(tmp, key_tokens, args);

	/* get clk name */
	if (token == GET_RATE || token == GET_PARENT)
		tmp = strsep(&buf, "\0");
	else
		tmp = strsep(&buf, " ");

	if (!tmp || *tmp == '\0' || *tmp == ' ')
		goto error;

	list_for_each_entry(pmt, &clk_list.head, head) {
		if (pmt->clk_name && !strcmp(tmp, pmt->clk_name))
			goto doo;
	}

	pmt = kzalloc(sizeof(struct bm_clock_dbg), GFP_KERNEL);
	if (!pmt)
		goto error;

	pmt->clk_name = kzalloc(STR_LEN_MAX, GFP_KERNEL);
	if (!pmt->clk_name) {
		kfree(pmt);
		goto error;
	}

	strncpy(pmt->clk_name, tmp, strlen(tmp));

	_clk = __clk_lookup(tmp);
	pmt->clk = _clk;

	list_add_tail(&pmt->head, &clk_list.head);

doo:
	switch (token) {
	case SET_RATE:
		tmp = strsep(&buf, "\0");
		if (!tmp)
			goto error;
		if (*tmp == '\0' || *tmp == ' ')
			goto error;

		if (isdigit(*tmp)) {
			if (kstrtouint(tmp, 0, &rate) < 0)
				goto error;
			pmt->clk_rate = rate;
			clk_set_rate(pmt->clk, rate);
		}
		break;
	case GET_RATE:
		pmt->clk_rate = clk_get_rate(pmt->clk);
		pr_info("clk: %s's rate is %lu\n", pmt->clk_name, pmt->clk_rate);
		break;
	case GET_PARENT:
		pclk = clk_get_parent(pmt->clk);
		pmt->parent_name = kzalloc(STR_LEN_MAX, GFP_KERNEL);
		if (!pmt->parent_name)
			goto error;
		pmt->parent_name = __clk_get_name(pclk);
		pr_info("clk: %s's parent is %s\n", pmt->clk_name, pmt->parent_name);
		break;
	case SET_PARENT:
		parent_clk_name = strsep(&buf, "\0");
		if (*parent_clk_name == '\0' || *parent_clk_name == ' ')
			goto error;
		pclk = __clk_lookup(parent_clk_name);
		clk_set_parent(pmt->clk, pclk);
		break;
	case ENABLE:
		tmp = strsep(&buf, "\0");
		if (!tmp)
			goto error;
		if (*tmp == '\0' || *tmp == ' ')
			goto error;
		if (kstrtouint(tmp, 10, &en) < 0)
			goto error;
		en ? clk_prepare_enable(pmt->clk) : clk_disable_unprepare(pmt->clk);
		break;
	default:
		break;
	}

	kfree(buf);
	return count;

error:
	kfree(buf);
	return -EINVAL;
}

static int clock_show(struct seq_file *s, void *data)
{
	struct bm_clock_dbg *tmp;

	seq_puts(s, "clock               rate\n");
	seq_puts(s, "------------------------------\n");

	list_for_each_entry(tmp, &clk_list.head, head)
		seq_printf(s, "%-*s%-*lu\n", 20, tmp->clk_name, 20,
			   tmp->clk_rate);

	return 0;
}

static int clock_open(struct inode *inode, struct file *file)
{
	return single_open(file, clock_show, inode->i_private);
}

static const struct file_operations top_clock_fops = {
	.open = clock_open,
	.write = clock_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};
#endif

#if defined(CONFIG_PM) || defined(CONFIG_HOTPLUG_CPU)
struct clk *axi_sram_clk;
struct clk *apb_rom_clk;
#endif

#ifdef CONFIG_PM
static int top_suspend(struct device *dev)
{
	dev_info(dev, "suspend\n");
	return 0;
}

static int top_resume(struct device *dev)
{
	dev_info(dev, "resume\n");
	return 0;
}

static const struct dev_pm_ops top_pm_ops = {
	.suspend	= top_suspend,
	.resume		= top_resume,
};
#endif

static int bm_top_probe(struct platform_device *pdev)
{
	struct regmap *syscon;
	struct clk *dbg_i2c_clk;

	syscon = syscon_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(syscon)) {
		dev_err(&pdev->dev, "cannot get regmap\n");
		return PTR_ERR(syscon);
	}

	bm_top_dev.syscon = syscon;
	create_bm_top_cdev();

	/* set vmon top and tmon tpu */
	bm_set_vtmon_sel(18, 11);

	/* enable all auto clock gatings */
	bm_set_auto_clock_gating(0x3);

#ifdef CONFIG_DEBUG_FS
	bmtop_debugfs_dir = debugfs_create_dir("top", NULL);
	if (bmtop_debugfs_dir)
		debugfs_create_file("clock", 0664, bmtop_debugfs_dir, NULL, &top_clock_fops);
#endif

	/*enable axi_dbg_i2c clock*/
	dbg_i2c_clk = devm_clk_get(&pdev->dev, "dbg_i2c_clk");
	if (IS_ERR(dbg_i2c_clk)) {
		dev_warn(&pdev->dev, "cannot get dbg i2c clk\n");
	} else {
		if (clk_prepare_enable(dbg_i2c_clk) < 0)
			dev_err(&pdev->dev, "failed to enable dbg i2c clock\n");
	}
#if defined(CONFIG_PM) || defined(CONFIG_HOTPLUG_CPU)
	/*
	 * these clocks are needed by BL31's domian on/off routine:
	 * boot ROM is for non-primary core polling mailbox.
	 * axi sram if for primary core to let DDR enter self-refresh state
	 */
	axi_sram_clk = devm_clk_get(&pdev->dev, "axi_sram_clk");
	if (IS_ERR(axi_sram_clk)) {
		dev_warn(&pdev->dev, "cannot get axi sram clk\n");
	} else {
		if (clk_prepare_enable(axi_sram_clk) < 0)
			dev_err(&pdev->dev, "failed to enable axi sram clock\n");
	}
	apb_rom_clk = devm_clk_get(&pdev->dev, "apb_rom_clk");
	if (IS_ERR(apb_rom_clk)) {
		dev_warn(&pdev->dev, "cannot get apb rom clk\n");
	} else {
		if (clk_prepare_enable(apb_rom_clk) < 0)
			dev_err(&pdev->dev, "failed to enable apb rom clock\n");
	}
#endif
	return 0;
}

static int bm_top_remove(struct platform_device *pdev)
{
	destroy_bm_top_cdev();
	return 0;
}

static const struct of_device_id bm_top_of_match[] = {
	{
		.compatible = "sophgo,top-misc",
	},
	{}
};

static struct platform_driver bm_top_driver = {
	.probe  = bm_top_probe,
	.remove = bm_top_remove,
	.driver = {
		.name = "sophgo,mango_top",
		.of_match_table = bm_top_of_match,
#ifdef CONFIG_PM
		.pm = &top_pm_ops,
#endif
	},
};

static int __init bm_top_init(void)
{
	return platform_driver_register(&bm_top_driver);
}

subsys_initcall(bm_top_init);

MODULE_DESCRIPTION("temporary module for TOP, should be replaced by standard interfaces");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("xiao.wang@bitmain.com>");
