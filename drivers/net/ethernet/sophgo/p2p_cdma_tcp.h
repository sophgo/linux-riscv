/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __P2P_CDMA_TCP_H__
#define __P2P_CDMA_TCP_H__

#include "sophgo_common.h"

u32 testcase_p2p_tcp_send_mode(void __iomem *csr_reg_base,
			       struct dma_tcp_send *dma_tcp_send,
			       u64 desc_phy_saddr,
			       u64 src_mem_start_addr,
			       u32 data_length,
			       u32 frame_length,
			       u32 cmd_id);

void p2p_rcv_hw_init(void __iomem *csr_reg_base, u64 desc_phy_saddr, u32 desc_num);
void p2p_tcp_rcv_cmd_config(struct dma_tcp_rcv *dma_tcp_rcv,
			    u64 dst_mem_start_addr,
			    u32 data_length,
			    u32 cmd_id);
void p2p_send_hw_init(void __iomem *csr_reg_base, u64 desc_phy_saddr, u32 desc_num);

#endif
