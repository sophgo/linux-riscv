#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/nvmem-provider.h>

/* eeprom protocol:
 * random read
 * [START] [SLAVE-ADDR] [W] [ACK] [OFFSET-HIGH] [ACK] [OFFSET_LOW] [ACK]
 *	[REPEAT-START] [SLAVE-ADDR] [R] [ACK] [DATA0] [ACK] [DATA1] [ACK] ...
 *	[DATAn] [NACK] [STOP]
 * random write
 * [START] [SLAVE-ADDR] [W] [ACK] [OFFSET-HIGH] [ACK] [OFFSET_LOW] [ACK]
 *	[DATA0] [ACK] [DATA1] [ACK] ... [DATAn] [NACK] [SOP]
 */

struct eeprom_ctx {
	struct nvmem_config nvcfg;
	struct nvmem_device *nvdev;
	struct i2c_client *i2c;
	u8 buf[0];
};

#define EEPROM_BUF_SIZE	(PAGE_SIZE - sizeof(struct eeprom_ctx))

static int eeprom_reset(struct i2c_client *i2c)
{
	struct i2c_msg msg;
	u8 offset[2];
	int err;

	memset(&msg, 0, sizeof(msg));
	memset(offset, 0, sizeof(offset));

	/* 16bits offset, big endian */
	msg.addr = i2c->addr;
	msg.buf = offset;
	msg.len = sizeof(offset);
	msg.flags = 0;

	err = i2c_transfer(i2c->adapter, &msg, 1);
	if (err != 1)
		return err < 0 ? err : -EIO;
	return 0;

}

static int eeprom_read(void *priv, unsigned int off, void *val, size_t count)
{
	struct eeprom_ctx *ctx = priv;
	struct i2c_client *i2c = ctx->i2c;
	struct i2c_msg msg[2];
	u8 offset_buf[2];
	int err;

	offset_buf[0] = (off >> 8) & 0xff;
	offset_buf[1] = off & 0xff;

	memset(msg, 0, sizeof(msg));
	/* 16bits offset, big endian */
	msg[0].addr = i2c->addr;
	msg[0].buf = offset_buf;
	msg[0].len = sizeof(offset_buf);
	msg[0].flags = 0;

	/* data to be read */
	msg[1].addr = i2c->addr;
	msg[1].buf = val;
	msg[1].len = count;
	msg[1].flags = I2C_M_RD;

	err = i2c_transfer(i2c->adapter, msg, ARRAY_SIZE(msg));
	if (err != ARRAY_SIZE(msg))
		return err < 0 ? err : -EIO;
	return 0;
}

static int eeprom_write(void *priv, unsigned int off, void *val, size_t count)
{
	struct eeprom_ctx *ctx = priv;
	struct i2c_client *i2c = ctx->i2c;
	struct i2c_msg msg;
	int err;

	ctx->buf[0] = (off >> 8) & 0xff;
	ctx->buf[1] = off & 0xff;
	if (count > (EEPROM_BUF_SIZE - 2))
		return -ENOMEM;
	memcpy(ctx->buf + 2, val, count);
	memset(&msg, 0, sizeof(msg));

	/* 16bits offset, big endian */
	msg.addr = i2c->addr;
	msg.buf = ctx->buf;
	msg.len = count + 2;
	msg.flags = 0;

	err = i2c_transfer(i2c->adapter, &msg, 1);
	if (err != 1)
		return err < 0 ? err : -EIO;
	return 0;
}

static int eeprom_probe(struct i2c_client *i2c,
			const struct i2c_device_id *ignored)
{
	struct eeprom_ctx *ctx;
	struct nvmem_config *cfg;
	/* default size */
	int size = 256;
	struct device_node *node = i2c->dev.of_node;
	const __be32 *val;

	if (eeprom_reset(i2c))
		return -ENODEV;

	if (node) {
		val =  of_get_property(node, "size", NULL);
		if (val)
			size = be32_to_cpup(val);
	}

	ctx = devm_kzalloc(&i2c->dev, PAGE_SIZE, GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;

	/* remember i2c client */
	ctx->i2c = i2c;
	i2c_set_clientdata(i2c, ctx);

	cfg = &ctx->nvcfg;
	cfg->name = dev_name(&i2c->dev);
	cfg->dev = &i2c->dev;
	cfg->read_only = false;
	cfg->root_only = true;
	cfg->owner = THIS_MODULE;
	cfg->compat = true;
	cfg->base_dev = &i2c->dev;
	cfg->reg_read = eeprom_read;
	cfg->reg_write = eeprom_write;
	cfg->priv = ctx;
	cfg->stride = 1;
	cfg->word_size = 1;
	cfg->size = size;
	ctx->nvdev = nvmem_register(cfg);
	if (IS_ERR(ctx->nvdev)) {
		dev_err(&i2c->dev, "failed to register nvmem device\n");
		return PTR_ERR(ctx->nvdev);
	}

	return 0;
}

static void eeprom_remove(struct i2c_client *i2c)
{
	nvmem_unregister(((struct eeprom_ctx *)i2c_get_clientdata(i2c))->nvdev);
}

static const struct of_device_id eeprom_dt_table[] = {
	{ .compatible = "sophgo,sg20xx-eeprom" },
	{},
};

static const struct i2c_device_id eeprom_id_table[] = {
	{ "sophgo-i2c-eeprom", 0 },
	{},
};

static struct i2c_driver eeprom_drv = {
	.driver = {
		.name = "sophgo-i2c-eeprom",
		.of_match_table = eeprom_dt_table,
	},
	.probe = eeprom_probe,
	.remove = eeprom_remove,
	.id_table = eeprom_id_table,
};

module_i2c_driver(eeprom_drv);

MODULE_DESCRIPTION("mvmem driver for sophgo i2c based eeprom");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chao.Wei@sophgo.com>");
