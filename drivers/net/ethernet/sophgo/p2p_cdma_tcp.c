// SPDX-License-Identifier: GPL-2.0

#include "p2p_cdma_tcp.h"

static void p2p_tcp_csr_config(void __iomem *csr_reg_base, u8 mode)
{
	u32 reg_val = 0;

	/* enable replay and hardware replayfunction */
	reg_val = sg_read32(csr_reg_base, CDMA_CSR_0_OFFSET);
	reg_val &= ~((1 << REG_CONNECTION_ON) | (1 << REG_HW_REPLAY_EN));
	reg_val |= ((1 << REG_CONNECTION_ON) | (0 << REG_HW_REPLAY_EN));
	sg_write32(csr_reg_base, CDMA_CSR_0_OFFSET, reg_val);
	debug_log("[CDMA_CSR_0_OFFSET]:0x%x\n", reg_val);
	if (mode == TCP_SEND) {
		/* tcp_csr_updt */
		reg_val = sg_read32(csr_reg_base, TCP_CSR_20_SETTING);
		reg_val |= (1 << REG_TCP_CSR_UPDT);
		sg_write32(csr_reg_base, TCP_CSR_20_SETTING, reg_val);
		debug_log("[TCP_CSR_20_SETTING]:0x%x\n", reg_val);
	}

	/*config CDMA own tx/rx channel id and prio */

	/* prio */
	reg_val = sg_read32(csr_reg_base, MTL_FAB_CSR_00);
	reg_val &= ~(0xf << REG_CDMA_TX_CH_ID);
	reg_val |=  (CDMA_TX_CHANNEL << REG_CDMA_TX_CH_ID);
	reg_val &= ~(0xf << REG_CDMA_RX_CH_ID);
	reg_val |=  (CDMA_RX_CHANNEL << REG_CDMA_RX_CH_ID);
	reg_val &= ~(0x1 << REG_CDMA_FAB_BYPASS);
	reg_val &= ~(0x3 << REG_CDMA_FAB_ARBITRATION);
	sg_write32(csr_reg_base, MTL_FAB_CSR_00, reg_val);
	debug_log("[MTL_FAB_CSR_00]:0x%x\n", reg_val);

	/* WRITE cxp_rn  / READ cxp_rn*/
	reg_val = sg_read32(csr_reg_base, CDMA_CSR_141_OFFSET);
	reg_val &= ~(0xff << REG_INTRA_DIE_WRITE_ADDR_H8);
	reg_val |= (0x80 << REG_INTRA_DIE_WRITE_ADDR_H8);
	reg_val &= ~(0xff << REG_INTRA_DIE_READ_ADDR_H8);
	reg_val |= (0x80 << REG_INTRA_DIE_READ_ADDR_H8);
	sg_write32(csr_reg_base, CDMA_CSR_141_OFFSET, reg_val);
	debug_log("[CDMA_CSR_141_OFFSET]:0x%x\n", reg_val);
}

