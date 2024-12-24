// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "sg_common.h"
#include "sg_fw.h"
#include "sg_io.h"

#ifdef PLAT_BM1686
static void sgdrv_start_tpu_scalar(struct sg_dev *sgdev)
{
	u64 rvba_phys = TPU_SCALAR_RVBA_ADDR;
	u32 reset_base = (rvba_phys >> 4) & 0xffffffff;
	u32 tmp;

	top_reg_write(sgdev, TOP_GP_REG14_A53LITE_IRQ_CLEAR_OFFSET, (0x1 << 3));
	top_reg_write(sgdev, 0x03c, reset_base);

	tmp = top_reg_read(sgdev, TOP_SW_RESET);
	tmp &= ~(1 << 1);
	top_reg_write(sgdev, TOP_SW_RESET, tmp);
	mdelay(100);
	tmp |= 1 << 1;
	top_reg_write(sgdev, TOP_SW_RESET, tmp);
	mdelay(100);
}

static void sgdrv_stop_tpu_scalar(struct sg_dev *sgdev)
{
	u32 ctrl_word;

	/*send fiq 6 to tpu scalar, let it get ready to die*/
	top_reg_write(sgdev, TOP_GP_REG14_A53LITE_IRQ_SET_OFFSET, 0x1 << 3);

	udelay(50);

	/* reset tpu scalar */
	ctrl_word = top_reg_read(sgdev, TOP_SW_RESET);
	ctrl_word &= ~(1 << 1);
	top_reg_write(sgdev, TOP_SW_RESET, ctrl_word);
}
#else
static void sgdrv_start_tpu_scalar(struct sg_dev *sgdev)
{
	u64 rvba_phys = TPU_SCALAR_RVBA_ADDR;
	int core_id;

	for (core_id = 0; core_id < sgdev->tpu_num; core_id++) {

		// rxu bypass
		tpu_sys_reg_write(sgdev, TPU_SYS_REG_DUMMY, 0, core_id);
		// rvba
		tpu_sys_reg_write(sgdev, TPU_SYS_REG_RVBA_L32,
			(rvba_phys + core_id * TPU_FW_OFFSET) & 0xffffffff, core_id);
		tpu_sys_reg_write(sgdev, TPU_SYS_REG_RVBA_H32,
			(rvba_phys + core_id * TPU_FW_OFFSET) >> 32, core_id);
		// clk
		tpu_sys_reg_setbits(sgdev, TPU_SYS_REG_CLK_EN, 0x10007f, core_id);
		// reset
		tpu_sys_reg_clrbits(sgdev, TPU_SYS_REG_RST, 0xc03000fb, core_id);
	}
}

static void sgdrv_stop_tpu_scalar(struct sg_dev *sgdev)
{
	// int core_id;
	// for (core_id = 0; core_id < sgdev->tpu_num; core_id++)
	// tpu_sys_reg_write(sgdev, TPU_SYS_REG_RST, 0xffffffff, core_id);
	pr_info("tpu scalar dummy stop\n");
}
#endif

// TODO
// comment for compile unused warning
#if 0
static void sgdrv_init_msgfifo_info(void)
{
	void __iomem *msgfifo_base_virt;
	u64 msgfifo_base_phys = 0x400000000;
	int msgfifo_info_size = 0x1000;

	msgfifo_base_virt = ioremap(msgfifo_base_phys, msgfifo_info_size);
	// memset(msgfifo_base_virt, 0, msgfifo_info_size);
	*(u64 *)((unsigned long)msgfifo_base_virt + 0x38) = 0;
	*(u64 *)((unsigned long)msgfifo_base_virt + 0x40) = 0;
	iounmap(msgfifo_base_virt);
}
#endif
void sgdrv_set_fw_mode(struct sg_dev *sgdev, struct platform_device *pdev)
{
	struct device_node *tpu_node;
	u32 val = 0;

	tpu_node = of_node_get(pdev->dev.of_node);

	of_property_read_u32(tpu_node, "sophgo_fw_mode", &val);
	pr_info("tpu scalar firmware mode from dtb is 0x%x\n", val);

	gp_reg_write(sgdev, GP_REG_FW_MODE, val);
	pr_info("set tpu scalar firmware mode to 0x%x\n", val);
}

static int sgdrv_wait_fwinit_done(struct sg_dev *sgdev)
{
	int cnt = 5000;
	int polling_ms = 1;
	u32 fw_mask = ~(0xf << 28);
	int i = 0;
	bool done_flag = false;

	while (!done_flag) {
		done_flag = true;
		for (i = 0; i < sgdev->tpu_num; i++) {
			if ((gp_reg_read(sgdev, 3 + i) & fw_mask) != (LAST_INI_REG_VAL & fw_mask))
				done_flag = false;
		}
		mdelay(polling_ms);
		if (--cnt == 0)
			break;
	}

	if (cnt) {
		pr_info("sgdrv: %s firmware init done!\n", sgdev->dev_name);
		return 0;
	}
	for (i = 0; i < sgdev->tpu_num; i++) {
		// TODO
		if ((gp_reg_read(sgdev, 3 + i) & fw_mask) != (LAST_INI_REG_VAL & fw_mask))
			pr_err("sgdrv: tp%d firmware init timeout!\n", i);
	}

	return -EBUSY;
}

