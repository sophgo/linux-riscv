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
#include <linux/delay.h>

#define ECC_REGION_0    0xE000000  // 511M = 0x1FF00000, should be multiple of 7
#define ECC_REGION_1    0xE000000
#define ECC_REGION_2    0xE000000
#define ECC_REGION_3    0xE000000

int link_ecc_beat_list_5x[] = {0x3, 0x1c, 0x5, 0x1a, 0x6, 0x19, 0x7, 0x18, 0x9, 0x16, 0xa, 0x15, 0xb, 0x14, 0xc, 0x13};
uint32_t link_ecc_beat_list_5x_num = sizeof(link_ecc_beat_list_5x)/sizeof(int);

struct ddrc_info {
	void __iomem *base;
	int ddrc_id;
	int irq;
	uint32_t trigger_error_index;
	char name[32];
};

static ssize_t ddrc_trigger_error_store(struct device *dev,
				  struct device_attribute *attr, const char *ubuf, size_t len)
{
	struct ddrc_info *ddrc_info = dev_get_drvdata(dev);
	char buf[32] = {0};
	int val = 0;
	int ret;

	pr_err("ddrc trigger store\n");
	memcpy(buf, ubuf, len);
	ret = kstrtoint(buf, 0, &val);

	if (val != 0) {
		pr_err("[sophgo ddrc%d]: write %d to 0x%llx tirgger error\n", ddrc_info->ddrc_id, val,
			(uint64_t)ddrc_info->base + ddrc_info->trigger_error_index);
		writel(val, ddrc_info->base + ddrc_info->trigger_error_index);

		return len;
	}

	pr_err("please echo 1 for trigger error\n");

	return -EINVAL;
}

static ssize_t ddrc_trigger_error_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return 0;
}

static void intr_type(uint32_t top_intr)
{
	static const char * const intr_type[] = {
		"dfi_error_intr at channel 0",
		"dfi_error_intr at channel 1",
		"derate_temp_limit_intr at channel 0",
		"derate_temp_limit_intr at channel 1",
		"rd_linkecc_uncorr_err_intr at channel 0",
		"rd_linkecc_uncorr_err_intr at channel 1",
		"rd_linkecc_corr_err_intr at channel 0",
		"rd_linkecc_corr_err_intr at channel 1",
		"ecc_uncorrected_err_intr at channel 0",
		"ecc_uncorrected_err_intr at channel 1",
		"ecc_corrected_err_intr at channel 0",
		"ecc_corrected_err_intr at channel 1",
		"ecc_ap_err_intr at channel 0",
		"ecc_ap_err_intr at channel 1",
	};

	if (!top_intr)
		pr_err("no_error");
	else {
		for (int i = 0; i < (ARRAY_SIZE(intr_type)); i++) {
			if (top_intr & 0x1)
				pr_err("%s ", intr_type[i]);
			top_intr = top_intr >> 1;
			if (!top_intr)
				break;
		}
	}
}

static uint32_t shift_to_mask_bit(uint32_t mask_bit, uint32_t shift_data)
{
	uint32_t value, index, buffer = 0;

	for (index = 0; index < 32; index++) {
		buffer = mask_bit;
		if ((buffer >> index) & 0x01)
			break;
	}
	value = shift_data << index;
	return value;
}

