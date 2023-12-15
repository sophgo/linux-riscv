#define pr_fmt(fmt) "sophgo-wdt:"fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/watchdog.h>
#include "bmwdt.h"

/* magic number for different type of watchdogs, no need now */
/* this byte is used to handle some different software implementations of wdt */
#define WDT_MAGIC_REG	(0)
/* write 1 to this register will enable watchdog */
#define WDT_ENABLE_REG	(1)
/* write to this register will result in reloading timeout to counter */
#define WDT_RELOAD_REG	(2)
/* offset 3 is reserved */
/* write to this register */
#define WDT_TIMEOUT_REG	(4)
#define WDT_COUNTER_REG	(8)

static inline struct i2c_client *wdt_i2c(struct watchdog_device *wdd)
{
	return wdd->driver_data;
}

static inline void wdt_counter2byte(u8 *byte, u32 counter)
{
	byte[0] = counter & 0xff;
	byte[1] = (counter >> 8) & 0xff;
	byte[2] = (counter >> 16) & 0xff;
	byte[3] = (counter >> 24) & 0xff;
}

static inline u32 wdt_byte2counter(u8 *byte)
{
	return byte[0] | (byte[1] << 8) | (byte[2] << 16) | (byte[3] << 24);
}

static int wdt_start(struct watchdog_device *wdd)
{
	return i2c_smbus_write_byte_data(wdt_i2c(wdd), WDT_ENABLE_REG, 1);
}

static int wdt_stop(struct watchdog_device *wdd)
{
	return i2c_smbus_write_byte_data(wdt_i2c(wdd), WDT_ENABLE_REG, 0);
}

static int wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	u8 tmp[4];

	wdt_counter2byte(tmp, t);

	return i2c_smbus_write_i2c_block_data(wdt_i2c(wdd), WDT_TIMEOUT_REG,
					      4, tmp);
}

static unsigned int wdt_get_timeleft(struct watchdog_device *wdd)
{
	u8 tmp[4];
	int err;

	err = i2c_smbus_read_i2c_block_data(wdt_i2c(wdd), WDT_COUNTER_REG,
					    4, tmp);
	if (err < 0)
		return err;
	return wdt_byte2counter(tmp);
}

static int wdt_ping(struct watchdog_device *wdd)
{
	/* reload */
	return i2c_smbus_write_byte_data(wdt_i2c(wdd), WDT_RELOAD_REG, 1);
}

static struct watchdog_info wdi = {
	.identity = "Sophgo I2C Watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static struct watchdog_ops wdo = {
	.owner		= THIS_MODULE,
	.start		= wdt_start,
	.stop		= wdt_stop,
	.ping		= wdt_ping,
	.set_timeout	= wdt_set_timeout,
	.get_timeleft	= wdt_get_timeleft,
};

static int wdt_reset(struct i2c_client *i2c)
{
	int err;
	u8 tmp[4];

	/* reset watchdog to given status */
	err = i2c_smbus_write_byte_data(i2c, WDT_ENABLE_REG, 0);
	if (err < 0)
		return err;

	wdt_counter2byte(tmp, 0xffffffff);
	err =  i2c_smbus_write_i2c_block_data(i2c, WDT_TIMEOUT_REG,
					      4, tmp);
	if (err < 0)
		return err;

	/* reload */
	err = i2c_smbus_write_byte_data(i2c, WDT_RELOAD_REG, 1);
	if (err < 0)
		return err;

	return 0;
}

static int wdt_probe(struct i2c_client *i2c,
			const struct i2c_device_id *ignored)
{
	struct watchdog_device *wdd;
	int err;

	wdd = devm_kzalloc(&i2c->dev, sizeof(*wdd), GFP_KERNEL);
	if (wdd == NULL)
		return -ENOMEM;

	err = wdt_reset(i2c);
	if (err)
		return err;

	/* remember i2c client */
	wdd->driver_data = i2c;
	wdd->info = &wdi;
	wdd->ops = &wdo;
	wdd->min_timeout = 1;
	wdd->timeout = 0xffffffff;
	wdd->max_timeout = 0xffffffff;
	wdd->parent = &i2c->dev;

#ifdef CONFIG_ARCH_SOPHGO
	err = bmwdt_register_device(wdd);
#else
	err = watchdog_register_device(wdd);
#endif
	if (err) {
		dev_err(&i2c->dev, "failed to register watchdog device\n");
		return err;
	}

	i2c_set_clientdata(i2c, wdd);

	return 0;
}

static void wdt_remove(struct i2c_client *i2c)
{
#ifdef CONFIG_ARCH_SOPHGO
	bmwdt_unregister_device(i2c_get_clientdata(i2c));
#else
	watchdog_unregister_device(i2c_get_clientdata(i2c));
#endif
	wdt_reset(i2c);
}

static const struct of_device_id wdt_dt_table[] = {
	{ .compatible = "sophgo,sg20xx-wdt" },
	{},
};

static const struct i2c_device_id wdt_id_table[] = {
	{ "sophgo-i2c-watchdog", 0 },
	{},
};

static struct i2c_driver wdt_drv = {
	.driver = {
		.name = "sophgo-i2c-watchdog",
		.of_match_table = wdt_dt_table,
	},
	.probe = wdt_probe,
	.remove = wdt_remove,
	.id_table = wdt_id_table,
};

module_i2c_driver(wdt_drv);

MODULE_DESCRIPTION("watchdog driver for sophgo i2c based watchdog");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chao.Wei@sophgo.com>");