#ifdef PLAT_BM1686
static int sgdrv_fw_download(struct sg_dev *sgdev)
{
	const char *bm1684x_dyn_fw = "bm1684x_firmware.bin";
	const struct firmware *fw;
	u64 rvba_phys = TPU_SCALAR_RVBA_ADDR;
	void __iomem *rvba_virt;
	int ret;

	ret = request_firmware(&fw, bm1684x_dyn_fw, sgdev->dev);
	if (ret != 0) {
		pr_info("%s request_firmware fail, check /lib/firmware\n", sgdev->dev_name);
		return -1;
	}

	rvba_virt = ioremap(rvba_phys, 16 * 1024 * 1024);
	memcpy(rvba_virt, (unsigned int *)(fw->data), fw->size);
	iounmap(rvba_virt);

	release_firmware(fw);

	return ret;
}
#else

static int sgdrv_request_and_load_firmware(struct sg_dev *sgdev,
			const char *fw_name, unsigned long long load_addr)
{
	const struct firmware *fw;
	void __iomem *rvba_virt;
	int ret = 0;

	ret = request_firmware(&fw, fw_name, sgdev->dev);
	if (ret != 0) {
		pr_info(" request_firmware fail, please check if these is firmware in /lib/firmware !!\n");
		return -1;
	}

	pr_info("fw_name: %s, fw->size: 0x%lx load_addr: 0x%llx\n", fw_name, fw->size, load_addr);

	rvba_virt = ioremap(load_addr, fw->size);
	memcpy_toio(rvba_virt, fw->data, fw->size);
	release_firmware(fw);
	iounmap(rvba_virt);

	return ret;
}

static int sgdrv_fw_download(struct sg_dev *sgdev)
{
	const char *zsbl_name = "tp_zsbl.bin";
	const char *tp_dtb_name = "bm1690-cdm-tp.dtb";
	const char *opensbi_name = "fw_dynamic.bin";
	const char *tp_image = "tp_Image";
	const char *tp_ramdisk_name = "tp_rootfs.cpio";
	u64 zsbl_offset = 0x0ull;
	u64 opensbi_offset = 0x1000000ull;
	u64 tp_image_offset = 0x1200000ull;
	u64 tp_dtb_offset = 0x8000000ull;
	u64 tp_ramdisk_offset = 0xb000000ull;
	u64 rvba_phys = TPU_SCALAR_RVBA_ADDR;
	int i = 0;
	int ret = 0;

	for (i = 0; i < sgdev->tpu_num; i++) {
		rvba_phys = TPU_SCALAR_RVBA_ADDR + i * TPU_FW_OFFSET;
		ret = sgdrv_request_and_load_firmware(sgdev, zsbl_name, rvba_phys + zsbl_offset);
		if (ret != 0) {
			pr_info("%s request_firmware fail, check /lib/firmware\n", sgdev->dev_name);
			return -1;
		}

		ret = sgdrv_request_and_load_firmware(sgdev, opensbi_name, rvba_phys + opensbi_offset);
		if (ret != 0) {
			pr_info("%s request_firmware fail, check /lib/firmware\n", sgdev->dev_name);
			return -1;
		}

		ret = sgdrv_request_and_load_firmware(sgdev, tp_image, rvba_phys + tp_image_offset);
		if (ret != 0) {
			pr_info("%s request_firmware fail, check /lib/firmware\n", sgdev->dev_name);
			return -1;
		}

		ret = sgdrv_request_and_load_firmware(sgdev, tp_dtb_name, rvba_phys + tp_dtb_offset);
		if (ret != 0) {
			pr_info("%s request_firmware fail, check /lib/firmware\n", sgdev->dev_name);
			return -1;
		}

		ret = sgdrv_request_and_load_firmware(sgdev, tp_ramdisk_name, rvba_phys + tp_ramdisk_offset);
		if (ret != 0) {
			pr_info("%s request_firmware fail, check /lib/firmware\n", sgdev->dev_name);
			return -1;
		}
	}
	return ret;
}

static void sgdrv_fw_gp_init(struct sg_dev *sgdev)
{
	size_t i = 0;

	for (i = 0; i < sgdev->tpu_num; i++)
		gp_reg_write(sgdev, 3 + i, FW_START);
}
#endif

int sgdrv_fw_load(struct sg_dev *sgdev)
{
	int ret = 0;
	pr_info("sgdrv: probe %d tpu\n", sgdev->tpu_num);
	sgdrv_stop_tpu_scalar(sgdev);

	sgdrv_fw_gp_init(sgdev);
	ret = sgdrv_fw_download(sgdev);
	if (ret) {
		pr_err("sgdrv: firmware download failed!\n");
		return ret;
	}

	sgdrv_start_tpu_scalar(sgdev);
	pr_info("start_tpu_scalar\n");

	ret = sgdrv_wait_fwinit_done(sgdev);
	if (ret) {
		pr_err("sgdrv: firmware load timeout!\n");
		return ret;
	}
	pr_info("sgdrv: firmware load success!\n");
	return ret;
}

void sgdrv_fw_unload(struct sg_dev *sgdev)
{
	sgdrv_stop_tpu_scalar(sgdev);
}