static void lpddr5x_inline_ecc_error_check_sub(int ddr_num, void *dev_id)
{
	uint32_t ddr_sys_index;
	uint32_t ctl_index, top_intr;
	uint64_t ddr_addr;
	uint32_t ecc_ap_err;
	uint32_t ecc_ap_err_threshold;
	bool ecc_uncorr_error;
	uint32_t ecc_uncorr_err_cnt;
	uint32_t ecc_uncorr_syndrome_31_0;
	uint32_t ecc_uncorr_syndrome_63_32;
	uint32_t ecc_uncorr_syndrome_71_64;
	uint32_t ecc_uncorr_rank;
	uint32_t ecc_uncorr_bg;
	uint32_t ecc_uncorr_ba;
	uint32_t ecc_uncorr_row;
	uint32_t ecc_uncorr_col;
	uint32_t ecc_uncorrected_err;
	bool ecc_corr_error;
	uint32_t ecc_corr_err_cnt;
	uint32_t ecc_corr_syndrome_31_0;
	uint32_t ecc_corr_syndrome_63_32;
	uint32_t ecc_corr_syndrome_71_64;
	uint32_t ecc_corr_bit_mask_31_0;
	uint32_t ecc_corr_bit_mask_63_32;
	uint32_t ecc_corr_bit_mask_71_64;
	uint32_t ecc_corrected_bit_num;
	uint32_t ecc_corr_rank;
	uint32_t ecc_corr_bg;
	uint32_t ecc_corr_ba;
	uint32_t ecc_corr_row;
	uint32_t ecc_corr_col;
	uint32_t ecc_corrected_err;
	uint32_t data = 0;

	struct ddrc_info *ddrc_info = (struct ddrc_info *)dev_id;

	ddr_sys_index = ddr_num >> 1;
	ctl_index = ddr_num & 0x1;

	ecc_ap_err = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10664)) & 0x1;

	if (ecc_ap_err) {
		pr_err("---- ecc_ap_err ----\n");
		ecc_ap_err_threshold = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10600)
			& 0x1F000000) >> 24;
		pr_err("ecc_ap_err_threshold = %d\r\n", ecc_ap_err_threshold);
		pr_err("the number of ECC errors (single/double) within one burst exceeded the threshold\r\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x, ", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// interrupt clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);
		data = ((data & (~0x10)) | shift_to_mask_bit(0x10, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);

		ecc_ap_err = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10664) & 0x1;
		if (!ecc_ap_err)
			pr_err("ecc_ap_err = %d, ecc_ap_err has been cleared\n", ecc_ap_err);
		else {
			pr_err("ecc_ap_err clear fail, please check\n");
			return;
		}

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");
	}

	// double-bit error
	ecc_uncorr_error = 0;
	ecc_uncorr_err_cnt = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10610)) &
			       0xFFFF0000) >> 16;

	if (ecc_uncorr_err_cnt) {
		pr_err("---- double-bit error ----\n");
		ecc_uncorr_error = 1;
		pr_err("error! sys%x chl%d inline_ECC has double-bit error, ecc_uncorr_err_cnt = %d\n",
				ddr_sys_index, ctl_index, ecc_uncorr_err_cnt);

		ecc_uncorr_syndrome_31_0 = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1063c);
		pr_err("error! sys%x chl%d inline_ECC has double-bit error, ecc_uncorr_syndrome_31_0 = 0x%x\n",
				ddr_sys_index, ctl_index, ecc_uncorr_syndrome_31_0);
		ecc_uncorr_syndrome_63_32 = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10640);
		pr_err("error! sys%x chl%d inline_ECC has double-bit error, ecc_uncorr_syndrome_63_32 = 0x%x\n",
				ddr_sys_index, ctl_index, ecc_uncorr_syndrome_63_32);
		ecc_uncorr_syndrome_71_64 = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10644);
		pr_err("error! sys%x chl%d inline_ECC has double-bit error, ecc_uncorr_syndrome_71_64 = 0x%x\n",
				ddr_sys_index, ctl_index, ecc_uncorr_syndrome_71_64);

		// address
		pr_err("---- address ----\n");
		ecc_uncorr_rank = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10634)) &
				0xFF000000) >> 24;
		ecc_uncorr_bg = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10638)) &
				0x0F000000) >> 24;
		ecc_uncorr_ba = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10638)) &
				0x00FF0000) >> 16;
		ecc_uncorr_row = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10634)) &
				0x00FFFFFF) >> 0;
		ecc_uncorr_col = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10638)) &
				0x000007FF) >> 0;
		ddr_addr = (ecc_uncorr_row << 14) | (ecc_uncorr_bg << 12) | (ecc_uncorr_ba << 10) | ecc_uncorr_col;

		pr_err("ecc_uncorr_rank = %d\n", ecc_uncorr_rank);
		pr_err("ecc_uncorr_bg = %d\n", ecc_uncorr_bg);
		pr_err("ecc_uncorr_ba = %d\n", ecc_uncorr_ba);
		pr_err("ecc_uncorr_row = 0x%x\n", ecc_uncorr_row);
		pr_err("ecc_uncorr_col = 0x%x\n", ecc_uncorr_col);
		pr_err("inline_ecc_uncorr_ddr_addr = 0x%llx\n", ddr_addr);

		// interrupt
		ecc_uncorrected_err = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10608)
				       & 0xFF0000) >> 16;
		if (ecc_uncorrected_err)
			pr_err("ecc_uncorrected_err_intr has been set to 1\n");
		else
			pr_err("ecc_uncorrected_err_intr should not be 0, please check\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// interrupt clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);
		data = ((data & (~0x2)) | shift_to_mask_bit(0x2, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);

		ecc_uncorrected_err = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10608)
				       & 0xFF0000) >> 16;
		if (!ecc_uncorrected_err)
			pr_err("ecc_uncorrected_err_intr has been cleared\n");
		else
			pr_err("ecc_uncorrected_err_intr clear fail, please check\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// count clear
		// interrupt clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);
		data = ((data & (~0x8)) | shift_to_mask_bit(0x8, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);

		ecc_uncorr_err_cnt = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10610)
				      & 0xFFFF0000) >> 16;
		if (!ecc_uncorr_err_cnt)
			pr_err("ecc_uncorr_err_cnt has been cleared\n");
		else
			pr_err("ecc_uncorr_err_cnt clear fail, please check\n");
	}

	// single-bit error
	ecc_corr_error = 0;
	ecc_corr_err_cnt = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10610)) &
			     0x0000FFFF) >> 0;

	if (ecc_corr_err_cnt) {
		pr_err("---- single-bit error ----\n");
		ecc_corr_error = 1;
		pr_err("error! sys%x chl%d inline_ECC has single-bit error, ecc_corr_err_cnt = %d\n",
				ddr_sys_index, ctl_index, ecc_corr_err_cnt);

		ecc_corr_syndrome_31_0 = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1061C)) &
					  0xFFFFFFFF) >> 0;
		pr_err("error! sys%x chl%d inline_ECC has single-bit error, ecc_corr_syndrome_31_0 = 0x%x\n",
				ddr_sys_index, ctl_index, ecc_corr_syndrome_31_0);
		ecc_corr_syndrome_63_32 = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10620)) &
					   0xFFFFFFFF) >> 0;
		pr_err("error! sys%x chl%d inline_ECC has single-bit error, ecc_corr_syndrome_63_32 = 0x%x\n",
				ddr_sys_index, ctl_index, ecc_corr_syndrome_63_32);
		ecc_corr_syndrome_71_64 = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10624)) &
					   0x000000FF) >> 0;
		pr_err("error! sys%x chl%d inline_ECC has single-bit error, ecc_corr_syndrome_71_64 = 0x%x\n",
				ddr_sys_index, ctl_index, ecc_corr_syndrome_71_64);

		ecc_corr_bit_mask_31_0 = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10628)) &
				0xFFFFFFFF) >> 0;
		pr_err("ecc_corr_bit_mask_31_0 = 0x%x\n", ecc_corr_bit_mask_31_0);
		ecc_corr_bit_mask_63_32 = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1062C)) &
				0xFFFFFFFF) >> 0;
		pr_err("ecc_corr_bit_mask_63_32 = 0x%x\n", ecc_corr_bit_mask_63_32);
		ecc_corr_bit_mask_71_64 = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10630)) &
				0x000000FF) >> 0;
		pr_err("ecc_corr_bit_mask_71_64 = 0x%x\n", ecc_corr_bit_mask_71_64);
		pr_err("for ecc_corr_bit_mask, bit set to 1 means that the bit has been corrected by the ECC logic\n");

		ecc_corrected_bit_num = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10608)) &
				0x0000007F) >> 0;
		pr_err("ecc_corrected_bit_num = %d, it is the number of data bits corrected by single-bit ECC error\n",
			  ecc_corrected_bit_num);

		// address
		pr_err("---- address ----\n");
		ecc_corr_rank = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10614)) &
				0xFF000000) >> 24;
		ecc_corr_bg = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10618)) &
			    0x0F000000) >> 24;
		ecc_corr_ba = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10618)) &
			    0x00FF0000) >> 16;
		ecc_corr_row = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10614)) &
				0x00FFFFFF) >> 0;
		ecc_corr_col = ((readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10618)) &
				0x000007FF) >> 0;
		ddr_addr = (ecc_corr_row << 14) | (ecc_corr_bg << 12) | (ecc_corr_ba << 10) | ecc_corr_col;

		pr_err("ecc_corr_rank = %d\n", ecc_corr_rank);
		pr_err("ecc_corr_bg = %d\n", ecc_corr_bg);
		pr_err("ecc_corr_ba = %d\n", ecc_corr_ba);
		pr_err("ecc_corr_row = 0x%x\n", ecc_corr_row);
		pr_err("ecc_corr_col = 0x%x\n", ecc_corr_col);
		pr_err("inline_ecc_corr_ddr_addr = 0x%llx\n", ddr_addr);

		// interrupt
		ecc_corrected_err  = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10608)
				     & 0xFF00) >> 8;
		if (ecc_corrected_err)
			pr_err("ecc_corrected_err_intr has been set to 1\n");
		else
			pr_err("ecc_corrected_err_intr should not be 0, please check\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// interrupt clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);
		data = ((data & (~0x1)) | shift_to_mask_bit(0x1, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);

		ecc_corrected_err  = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10608)
				     & 0xFF00) >> 8;
		if (!ecc_corrected_err)
			pr_err("ecc_corrected_err_intr has been cleared\n");
		else
			pr_err("ecc_corrected_err_intr clear fail, please check\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// count clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);
		data = ((data & (~0x4)) | shift_to_mask_bit(0x4, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x1060C);

		ecc_corr_err_cnt = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10610)
				      & 0xFFFF);
		if (!ecc_corr_err_cnt)
			pr_err("ecc_corr_err_cnt has been cleared\n");
		else
			pr_err("ecc_corr_err_cnt clear fail, please check\n");
	}
}

