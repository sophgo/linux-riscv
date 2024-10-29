#include <linux/module.h>
#include <linux/init.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/bitops.h>

#define EFUSE_MODE		0
#define EFUSE_ADR		0x4
#define EFUSE_RD_DATA		0xc

struct sg_efuse_device {
	struct device *dev;
	void __iomem *regs;

	u32 num_addr_bits;
	/*number of hard macro cells*/
	u32 num_cells;
	/*width in bytes of hard macro cells*/
	u32 cell_width;

	struct nvmem_device *nvdev;

	struct mutex efuse_mutex;
};

static inline void efuse_write32(struct sg_efuse_device *efuse, uint32_t offset, uint32_t v)
{
	writel(v, efuse->regs + offset);
}

static inline uint32_t efuse_read32(struct sg_efuse_device *efuse, uint32_t offset)
{
	return readl(efuse->regs + offset);
}

static void efuse_mode_wait_ready(struct sg_efuse_device *efuse)
{
	while ((efuse_read32(efuse, EFUSE_MODE) & 0b11) != 0)
		;
}

static void efuse_mode_reset(struct sg_efuse_device *efuse)
{
	efuse_write32(efuse, EFUSE_MODE, 0);
	efuse_mode_wait_ready(efuse);
}

static void efuse_mode_md_write(struct sg_efuse_device *efuse, u32 val)
{
	u32 mode = efuse_read32(efuse, EFUSE_MODE);
	u32 new = (mode & 0xfffffffc) | (val & 0b11);

	efuse_write32(efuse, EFUSE_MODE, new);
}

static u32 make_adr_val(struct sg_efuse_device *efuse, u32 address, u32 bit_i)
{
	const u32 address_mask = (1 << efuse->num_addr_bits) - 1;

	return (address & address_mask) |
		((bit_i & 0x1f) << efuse->num_addr_bits);
}

static void efuse_set_bit(struct sg_efuse_device *efuse, uint32_t address, uint32_t bit_i)
{
	u32 adr_val = make_adr_val(efuse, address, bit_i);

	efuse_write32(efuse, EFUSE_ADR, adr_val);
	efuse_mode_md_write(efuse, 0b11);
	efuse_mode_wait_ready(efuse);
}

/*used to read hard macro address*/
static u32 efuse_embedded_read(struct sg_efuse_device *efuse, u32 address)
{
	u32 adr_val;
	u32 ret =  0;

	mutex_lock(&efuse->efuse_mutex);

	efuse_mode_reset(efuse);
	adr_val = make_adr_val(efuse, address, 0);
	efuse_write32(efuse, EFUSE_ADR, adr_val);
	efuse_mode_md_write(efuse, 0b10);
	efuse_mode_wait_ready(efuse);
	ret = efuse_read32(efuse, EFUSE_RD_DATA);

	mutex_unlock(&efuse->efuse_mutex);

	return ret;
}

/*used to write hard macro address*/
static void efuse_embedded_write(struct sg_efuse_device *efuse, u32 address, u32 val)
{
	mutex_lock(&efuse->efuse_mutex);

	for (int i = 0; i < 32; i++)
		if ((val >> i) & 1)
			efuse_set_bit(efuse, address, i);

	mutex_unlock(&efuse->efuse_mutex);
}

static u32 nvmem_read_cell(struct sg_efuse_device *efuse, int offset)
{
	return efuse_embedded_read(efuse, offset);
}

static void nvmem_write_cell(struct sg_efuse_device *efuse, int offset, u32 value)
{
	efuse_embedded_write(efuse, offset, value);
}

static int sophgo_nvmem_read(void *context, unsigned int off, void *val, size_t count)
{
	int size, left, start, i;
	u32 tmp;
	u8 *dst;
	struct sg_efuse_device *efuse = context;

	left = count;
	dst = val;
	/* head */
	if (off & 0x03) {
		size = min_t(int, 4 - (off & 0x03), left);
		start = (off & 0x03);
		tmp = nvmem_read_cell(efuse, off >> 2);
		memcpy(dst, &((u8 *)&tmp)[start], size);
		dst += size;
		left -= size;
		off += size;
	}

	/* body */
	size = left >> 2;
	for (i = 0; i < size; ++i) {
		tmp = nvmem_read_cell(efuse, off >> 2);
		memcpy(dst, &tmp, 4);
		dst += 4;
		left -= 4;
		off += 4;
	}

	/* tail */
	if (left) {
		tmp = nvmem_read_cell(efuse, off >> 2);
		memcpy(dst, &tmp, left);
	}


	return 0;
}

