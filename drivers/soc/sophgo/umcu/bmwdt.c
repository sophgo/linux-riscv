
#define BMWDT_NAME			"bm-wdt"

#define pr_fmt(fmt) BMWDT_NAME ":" fmt

#include <linux/cdev.h>		/* For character device */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/fs.h>		/* For file operations */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/jiffies.h>	/* For timeout functions */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/kref.h>		/* For data references */
#include <linux/miscdevice.h>	/* For handling misc devices */
#include <linux/module.h>	/* For module stuff/... */
#include <linux/mutex.h>	/* For mutexes */
#include <linux/slab.h>		/* For memory functions */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/watchdog.h>	/* For watchdog specific items */
#include <linux/workqueue.h>	/* For workqueue */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */
#include <linux/of.h>		/* For device tree parse */
#include <linux/cpu.h>		/* For per cpu operations */
#include <linux/ctype.h>	/* For character judgement */

#define BMWDT_DOG_MAX			32
#define BMWDT_ENTER()			pr_devel("enter %s\n", __func__)

#define BMWDT_DEFAULT_STATUS		true
#define BMWDT_DEFAULT_MODE		true
#define BMWDT_DEFAULT_TIMEOUT		30
#define BMWDT_DEFAULT_INTERVAL(timeout)	((timeout) / 2)
#define BMWDT_MSGBUF_SIZE		128