static uint16_t dwc_ddrctl_cinit_seq_mr_access(uint8_t ddr_sys_index, uint8_t ctl_index,
					       uint32_t mr_address, uint32_t rank, void *dev_id)
{
		int conter = 0;
		bool rslt;
		uint16_t mrr_data = 0;
		uint32_t data = 0;

		struct ddrc_info *ddrc_info = (struct ddrc_info *)dev_id;

		/** - clear mrr_done if we are trying to do a read */
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10080);
		data = ((data & (~0x1000000)) | shift_to_mask_bit(0x1000000, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10080);

		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10080);
		data = ((data & (~0x80000000)) | shift_to_mask_bit(0x80000000, 0x1));
		data = ((data & (~0x1000000)) | shift_to_mask_bit(0x1000000, 0x0));
		data = ((data & (~0xFF0)) | shift_to_mask_bit(0xFF0, rank));
		data = ((data & (~0x8)) | shift_to_mask_bit(0x8, 0));
		data = ((data & (~0x1)) | shift_to_mask_bit(0x1, 1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10080);

		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10084);
		data = ((data & (~0xFFFFFFFF)) | shift_to_mask_bit(0xFFFFFFFF, mr_address << 8));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10084);

		/** - hit the mrctrl0->mr_wr bit to start to access. */
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10080);
		data = ((data & (~0x80000000)) | shift_to_mask_bit(0x80000000, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10080);

		do {
			rslt = (readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10090)) & 0x1;
			// udelay(10);
			conter++;
			if (conter > 20) {
				pr_err(" time out! operation mode = 0 fail\n");
				break;
			}
		} while (rslt);

		if (((ddr_sys_index < 8) & (ddr_sys_index % 2)) || ((ddr_sys_index >= 8) & !(ddr_sys_index % 2)))
			mrr_data = (readl(ddrc_info->base +
				    (0x02000000 + ctl_index * 0x400000) + 0x10094) & 0xFF00) >> 8;
		else
			mrr_data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10094) & 0xFF;

	return mrr_data;
}