static int sophgo_nvmem_write(void *context, unsigned int off, void *val, size_t count)
{
	int size, left, start, i;
	u32 tmp;
	u8 *dst;
	struct sg_efuse_device *efuse = context;

	left = count;
	dst = val;
	/* head */
	if (off & 0x03) {
		tmp = 0;
		size = min_t(int, 4 - (off & 0x03), left);
		start = (off & 0x03);
		memcpy(&((u8 *)&tmp)[start], dst, size);
		nvmem_write_cell(efuse, off >> 2, tmp);
		dst += size;
		left -= size;
		off += size;
	}

	/* body */
	size = left >> 2;
	for (i = 0; i < size; ++i) {
		memcpy(&tmp, dst, 4);
		nvmem_write_cell(efuse, off >> 2, tmp);
		dst += 4;
		left -= 4;
		off += 4;
	}

	/* tail */
	if (left) {
		tmp = 0;
		memcpy(&tmp, dst, left);
		nvmem_write_cell(efuse, off >> 2, tmp);
	}

	return 0;
}

static int sg_efuse_probe(struct platform_device *pdev)
{
	struct clk *base_clk, *apb_clk;
	int ret;
	struct nvmem_config sg_efuse_cfg;
	struct nvmem_device *nvmem;
	struct sg_efuse_device *efuse;

	efuse = devm_kzalloc(&pdev->dev, sizeof(*efuse), GFP_KERNEL);

	efuse->regs = devm_platform_ioremap_resource(pdev, 0);

	if (IS_ERR(efuse->regs))
		return PTR_ERR(efuse->regs);

	device_property_read_u32(&pdev->dev, "num_cells", &efuse->num_cells);
	efuse->num_addr_bits = ilog2(efuse->num_cells);

	mutex_init(&efuse->efuse_mutex);

	base_clk = devm_clk_get(&pdev->dev, "base_clk");
	if (IS_ERR(base_clk)) {
		dev_warn(&pdev->dev, "cannot get efuse base clk\n");
	} else {
		ret = clk_prepare_enable(base_clk);
		if (ret < 0)
			dev_err(&pdev->dev, "failed to enable base clock\n");
	}

	apb_clk = devm_clk_get(&pdev->dev, "apb_clk");
	if (IS_ERR(apb_clk)) {
		dev_warn(&pdev->dev, "cannot get efuse apb clk\n");
	} else {
		ret = clk_prepare_enable(apb_clk);
		if (ret < 0)
			dev_err(&pdev->dev, "failed to enable apb clock\n");
	}

	sg_efuse_cfg.name = "sophgo_efuse";
	sg_efuse_cfg.id = NVMEM_DEVID_AUTO;
	sg_efuse_cfg.read_only = false,
	sg_efuse_cfg.root_only = true,
	sg_efuse_cfg.reg_read = sophgo_nvmem_read,
	sg_efuse_cfg.reg_write = sophgo_nvmem_write,
	sg_efuse_cfg.size = efuse->num_cells * 4,
	/* efuse is 32bits per cell, but symmetric keys in real word usually
	 * byte sequence, so i implements a least significant byte first scheme.
	 * for symmetric key stored msb in efuse.
	 */
	sg_efuse_cfg.word_size = 1,
	sg_efuse_cfg.stride = 1,
	sg_efuse_cfg.dev = &pdev->dev;
	sg_efuse_cfg.priv = efuse;

	nvmem = devm_nvmem_register(&pdev->dev, &sg_efuse_cfg);
	if (IS_ERR(nvmem)) {
		dev_err(&pdev->dev, "failed to register nvmem\n");
		return PTR_ERR(nvmem);
	}

	efuse->nvdev = nvmem;

	return 0;
}

static const struct of_device_id sg_efuse_dt_ids[] = {
	{
		.compatible = "sg,efuse",
	},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, sg_efuse_dt_ids);

static struct platform_driver sg_efuse_platform_driver = {
	.probe = sg_efuse_probe,
	.driver = {
		.name = "sg,efuse",
		.of_match_table = sg_efuse_dt_ids,
	},
};

module_platform_driver(sg_efuse_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhencheng.zhang");
MODULE_DESCRIPTION("Sophgo Efuse NVMEM Driver");