#define bmwdt_dev_dbg(dev, fmt, ...)	\
	pr_devel("%d: " fmt, dev->wdd->id, ##__VA_ARGS__)
#define bmwdt_dev_info(dev, fmt, ...)	\
	pr_info("%d: " fmt, dev->wdd->id, ##__VA_ARGS__)
#define bmwdt_dev_warn(dev, fmt, ...)	\
	pr_warn("%d: " fmt, dev->wdd->id, ##__VA_ARGS__)
#define bmwdt_dev_err(dev, fmt, ...)	\
	pr_err("%d: " fmt, dev->wdd->id, ##__VA_ARGS__)

struct bmwdt_cpu_data {
	struct work_struct work;
	struct bmwdt_dev *dev;
};

/* per device context */
struct bmwdt_dev {
	struct cdev cdev;
	struct watchdog_device *wdd;
	struct mutex lock;
	atomic_t enable, automatic;
	atomic_t timeout, interval;
	atomic_t cpumask;
	struct delayed_work dwork;
	struct bmwdt_cpu_data __percpu *cpu_data;
	struct list_head head;
	char msgbuf[BMWDT_MSGBUF_SIZE];
	int msglen;
};

/* global context */
static struct bmwdt_ctx {
	struct list_head dev_list;
	struct mutex lock;
	struct ida ida;
	dev_t devno;
} ctx;

static int bmwdt_reset(struct bmwdt_dev *dev);

static int bmwdt_cmd_kick(struct bmwdt_dev *dev, const char *cmd)
{
	int err;
	struct watchdog_device *wdd = dev->wdd;

	err = wdd->ops->ping(wdd);

	return err ? -EIO : 0;
}

static int bmwdt_cmd_manual(struct bmwdt_dev *dev, const char *cmd)
{
	atomic_set(&dev->automatic, false);
	return bmwdt_reset(dev);
}

static int bmwdt_cmd_auto(struct bmwdt_dev *dev, const char *cmd)
{
	atomic_set(&dev->automatic, true);
	return bmwdt_reset(dev);
}

static int bmwdt_cmd_enable(struct bmwdt_dev *dev, const char *cmd)
{
	atomic_set(&dev->enable, true);
	return bmwdt_reset(dev);
}

static int bmwdt_cmd_disable(struct bmwdt_dev *dev, const char *cmd)
{
	atomic_set(&dev->enable, false);
	return bmwdt_reset(dev);
}

static int bmwdt_cmd_timeout(struct bmwdt_dev *dev, const char *cmd)
{
	long timeout;
	const char *p = cmd;

	if (strncmp(cmd, "timeout", strlen("timeout")) == 0) {
		/* timeout prefix */
		p += strlen("timeout");
	}

	/* skip all spaces */
	while (isspace(*p))
		++p;

	/* just timeout without value, reset to default value */
	if (!*p)
		timeout = BMWDT_DEFAULT_TIMEOUT;
	else
		if (kstrtol(p, 0, &timeout))
			return -EINVAL;

	if (timeout) {
		atomic_set(&dev->timeout, timeout);
		atomic_set(&dev->interval, BMWDT_DEFAULT_INTERVAL(timeout));
	} else
		atomic_set(&dev->enable, false);

	return bmwdt_reset(dev);
}

static int bmwdt_cmd_interval(struct bmwdt_dev *dev, const char *cmd)
{
	long interval;
	const char *p = NULL;

	if (strncmp(cmd, "interval", strlen("interval")) == 0) {
		/* interval prefix */
		p = cmd + strlen("interval");
		/* skip all spaces */
		while (isspace(*p))
			++p;
	}

	/* just interval without value, reset to default value */
	if (p && !*p)
		interval = BMWDT_DEFAULT_INTERVAL(atomic_read(&dev->timeout));
	else
		if (kstrtol(p, 0, &interval))
			return -EINVAL;

	if (interval >= atomic_read(&dev->timeout))
		return -EINVAL;

	atomic_set(&dev->interval, interval);
	return bmwdt_reset(dev);
}

struct bmwdt_cmd {
	char *cmd;
	int (*func)(struct bmwdt_dev *dev, const char *cmd);
};

static const struct bmwdt_cmd cmd_list[] = {
	{"kick", bmwdt_cmd_kick},
	{"manual", bmwdt_cmd_manual},
	{"auto", bmwdt_cmd_auto},
	{"enable", bmwdt_cmd_enable},
	{"disable", bmwdt_cmd_disable},
	{"timeout", bmwdt_cmd_timeout},
	{"interval", bmwdt_cmd_interval},
};

static void bmwdt_empty_work(struct work_struct *work)
{
	/* empty work */
	struct bmwdt_dev *dev;
	int cpu;

	dev = container_of(work, struct bmwdt_cpu_data, work)->dev;
	cpu = smp_processor_id();
	bmwdt_dev_dbg(dev, "cpu%d kick the dog\n", cpu);

	/* clear cpu mask */
	atomic_fetch_and(~BIT(cpu), &dev->cpumask);
}

static void bmwdt_kick_work(struct work_struct *work)
{
	/* deploy an empty work on all online cpus */
	int cpu;
	struct bmwdt_dev *dev;

	BMWDT_ENTER();

	dev = container_of(to_delayed_work(work), struct bmwdt_dev, dwork);

	bmwdt_dev_dbg(dev, "deploy work through cpu%d\n", smp_processor_id());

	atomic_set(&dev->cpumask, 0);

	cpus_read_lock();

	for_each_online_cpu(cpu) {
		struct bmwdt_cpu_data *cpu_data =
			per_cpu_ptr(dev->cpu_data, cpu);
		struct work_struct *work = &cpu_data->work;

		cpu_data->dev = dev;

		atomic_fetch_or(BIT(cpu), &dev->cpumask);
		INIT_WORK(work, bmwdt_empty_work);
		schedule_work_on(cpu, work);
	}

	/* wait all per cpu work done */
	for_each_online_cpu(cpu)
		flush_work(&per_cpu_ptr(dev->cpu_data, cpu)->work);

	cpus_read_unlock();

	/* BLUSH!, check if all cpu does do a work, I am not sure if
	 * schedule_work_on can always offload work to specfied cpu
	 */
	WARN_ONCE(atomic_read(&dev->cpumask),
		  "Oops! some works are not done by their cpus\n");

	/* kick dog */
	if (dev->wdd->ops->ping(dev->wdd))
		bmwdt_dev_warn(dev, "ping failed\n");

	schedule_delayed_work(&dev->dwork,
			      atomic_read(&dev->interval) * HZ);
}

static int bmwdt_run_cmd(struct bmwdt_dev *dev, const char *cmd)
{
	int i;
	int err = -EINVAL;

	mutex_lock(&dev->lock);
	for (i = 0; i < ARRAY_SIZE(cmd_list); ++i) {
		if (strncmp(cmd_list[i].cmd, cmd,
			    strlen(cmd_list[i].cmd)) == 0) {
			err = cmd_list[i].func(dev, cmd);
			break;
		}
	}

	if (i == ARRAY_SIZE(cmd_list)) {
		/* not found in command list, just a timeout number maybe
		 * compatible with lagecy driver, set watchdog timeout as
		 * default command
		 */
		err = bmwdt_cmd_timeout(dev, cmd);
	}
	mutex_unlock(&dev->lock);

	return err;
}

/* called after any parameter changed */
static int bmwdt_reset(struct bmwdt_dev *dev)
{
	struct watchdog_device *wdd = dev->wdd;
	const char *name = NULL;

	BMWDT_ENTER();

	if (wdd->parent)
		name = dev_name(wdd->parent);

	name = name ? name : "no-name";

	if (wdd->ops->stop(wdd))
		bmwdt_dev_warn(dev, "stop failed\n");

	cancel_delayed_work_sync(&dev->dwork);

	if (wdd->ops->set_timeout(wdd, atomic_read(&dev->timeout)))
		bmwdt_dev_warn(dev, "set timeout failed\n");
	if (wdd->ops->ping(wdd))
		bmwdt_dev_warn(dev, "set ping failed\n");

	if (atomic_read(&dev->enable))
		if (wdd->ops->start(wdd))
			bmwdt_dev_warn(dev, "start failed\n");

	if (atomic_read(&dev->automatic))
		schedule_delayed_work(&dev->dwork,
				      atomic_read(&dev->interval) * HZ);

	dev->msglen = snprintf(dev->msgbuf, sizeof(dev->msgbuf),
		BMWDT_NAME "-%d(%s) %s, mode: %s, timeout: %d, interval: %d\n",
		wdd->id, name,
		atomic_read(&dev->enable) ? "enabled" : "disabled",
		atomic_read(&dev->automatic) ? "auto" : "manual",
		atomic_read(&dev->timeout),
		atomic_read(&dev->interval));

	return 0;
}

static ssize_t bmwdt_write(struct file *file, const char __user *data,
			   size_t len, loff_t *ppos)
{
	char cmd[128];
	struct bmwdt_dev *dev = file->private_data;
	int err;

	BMWDT_ENTER();

	if (len >= sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(cmd, data, len))
		return -EFAULT;

	cmd[len] = 0;

	err = bmwdt_run_cmd(dev, cmd);

	return err ? err : len;
}

static ssize_t bmwdt_read(struct file *file, char __user *data,
			  size_t len, loff_t *ppos)
{
	struct bmwdt_dev *dev = file->private_data;
	int copied;

	/* all watchdog information */
	BMWDT_ENTER();

	copied = min_t(int, (dev->msglen - *ppos), len);

	if (copy_to_user(data, dev->msgbuf + *ppos, copied))
		return -EFAULT;

	*ppos += copied;

	return copied;
}

static int bmwdt_open(struct inode *inode, struct file *file)
{
	struct bmwdt_dev *dev;

	BMWDT_ENTER();

	dev = container_of(inode->i_cdev, struct bmwdt_dev, cdev);
	file->private_data = dev;

	return 0;
}

static int bmwdt_release(struct inode *inode, struct file *file)
{
	BMWDT_ENTER();
	return 0;
}

static const struct file_operations bmwdt_fops = {
	.owner		= THIS_MODULE,
	.write		= bmwdt_write,
	.read		= bmwdt_read,
	.open		= bmwdt_open,
	.release	= bmwdt_release,
};

static int bmwdt_cdev_register(struct watchdog_device *wdd, dev_t devno)
{
	struct bmwdt_dev *wd_data;
	int err;

	BMWDT_ENTER();

	wd_data = kzalloc(sizeof(struct bmwdt_dev), GFP_KERNEL);
	if (!wd_data)
		return -ENOMEM;

	mutex_init(&wd_data->lock);

	wd_data->wdd = wdd;
	wdd->wd_data = (void *)wd_data;

	/* Fill in the data structures */
	cdev_init(&wd_data->cdev, &bmwdt_fops);
	wd_data->cdev.owner = wdd->ops->owner;

	/* Add the device */
	err = cdev_add(&wd_data->cdev, devno, 1);
	if (err) {
		pr_err(BMWDT_NAME"%d unable to add device %d:%d\n",
			wdd->id,  MAJOR(ctx.devno), wdd->id);
		kfree(wd_data);
		return err;
	}

	atomic_set(&wd_data->enable, BMWDT_DEFAULT_STATUS);
	atomic_set(&wd_data->automatic, BMWDT_DEFAULT_MODE);
	atomic_set(&wd_data->timeout, BMWDT_DEFAULT_TIMEOUT);
	atomic_set(&wd_data->interval,
		   BMWDT_DEFAULT_INTERVAL(BMWDT_DEFAULT_TIMEOUT));
	INIT_DELAYED_WORK(&wd_data->dwork, bmwdt_kick_work);

	wd_data->cpu_data = alloc_percpu(struct bmwdt_cpu_data);
	if (!wd_data->cpu_data) {
		pr_err("allocate data for each cpu failed\n");
		kfree(wd_data);
		return -ENOMEM;
	}

	/* link to global context */
	mutex_lock(&ctx.lock);
	list_add_tail(&wd_data->head, &ctx.dev_list);
	mutex_unlock(&ctx.lock);

	return 0;
}

static void bmwdt_cdev_unregister(struct watchdog_device *wdd)
{
	struct bmwdt_dev *wd_data = (void *)wdd->wd_data;

	BMWDT_ENTER();

	mutex_lock(&ctx.lock);
	list_del(&wd_data->head);
	mutex_unlock(&ctx.lock);

	cdev_del(&wd_data->cdev);

	mutex_lock(&wd_data->lock);
	wd_data->wdd = NULL;
	wdd->wd_data = NULL;
	mutex_unlock(&wd_data->lock);

	kfree(wd_data);
}

static struct class bmwdt_class = {
	.name =		BMWDT_NAME,
	.owner =	THIS_MODULE,
	.dev_groups =	NULL,
};

/* try original linux system watchdog_register_device first.
 * if flaver = "strict" not in device true, register this watchdog to original
 * watchdog framework
 *
 * @retval true if device flavor is strict
 * @retval false if device flavor is original watchdog
 */
static int bmwdt_flavor_strict(struct watchdog_device *wdd)
{
	struct device_node *np;
	const char *flavor;
	int err;

	/* no device node found, aka no device tree */
	if (!wdd->parent)
		return false;

	np = wdd->parent->of_node;

	if (!np)
		return false;

	err = of_property_read_string(np, "flavor", &flavor);
	if (err)
		return false;

	pr_info("watchdog %s flavor %s\n", dev_name(wdd->parent), flavor);

	return strcasecmp(flavor, "strict") == 0;
}

int bmwdt_register_device(struct watchdog_device *wdd)
{
	struct device *dev;
	struct bmwdt_dev *bmdev;
	dev_t devno;
	int err, id = -1;

	BMWDT_ENTER();

	/* register this watchdog to original watchdog framework instead */
	if (!bmwdt_flavor_strict(wdd))
		return watchdog_register_device(wdd);

	/* assign an id to this watchdog device */
	/* Use alias for watchdog id if possible */
	if (wdd->parent) {
		err = of_alias_get_id(wdd->parent->of_node, "watchdog");
		if (err >= 0)
			id = ida_simple_get(&ctx.ida, err, err + 1, GFP_KERNEL);
	}

	if (id < 0)
		id = ida_simple_get(&ctx.ida, 0, BMWDT_DOG_MAX, GFP_KERNEL);

	if (id < 0)
		return id;
	wdd->id = id;

	devno = MKDEV(MAJOR(ctx.devno), wdd->id);

	err = bmwdt_cdev_register(wdd, devno);
	if (err) {
		ida_simple_remove(&ctx.ida, wdd->id);
		return err;
	}

	bmdev = (struct bmwdt_dev *)wdd->wd_data;
	err = bmwdt_reset(bmdev);
	if (err) {
		pr_err("start kick work failed\n");
		ida_simple_remove(&ctx.ida, wdd->id);
		bmwdt_cdev_unregister(wdd);
		return err;
	}

	dev = device_create_with_groups(&bmwdt_class, wdd->parent,
					devno, wdd, wdd->groups,
					BMWDT_NAME "-%d", wdd->id);
	if (IS_ERR(dev)) {
		/* stop watchdog */
		atomic_set(&bmdev->enable, false);
		/* disable auto kick */
		atomic_set(&bmdev->automatic, false);
		bmwdt_reset(bmdev);
		ida_simple_remove(&ctx.ida, wdd->id);
		bmwdt_cdev_unregister(wdd);
		return PTR_ERR(dev);
	}

	return err;
}
EXPORT_SYMBOL_GPL(bmwdt_register_device);

void bmwdt_unregister_device(struct watchdog_device *wdd)
{
	struct bmwdt_dev *dev = (struct bmwdt_dev *)(wdd->wd_data);

	BMWDT_ENTER();

	if (!bmwdt_flavor_strict(wdd)) {
		watchdog_unregister_device(wdd);
		return;
	}

	atomic_set(&dev->automatic, false);
	bmwdt_reset(dev);

	bmwdt_cdev_unregister(wdd);
	device_destroy(&bmwdt_class, dev->cdev.dev);
	ida_simple_remove(&ctx.ida, wdd->id);
}
EXPORT_SYMBOL_GPL(bmwdt_unregister_device);

static int __init bmwdt_init(void)
{
	int err;

	BMWDT_ENTER();

	err = class_register(&bmwdt_class);
	if (err < 0) {
		pr_err("could not register class\n");
		return err;
	}

	err = alloc_chrdev_region(&ctx.devno, 0, BMWDT_DOG_MAX, BMWDT_NAME);
	if (err < 0) {
		pr_err("unable to allocate char dev region\n");
		goto err_alloc;
	}

	INIT_LIST_HEAD(&ctx.dev_list);
	mutex_init(&ctx.lock);
	ida_init(&ctx.ida);

	return 0;

err_alloc:
	class_unregister(&bmwdt_class);
	return err;
}

static void __exit bmwdt_exit(void)
{
	BMWDT_ENTER();

	mutex_destroy(&ctx.lock);

	unregister_chrdev_region(ctx.devno, BMWDT_DOG_MAX);
	class_unregister(&bmwdt_class);
}

subsys_initcall_sync(bmwdt_init);
module_exit(bmwdt_exit);

MODULE_AUTHOR("Chao Wei <chao.wei@sophgo.com>");
MODULE_DESCRIPTION("Bitmain Watchdog Framework");
MODULE_LICENSE("GPL");
