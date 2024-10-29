// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/io.h>
#include "sophgo_common.h"

void sg_write32(void __iomem *base, u32 offset, u32 value)
{
	iowrite32(value, (void __iomem *)(((unsigned long)base) + offset));
}

u32 sg_read32(void __iomem *base, u32 offset)
{
	return ioread32((void __iomem *)(((unsigned long)base) + offset));
}

void p2p_enable_desc(void __iomem *csr_reg_base, u8 mode)
{
	u32 reg_val;

	if (mode == TCP_SEND) {
		reg_val = sg_read32(csr_reg_base, TCP_CSR_02_SETTING);
		reg_val |= (1 << REG_TCP_SEND_DES_MODE_ENABLE);
		sg_write32(csr_reg_base, TCP_CSR_02_SETTING, reg_val);
		debug_log("enable tcp_send desc!\n");
	} else if (mode == TCP_RECEIVE) {
		reg_val = sg_read32(csr_reg_base, TCP_CSR_02_SETTING);
		reg_val |= (1 << REG_TCP_RECEIVE_DES_MODE_ENABLE);
		sg_write32(csr_reg_base, TCP_CSR_02_SETTING, reg_val);
		debug_log("enable tcp_rcv desc!\n");
	} else if (mode == NORMAL_GENERAL_DESC) {
		reg_val = sg_read32(csr_reg_base, CDMA_CSR_0_OFFSET);
		reg_val |= (1 << REG_DES_MODE_ENABLE);
		sg_write32(csr_reg_base, CDMA_CSR_0_OFFSET, reg_val);
		debug_log("enable normal desc\n");
	}
}

int p2p_poll(void __iomem *csr_reg_base, u8 mode)
{
	u32 reg_offset;
	u32 bit_offset;
	u32 count = 1500;
	char *mode_name;
	u8 poll_val;

	switch (mode) {
	case NORMAL_GENERAL_PIO:
		reg_offset = CDMA_CSR_20_SETTING;
		bit_offset = INTR_CMD_DONE_STATUS;
		mode_name = "NORMAL_GENERAL_PIO";
		poll_val = 1;
		break;
	case NORMAL_GENERAL_DESC:
		reg_offset = CDMA_CSR_0_OFFSET;
		bit_offset = REG_DES_MODE_ENABLE;
		mode_name = "NORMAL_GENERAL_DESC";
		poll_val = 0;
		break;
	case TCP_SEND:
		reg_offset = CDMA_CSR_20_SETTING;
		bit_offset = INTR_TCP_SEND_CMD_DONE;
		mode_name = "TCP_SEND";
		poll_val = 1;
		break;
	case TCP_RECEIVE:
		reg_offset = CDMA_CSR_20_SETTING;
		bit_offset = INTR_TCP_RCV_CMD_DONE;
		mode_name = "TCP_RECEIVE";
		poll_val = 1;
		break;
	default:
		break;
	}

	while (((sg_read32(csr_reg_base, reg_offset) >> bit_offset) & 0x1) != poll_val) {
		udelay(1);
		if (--count == 0) {
			debug_log("[%s]cdma polling wait timeout\n", mode_name);
			return -1;
		}
		if (count % 100 == 0) {
			debug_log("[%s]cdma transfer consumes %d us\n",
				  mode_name, 1500 - count);
		}
	}

	debug_log("[%s] CDMA polling complete\n", mode_name);

	return 0;
}