static void lpddr5x_data_ecc_syndrome_to_beat_and_bit(uint16_t syndrome)
{
	uint8_t beat_num;
	uint8_t bit_num;
	uint8_t set_num = 0;

	if ((syndrome & 0x100) && !(syndrome >> 9)) {
		beat_num = (syndrome & 0xF8) >> 3;
		for (int i = 0; i < link_ecc_beat_list_5x_num; i++) {
			if (beat_num == link_ecc_beat_list_5x[i]) {
				bit_num = syndrome & 0x7;
				pr_err("beat%d bit%d has single-bit DATA error\r\n", i, bit_num);
				return;
			}
		}

		for (int i = 0; i < 9; i++)
			set_num = set_num + ((syndrome >> i) & 0x1);
		if (set_num <= 2) {
			for (int i = 0; i < 9; i++) {
				if ((syndrome >> i) & 0x1) {
					pr_err("check_bit%d has single-bit DATA error\r\n", i);
					return;
				}
			}
		}
	}

	pr_err("data syndrome is error, please check\r\n");
}

static void lpddr5x_dmi_ecc_syndrome_to_beat_and_bit(uint16_t syndrome)
{
	uint8_t beat_num;
	uint8_t bit_num;
	uint8_t dmi_num;
	uint8_t set_num = 0;

	if ((syndrome & 0x20) && !(syndrome >> 6)) {
		beat_num = (syndrome & 0x1C) >> 2;
		if ((beat_num == 0x3) || (beat_num == 0x6) || (beat_num == 0x5) || (beat_num == 0x7)) {
			bit_num = syndrome & 0x3;
			if (beat_num == 0x3)
				dmi_num = 0 * 4 + bit_num;
			else if (beat_num == 0x6)
				dmi_num = 1 * 4 + bit_num;
			else if (beat_num == 0x5)
				dmi_num = 2 * 4 + bit_num;
			else if (beat_num == 0x7)
				dmi_num = 3 * 4 + bit_num;
			pr_err("dmi%d has single-bit DMI error\r\n", dmi_num);
		}

		for (int i = 0; i < 6; i++)
			set_num = set_num + ((syndrome >> i) & 0x1);
		if (set_num <= 2) {
			for (int i = 0; i < 6; i++) {
				if ((syndrome >> i) & 0x1) {
					pr_err("check_bit%d has single-bit DMI error\r\n", i);
					return;
				}
			}
		}
	}

	pr_err("dmi syndrome is error, please check\r\n");
}