static void p2p_tcp_desc_csr_config(void __iomem *csr_reg_base,
					u64 tcp_des_addr,
					u64 tcp_des_last_addr,
					u8 mode)
{
	u32 reg_val = 0;
	u64 tcp_des_addr_l32;
	u64 tcp_des_addr_h4;
	u64 tcp_des_last_addr_l32;
	u64 tcp_des_last_addr_h4;

	tcp_des_addr_l32 = (tcp_des_addr >> 4) & 0xffffffff;
	tcp_des_addr_h4 = (tcp_des_addr >> 36) & 0xf;
	tcp_des_last_addr_l32 = (tcp_des_last_addr >> 4) & 0xffffffff;
	tcp_des_last_addr_h4 = (tcp_des_last_addr >> 36) & 0xf;
	debug_log("tcp_des_addr:0x%llx tcp_des_last_addr:0x%llx\n",
		  tcp_des_addr, tcp_des_last_addr);

	if (mode == TCP_SEND) {
		sg_write32(csr_reg_base, TCP_CSR_03_SETTING, tcp_des_addr_l32);
		sg_write32(csr_reg_base, TCP_CSR_04_SETTING, tcp_des_addr_h4);
		sg_write32(csr_reg_base, TCP_CSR_07_SETTING, tcp_des_last_addr_l32);
		sg_write32(csr_reg_base, TCP_CSR_08_SETTING, tcp_des_last_addr_h4);
		debug_log("tcp_des_addr_l32 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_03_SETTING));
		debug_log("tcp_des_addr_h4 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_04_SETTING));
		debug_log("tcp_des_last_addr_l32 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_07_SETTING));
		debug_log("tcp_des_last_addr_h4 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_08_SETTING));
		reg_val = sg_read32(csr_reg_base, CDMA_CSR_143_SETTING);
		reg_val &= ~(0xff << REG_TXCH_READ_ADDR_H8);
		reg_val |= (0x80 << REG_TXCH_READ_ADDR_H8);
		reg_val &= ~(0xff << REG_TXCH_WRITE_ADDR_H8);
		reg_val |= (0x80 << REG_TXCH_WRITE_ADDR_H8);
		sg_write32(csr_reg_base, CDMA_CSR_143_SETTING, reg_val);
		debug_log("[DESC MODE][CDMA_CSR_143_OFFSET]:get 0x%x\n",
			  sg_read32(csr_reg_base, CDMA_CSR_143_SETTING));
	} else if (mode == TCP_RECEIVE) {
		sg_write32(csr_reg_base, TCP_CSR_05_SETTING, tcp_des_addr_l32);
		sg_write32(csr_reg_base, TCP_CSR_06_SETTING, tcp_des_addr_h4);
		sg_write32(csr_reg_base, TCP_CSR_09_SETTING, tcp_des_last_addr_l32);
		sg_write32(csr_reg_base, TCP_CSR_10_SETTING, tcp_des_last_addr_h4);
		debug_log("tcp_des_addr_l32 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_05_SETTING));
		debug_log("tcp_des_addr_h4 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_06_SETTING));
		debug_log("tcp_des_last_addr_l32 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_09_SETTING));
		debug_log("tcp_des_last_addr_h4 :0x%x\n",
			  sg_read32(csr_reg_base, TCP_CSR_10_SETTING));
		reg_val = sg_read32(csr_reg_base, CDMA_CSR_144_SETTING);
		reg_val &= ~(0xff << REG_RXCH_READ_ADDR_H8);
		reg_val |= (0x80 << REG_RXCH_READ_ADDR_H8);
		reg_val &= ~(0xff << REG_RXCH_WRITE_ADDR_H8);
		reg_val |= (0x80 << REG_RXCH_WRITE_ADDR_H8);
		sg_write32(csr_reg_base, CDMA_CSR_144_SETTING, reg_val);
		debug_log("[DESC MODE][CDMA_CSR_144_OFFSET]:get 0x%x\n",
			  sg_read32(csr_reg_base, CDMA_CSR_144_SETTING));
	}
}

static int p2p_tcp_send_cmd_config(struct dma_tcp_send *dma_tcp_send,
				   u64 src_mem_start_addr,
				   u32 data_length,
				   u64 frame_length,
				   u32 cmd_id,
				   u8 desc_flag)
{
	u64 reg_val;
	//u32 mem_tag = 0;
	u8 fd_flag;
	u8 ld_flag;
	u64 src_addr_h8 =  (src_mem_start_addr >> 32) & 0xff;
	u64 src_addr_l32 = (src_mem_start_addr) & 0xFFFFFFFF;
	struct dma_tcp_send *cmd_reg_base = dma_tcp_send;

	debug_log("[TCP SEND MODE] src_mem_start_addr:0x%llx\n", src_mem_start_addr);
	debug_log("[TCP SEND MODE] src_addr_h8:0x%llx src_addr_l32 :0x%llx\n",
		  src_addr_h8, src_addr_l32);
	debug_log("[TCP SEND MODE] dma_tcp_send:0x%llx", (u64)cmd_reg_base);
	debug_log("[TCP SEND MODE] desc_flag:%d", desc_flag);
	debug_log("[TCP SEND MODE] data_length:%u", data_length);
	debug_log("[TCP SEND MODE] frame_length:%llu", frame_length);

	switch (desc_flag) {
	case FD:
		fd_flag = 1;
		ld_flag = 0;
		break;
	case LD:
		fd_flag = 0;
		ld_flag = 1;
		frame_length = 0;
		break;
	case MD:
		fd_flag = 0;
		ld_flag = 0;
		frame_length = 0;
		break;
	case FLD:
		fd_flag = 1;
		ld_flag = 1;
		break;
	default:
		pr_info("choose desc_flag error!");
		return -EINVAL;
	}

	reg_val = 0;
	reg_val = ((1ull << CDMA_CMD_INTR_ENABLE) |
		   (1ull << CMD_TCP_SEND_OWN) |
		   ((u64)fd_flag << CMD_TCP_SEND_FD) |
		   ((u64)ld_flag << CMD_TCP_SEND_LD) |
		   ((u64)CDMA_TCP_SEND << CMD_TCP_SEND_CMD_TYPE) |
		   ((u64)data_length << CMD_TCP_SEND_BUF_LEN) |
		   ((u64)frame_length << CMD_TCP_SEND_FRAME_LEN));
	cmd_reg_base->desc0 = cpu_to_le64(reg_val);
	debug_log("[TCP SEND MODE] reg_val:%llu", reg_val);

	reg_val = 0;
	reg_val = ((src_addr_l32 << CMD_TCP_SEND_BUF_ADDR_L32) |
		   (src_addr_h8 << CMD_TCP_SEND_BUF_ADDR_H8) |
		   ((u64)cmd_id << CMD_TCP_SEND_CMD_ID));
	cmd_reg_base->desc1 =  cpu_to_le64(reg_val);
	debug_log("[TCP SEND MODE] reg_val:%llu", reg_val);

	return 0;
}

void p2p_tcp_rcv_cmd_config(struct dma_tcp_rcv *dma_tcp_rcv,
			    u64 dst_mem_start_addr,
			    u32 data_length,
			    u32 cmd_id)
{
	u64 reg_val;
	u64 dst_addr_h8 =  (dst_mem_start_addr >> 32) & 0xff;
	u64 dst_addr_l32 = (dst_mem_start_addr) & 0xFFFFFFFF;
	struct dma_tcp_rcv *cmd_reg_base = dma_tcp_rcv;

	debug_log("[TCP RCV MODE] dst_mem_start_addr:0x%llx\n", dst_mem_start_addr);
	debug_log("[TCP RCV MODE] dst_addr_h8:0x%llx dst_addr_l32 :0x%llx data_length:%u\n",
		  dst_addr_h8, dst_addr_l32, data_length);
	debug_log("[TCP RCV MODE] data_length:%u\n", data_length);
	debug_log("dma_tcp_rcv:0x%llx", (u64)cmd_reg_base);

	reg_val = 0;
	reg_val = ((1ull << CMD_TCP_RCV_INTR_ENABLE) |
		   (1ull << CMD_TCP_RCV_OWN) |
		   ((u64)CDMA_TCP_RECEIVE << CMD_TCP_RCV_CMD_TYPE) |
		   ((u64)data_length << CMD_TCP_RCV_BUF_LEN));

	cmd_reg_base->desc0 = cpu_to_le64(reg_val);
	debug_log("[CDMA_CMD_0]:0x%llx\n", cmd_reg_base->desc0);

	reg_val = 0;
	reg_val = ((dst_addr_l32 << CMD_TCP_RCV_BUF_ADDR_L32) |
		   (dst_addr_h8 << CMD_TCP_RCV_BUF_ADDR_H8) |
		   ((u64)cmd_id << CMD_TCP_RCV_CMD_ID));

	cmd_reg_base->desc1 =  cpu_to_le64(reg_val);
	debug_log("[CDMA_CMD_1]:0x%llx\n", cmd_reg_base->desc1);
}

u32 testcase_p2p_tcp_send_mode(void __iomem *csr_reg_base,
			       struct dma_tcp_send *dma_tcp_send,
			       u64 desc_phy_saddr,
			       u64 src_mem_start_addr,
			       u32 data_length,
			       u32 frame_length,
			       u32 cmd_id)
{
	u8 desc_flag;
	u32 desc_num = frame_length / data_length;
	struct dma_tcp_send *desc_current_virtual_addr;
	u64 desc_phy_eaddr;

	debug_log("[TCP SEND MODE] csr_reg_base :0x%llx desc_phy_saddr:0x%llx\n ",
		  (u64)csr_reg_base, desc_phy_saddr);

	desc_current_virtual_addr = dma_tcp_send;

	for (int i = 0; i < desc_num; i++) {
		if (desc_num <= 1) {
			desc_flag = FLD;
		} else if (desc_num == 2) {
			if (i == 0)
				desc_flag = FD;
			else if (i == 1)
				desc_flag = LD;
		} else if (desc_num > 2) {
			if (i == 0)
				desc_flag = FD;
			else if (i == desc_num - 1)
				desc_flag = LD;
			else
				desc_flag = MD;
		}
		p2p_tcp_send_cmd_config(desc_current_virtual_addr, src_mem_start_addr, data_length,
					frame_length, cmd_id, desc_flag);
		desc_current_virtual_addr++;
		src_mem_start_addr += data_length;
		cmd_id++;
	}

	desc_phy_eaddr = desc_phy_saddr + desc_num * TCP_SEND_CMD_SIZE;
	p2p_tcp_desc_csr_config(csr_reg_base, desc_phy_saddr, desc_phy_eaddr, TCP_SEND);

	return cmd_id;
}

void p2p_send_hw_init(void __iomem *csr_reg_base, u64 desc_phy_saddr, u32 desc_num)
{
	//u64 desc_phy_eaddr;
	p2p_tcp_csr_config(csr_reg_base, TCP_SEND);
	// desc_phy_eaddr = desc_phy_saddr + desc_num * TCP_SEND_CMD_SIZE;
	// p2p_tcp_desc_csr_config(csr_reg_base, desc_phy_saddr, desc_phy_eaddr, TCP_SEND);
}

void p2p_rcv_hw_init(void __iomem *csr_reg_base, u64 desc_phy_saddr, u32 desc_num)
{
	u64 desc_phy_eaddr;

	p2p_tcp_csr_config(csr_reg_base, TCP_RECEIVE);
	desc_phy_eaddr = desc_phy_saddr + desc_num * TCP_RCV_CMD_SIZE;
	p2p_tcp_desc_csr_config(csr_reg_base, desc_phy_saddr, desc_phy_eaddr, TCP_RECEIVE);
}