static void lpddr5x_link_ecc_write_error_check_sub(int ddr_num, uint8_t rank, void *dev_id)
{
	uint32_t ddr_sys_index;
	uint32_t ctl_index;
	uint8_t data_mr43, data_mr44, data_mr45;
	uint16_t SBE_count;
	uint16_t DBE_flag;
	uint16_t data_ecc_syndrome;
	uint16_t dmi_ecc_syndrome;
	uint16_t error_byte_lane;

	struct ddrc_info *ddrc_info = (struct ddrc_info *)dev_id;

	ddr_sys_index = ddr_num >> 1;
	ctl_index = ddr_num & 0x1;

	data_mr43 = dwc_ddrctl_cinit_seq_mr_access(ddr_sys_index, ctl_index, 0x2B, rank + 1, ddrc_info);
	data_mr44 = dwc_ddrctl_cinit_seq_mr_access(ddr_sys_index, ctl_index, 0x2C, rank + 1, ddrc_info);
	data_mr45 = dwc_ddrctl_cinit_seq_mr_access(ddr_sys_index, ctl_index, 0x2D, rank + 1, ddrc_info);

	DBE_flag = (data_mr43 & 0x80) >> 7;
	SBE_count = data_mr43 & 0x3F;
	data_ecc_syndrome = ((data_mr45 & 0x80) << 1) | data_mr44;
	dmi_ecc_syndrome = data_mr45 & 0x3F;
	error_byte_lane = (data_mr45 & 0x40) >> 6;

	if (SBE_count) {
		pr_err("---- single-bit error ----\n");
		pr_err("error_byte_lane = %d, ", error_byte_lane);
		if (!error_byte_lane)
			pr_err("the most recent single bit error occurred on DQ[7:0] or DMI0\n");
		else
			pr_err("the most recent single bit error occurred on DQ[15:8] or DMI1\n");

		pr_err("error! sys%x chl%d rank%d link_ECC has write single-bit error, SBE_count = %d\n",
			  ddr_sys_index, ctl_index, rank, SBE_count);
		if (data_ecc_syndrome) {
			pr_err("sys%x chl%d rank%d lECC has write single-bit DATA error, data_ecc_syndrome = 0x%x\n",
				  ddr_sys_index, ctl_index, rank, data_ecc_syndrome);
			lpddr5x_data_ecc_syndrome_to_beat_and_bit(data_ecc_syndrome);
		}
		if (dmi_ecc_syndrome) {
			pr_err("sys%x chl%d rank%d lECC has write single-bit DMI error, dmi_ecc_syndrome = 0x%x\n",
				  ddr_sys_index, ctl_index, rank, dmi_ecc_syndrome);
			lpddr5x_dmi_ecc_syndrome_to_beat_and_bit(dmi_ecc_syndrome);
		}
	}

	if (DBE_flag) {
		pr_err("---- double-bit error ----\n");
		pr_err("error! sys%x chl%d rank%d link_ECC has write double-bit error\n",
				ddr_sys_index, ctl_index, rank);
	}
}

static void lpddr5x_link_ecc_read_error_check_sub(uint32_t ddr_num, uint8_t rank, uint8_t byte_index, void *dev_id)
{
	uint32_t ddr_sys_index;
	uint32_t ctl_index, top_intr;
	uint32_t data = 0;
	uint8_t byte_set;
	int sw_done_ack;
	int counter = 0;
	uint32_t rd_link_ecc_uncorr_cnt;
	uint32_t rd_link_ecc_corr_cnt;
	uint32_t rd_link_ecc_err_syndrome;
	bool rd_link_ecc_uncorr_error;
	bool rd_link_ecc_corr_error;
	uint8_t rd_link_ecc_uncorr_err_int;
	uint8_t rd_link_ecc_corr_err_int;
	uint8_t link_ecc_corr_rank;
	uint8_t link_ecc_corr_bg;
	uint8_t link_ecc_corr_ba;
	uint32_t link_ecc_corr_row;
	uint32_t link_ecc_corr_col;
	uint8_t link_ecc_uncorr_rank;
	uint8_t link_ecc_uncorr_bg;
	uint8_t link_ecc_uncorr_ba;
	uint32_t link_ecc_uncorr_row;
	uint32_t link_ecc_uncorr_col;
	uint64_t ddr_addr;

	struct ddrc_info *ddrc_info = (struct ddrc_info *)dev_id;

	ddr_sys_index = ddr_num >> 1;
	ctl_index = ddr_num & 0x1;

	if (((ddr_sys_index < 8) && (ddr_sys_index % 2)) || ((ddr_sys_index >= 8) && !(ddr_sys_index % 2)))
		byte_set = ~byte_index & 0x1;
	else
		byte_set = byte_index;

	data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10C80);
	data = ((data & (~0x1)) | shift_to_mask_bit(0x1, 0x0));
	writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10C80);

	data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10990);
	data = ((data & (~0x30)) | shift_to_mask_bit(0x30, rank));     // rank_sel
	data = ((data & (~0x7)) | shift_to_mask_bit(0x7, byte_set));   // byte_sel
	writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10990);

	data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10C80);
	data = ((data & (~0x1)) | shift_to_mask_bit(0x1, 0x1));
	writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10C80);

	counter = 0;
	while (1) {
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10C84);
		sw_done_ack = data & 0x1;
		if (sw_done_ack == 1)
			break;
		counter++;
		if (counter > 10) {
			pr_err("time out! sys%x chl%d rank%d byte%d link_ECC read error check done fail\n",
				  ddr_sys_index, ctl_index, rank, byte_index);
			return;
		}
	}

	rd_link_ecc_uncorr_error = 0;
	rd_link_ecc_corr_error = 0;

	// double-bit error
	rd_link_ecc_uncorr_cnt = (readl(ddrc_info->base +
				  (0x02000000 + ctl_index * 0x400000) + 0x10994) & 0xFF000000) >> 24;
	if (rd_link_ecc_uncorr_cnt) {
		pr_err("---- double-bit error ----\n");
		rd_link_ecc_uncorr_error = 1;
		pr_err("sys%x chl%d rank%d byte%d lECC has read double-bit error, rd_link_ecc_uncorr_cnt = %d\n",
				ddr_sys_index, ctl_index, rank, byte_index, rd_link_ecc_uncorr_cnt);

		// address
		pr_err("---- address ----\n");
		link_ecc_uncorr_rank = (readl(ddrc_info->base +
					(0x02000000 + ctl_index * 0x400000) + 0x109E8) & 0xFF000000) >> 24;
		link_ecc_uncorr_bg = (readl(ddrc_info->base +
				      (0x02000000 + ctl_index * 0x400000) + 0x109EC) & 0xFF000000) >> 24;
		link_ecc_uncorr_ba = (readl(ddrc_info->base +
				      (0x02000000 + ctl_index * 0x400000) + 0x109EC) & 0xFF0000) >> 16;
		link_ecc_uncorr_row = readl(ddrc_info->base +
					    (0x02000000 + ctl_index * 0x400000) + 0x109E8) & 0xFFFFFF;
		link_ecc_uncorr_col = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x109EC) & 0x7FF;
		ddr_addr = (link_ecc_uncorr_row << 14) | (link_ecc_uncorr_bg << 12) |
			   (link_ecc_uncorr_ba << 10) | link_ecc_uncorr_col;

		pr_err("link_ecc_uncorr_rank = %d\n", link_ecc_uncorr_rank);
		pr_err("link_ecc_uncorr_bg = %d\n", link_ecc_uncorr_bg);
		pr_err("link_ecc_uncorr_ba = %d\n", link_ecc_uncorr_ba);
		pr_err("link_ecc_uncorr_row = 0x%x\n", link_ecc_uncorr_row);
		pr_err("link_ecc_uncorr_col = 0x%x\n", link_ecc_uncorr_col);
		pr_err("link_ecc_uncorr_ddr_addr = 0x%llx\n", ddr_addr);

		// interrupt
		rd_link_ecc_uncorr_err_int = (readl(ddrc_info->base +
					      (0x02000000 + ctl_index * 0x400000) + 0x10998) & 0xF00) >> 8;
		if (rd_link_ecc_uncorr_err_int & 0x1)
			pr_err("rd_link_ecc_uncorr_err_int = %d, rank0 has read double-bit error\n",
					rd_link_ecc_uncorr_err_int);
		if (rd_link_ecc_uncorr_err_int & 0x2)
			pr_err("rd_link_ecc_uncorr_err_int = %d, rank1 has read double-bit error\n",
					rd_link_ecc_uncorr_err_int);
		if (!rd_link_ecc_uncorr_err_int)
			pr_err("rd_link_ecc_uncorr_err_int should not be 0, please check\r\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// interrupt clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);
		data = ((data & (~0x20)) | shift_to_mask_bit(0x20, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);

		rd_link_ecc_uncorr_err_int = (readl(ddrc_info->base +
					      (0x02000000 + ctl_index * 0x400000) + 0x10998) & 0xF00) >> 8;
		if (!rd_link_ecc_uncorr_err_int)
			pr_err("rd_linkecc_uncorr_err_intr has been cleared\n");
		else
			pr_err("rd_linkecc_uncorr_err_intr clear fail, please check\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// count clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);
		data = ((data & (~0x40)) | shift_to_mask_bit(0x40, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);

		rd_link_ecc_uncorr_cnt = (readl(ddrc_info->base +
					  (0x02000000 + ctl_index * 0x400000) + 0x10994) & 0xFF000000) >> 24;
		if (!rd_link_ecc_uncorr_cnt)
			pr_err("rd_link_ecc_uncorr_cnt has been cleared\n");
		else
			pr_err("rd_link_ecc_uncorr_cnt clear fail, please check\n");
	}

	// single-bit error
	rd_link_ecc_corr_cnt = (readl(ddrc_info->base +
				(0x02000000 + ctl_index * 0x400000) + 0x10994) & 0xFF0000) >> 16;
	if (rd_link_ecc_corr_cnt) {
		pr_err("---- single-bit error ----\n");
		rd_link_ecc_corr_error = 1;
		pr_err("sys%x chl%d rank%d byte%d lECC has read single-bit error, rd_link_ecc_corr_cnt = %d\n",
				ddr_sys_index, ctl_index, rank, byte_index, rd_link_ecc_corr_cnt);

		rd_link_ecc_err_syndrome = readl(ddrc_info->base +
						 (0x02000000 + ctl_index * 0x400000) + 0x10994) & 0x1FF;
		pr_err("sys%x chl%d rank%d byte%d lECC has read single-bit error, rd_link_ecc_err_syndrome = 0x%x\n",
				ddr_sys_index, ctl_index, rank, byte_index, rd_link_ecc_err_syndrome);
		lpddr5x_data_ecc_syndrome_to_beat_and_bit(rd_link_ecc_err_syndrome);

		link_ecc_corr_rank = (readl(ddrc_info->base +
					    (0x02000000 + ctl_index * 0x400000) + 0x109E0) & 0xFF000000) >> 24;
		link_ecc_corr_bg = (readl(ddrc_info->base +
					  (0x02000000 + ctl_index * 0x400000) + 0x109E4) & 0xFF000000) >> 24;
		link_ecc_corr_ba = (readl(ddrc_info->base +
					  (0x02000000 + ctl_index * 0x400000) + 0x109E4) & 0xFF0000) >> 16;
		link_ecc_corr_row = readl(ddrc_info->base +
					  (0x02000000 + ctl_index * 0x400000) + 0x109E0) & 0xFFFFFF;
		link_ecc_corr_col = readl(ddrc_info->base +
					  (0x02000000 + ctl_index * 0x400000) + 0x109E4) & 0x7FF;
		ddr_addr = (link_ecc_corr_row << 14) | (link_ecc_corr_bg << 12) |
			   (link_ecc_corr_ba << 10) | link_ecc_corr_col;

		pr_err("link_ecc_corr_rank = %d\n", link_ecc_corr_rank);
		pr_err("link_ecc_corr_bg = %d\n", link_ecc_corr_bg);
		pr_err("link_ecc_corr_ba = %d\n", link_ecc_corr_ba);
		pr_err("link_ecc_corr_row = 0x%x\n", link_ecc_corr_row);
		pr_err("link_ecc_corr_col = 0x%x\n", link_ecc_corr_col);
		pr_err("link_ecc_corr_ddr_addr = 0x%llx\n", ddr_addr);

		// interrupt
		rd_link_ecc_corr_err_int = readl(ddrc_info->base +
						 (0x02000000 + ctl_index * 0x400000) + 0x10998) & 0xF;
		if (rd_link_ecc_corr_err_int & 0x1)
			pr_err("rd_link_ecc_corr_err_int = %d, rank0 has read single-bit error\n",
					rd_link_ecc_corr_err_int);
		if (rd_link_ecc_corr_err_int & 0x2)
			pr_err("rd_link_ecc_corr_err_int = %d, rank1 has read single-bit error\n",
					rd_link_ecc_corr_err_int);
		if (!rd_link_ecc_corr_err_int)
			pr_err("rd_link_ecc_corr_err_int should not be 0, please check\r\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// interrupt clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);
		data = ((data & (~0x2)) | shift_to_mask_bit(0x2, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);

		rd_link_ecc_corr_err_int = readl(ddrc_info->base +
						 (0x02000000 + ctl_index * 0x400000) + 0x10998) & 0xF;
		if (!rd_link_ecc_corr_err_int)
			pr_err("rd_linkecc_corr_err_intr has been cleared\n");
		else
			pr_err("rd_linkecc_corr_err_intr clear fail, please check\n");

		top_intr = readl(ddrc_info->base + 0x02800000 + 0xC);
		pr_err("top_intr = 0x%x", top_intr);
		intr_type(top_intr);
		pr_err("\n");

		// count clear
		data = readl(ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);
		data = ((data & (~0x4)) | shift_to_mask_bit(0x4, 0x1));
		writel(data, ddrc_info->base + (0x02000000 + ctl_index * 0x400000) + 0x10984);

		rd_link_ecc_corr_cnt = (readl(ddrc_info->base +
					(0x02000000 + ctl_index * 0x400000) + 0x10994) & 0xFF0000) >> 16;
		if (!rd_link_ecc_corr_cnt)
			pr_err("rd_link_ecc_corr_cnt has been cleared\n");
		else
			pr_err("rd_link_ecc_corr_cnt clear fail, please check\n");
	}
}

static irqreturn_t ddrc_interrupt(int irq, void *dev_id)
{
	struct ddrc_info *ddrc_info = (struct ddrc_info *)dev_id;

	pr_err("%s trigger interrupt\n", ddrc_info->name);

	for (int ctl_index = 0; ctl_index < 2; ctl_index++)
		lpddr5x_inline_ecc_error_check_sub((ddrc_info->ddrc_id) * 2 + ctl_index, ddrc_info);

	for (int ctl_index = 0; ctl_index < 2; ctl_index++) {
		for (int rank = 0; rank < 2; rank++) {
			lpddr5x_link_ecc_write_error_check_sub((ddrc_info->ddrc_id) * 2 + ctl_index, rank, ddrc_info);

			for (int byte_index = 0; byte_index < 2; byte_index++)
				lpddr5x_link_ecc_read_error_check_sub((ddrc_info->ddrc_id) * 2 + ctl_index,
								      rank, byte_index, ddrc_info);

		}
	}

	return IRQ_HANDLED;
}

static DEVICE_ATTR_RW(ddrc_trigger_error);
static int sophgo_ddrc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev_of_node(dev);
	struct resource *regs;
	int ret;
	struct ddrc_info *ddrc_info;

	ddrc_info = devm_kzalloc(dev, sizeof(*ddrc_info), GFP_KERNEL);
	if (!ddrc_info)
		return -ENOMEM;

	dev_set_drvdata(dev, ddrc_info);

	ret = device_create_file(dev, &dev_attr_ddrc_trigger_error);
	pr_info("[sophgo ddrc]: create ddrc success\n");

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return pr_err("no registers defined\n");

	ddrc_info->base = devm_ioremap(dev, regs->start, resource_size(regs));
	if (!ddrc_info->base) {
		pr_err("ioremap failed\n");
		goto failed;
	}

	ret = of_property_read_u32(dev_node, "ddrc_id", &ddrc_info->ddrc_id);

	ddrc_info->irq = platform_get_irq(pdev, 0);
	if (ddrc_info->irq < 0) {
		pr_err("failed to get irq\n");
		goto failed;
	}

	sprintf(ddrc_info->name, "ddrc%d", ddrc_info->ddrc_id);
	ret = request_irq(ddrc_info->irq, ddrc_interrupt, IRQF_TRIGGER_HIGH,  //IRQF_TRIGGER_HIGH
			  ddrc_info->name, ddrc_info);
	if (ret < 0) {
		pr_err("%s failed request irq%d\n", ddrc_info->name, ddrc_info->irq);
		goto failed;
	}

	return 0;
failed:
	return -1;
}

static void sophgo_ddrc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ddrc_info *ddrc_info = dev_get_drvdata(dev);

	pr_info("[sophgo]: %s remove ddrc success\n", ddrc_info->name);
}

static const struct of_device_id c2c_enable_of_match[] = {
	{ .compatible = "sophgo,ddrc",},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, c2c_enable_of_match);

static struct platform_driver sophgo_ddrc_driver = {
	.driver = {
		.name		= "sophgo-ddrc",
		.of_match_table	= c2c_enable_of_match,
	},
	.probe			= sophgo_ddrc_probe,
	.remove			= sophgo_ddrc_remove,
};

module_platform_driver(sophgo_ddrc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("driver for ddrc error check");
