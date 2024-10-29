/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOPHGO_COMMON_H_
#define _SOPHGO_COMMON_H_

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/types.h>

//#include "sophgo_pt.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 dma_addr_t;

#define DESC_NUM	256
#define CDM_ID_MAX	0xffffff
#define CRC_LEN		4
#define CMD_ID_START	1
#define SKB_LEN		(ETH_MTU + 0xd)

#define HOST_READY_FLAG 0x01234567
#define DEVICE_READY_FLAG 0x89abcde

#define PT_ALIGN 8
#define VETH_DEFAULT_MTU 1500

#define SHM_HOST_PHY_OFFSET 0x00
#define SHM_HOST_LEN_OFFSET 0x08
#define SHM_HOST_HEAD_OFFSET 0x10
#define SHM_HOST_TAIL_OFFSET 0x14
#define SHM_HANDSHAKE_OFFSET 0x40

#define TOP_MISC_GP_REG14_STS_OFFSET 0x0b8
#define TOP_MISC_GP_REG14_SET_OFFSET 0x190
#define TOP_MISC_GP_REG14_CLR_OFFSET 0x194

#define TOP_MISC_GP_REG15_STS_OFFSET 0x0bc
#define TOP_MISC_GP_REG15_SET_OFFSET 0x198
#define TOP_MISC_GP_REG15_CLR_OFFSET 0x19c

struct dma_tcp_send {
	union {
		struct {
			unsigned int intr_en : 1; // Interrupt Enable
			unsigned int own : 1; // Own bit
			unsigned int FD : 1; // First descriptor
			unsigned int LD : 1; // Last descriptor
			unsigned int cmd_type : 4; // Command type
			unsigned int buffer_length : 16; // Buffer length
			unsigned int breakpoint : 1; // Breakpoint enable
			unsigned int reserved1 : 7; // Reserved
			unsigned int frame_length : 16; // Frame length
			unsigned int reserved2 : 16; // Reserved
			unsigned int buffer_addr_l32 : 32; // Buffer address low 32 bits
			unsigned int buffer_addr_h8 : 8; // Buffer address high 8 bits
			unsigned int cmd_id : 24; // Command ID
		};
		struct {
			u64 desc0;
			u64 desc1;
		};
	};
};

struct dma_tcp_rcv {
	union {
		struct {
			unsigned int intr_en : 1; // Interrupt Enable - RW
			unsigned int own : 1; // Own bit - RW
			unsigned int reserved1 : 2; // Reserved - RO
			unsigned int cmd_type : 4; // Command type - RW
			unsigned int buffer_length : 16; // Buffer length - RW
			unsigned int breakpoint : 1; // Breakpoint enable - RW
			unsigned int reserved2 : 7; // Reserved - RO
			unsigned int reserved3 : 32; // Reserved - RO
			unsigned int buffer_addr_l32 : 32; // Buffer address low 32 bits - RW
			unsigned int buffer_addr_h8 : 8; // Buffer address high 8 bits - RW
			unsigned int cmd_id : 24; // Command ID - RW
		};
		struct {
			unsigned int reserved4 : 1;
			unsigned int own_1 : 1;
			unsigned int FD : 1;
			unsigned int LD : 1;
			unsigned int MAC_filter_status : 18;
			unsigned int reserved5 : 10;
			unsigned int packet_status : 9;
			unsigned int error_summary : 1;
			unsigned int packet_length : 14;
			unsigned int reserved6 : 8;
			unsigned int reserved7 : 32;
			unsigned int reserved8 : 32;
		};
		struct {
			u64 desc0;
			u64 desc1;
		};
	};
};

struct buffer_info {
	dma_addr_t paddr;
	struct sk_buff *skb;
};

struct rx_queue {
	u64 rx_next_to_use;
	u64 rx_next_to_clean;
	u32 cmd_id_cur;
	struct buffer_info *buffer_info;
};

struct tx_queue {
	u64 tx_need_to_clean;
	u64 tx_next_to_use;
	u64 tx_cur;
	struct sk_buff *tx_skb;
};

struct veth_dev {
	struct platform_device *pdev;

	void __iomem *cdma_reg;
	void __iomem *cdma_csr_reg;
	void __iomem *cxp_top_reg;

	struct resource *cdma_cfg_res;
	struct resource *cxp_top_cfg_res;

	int rx_irq;
	atomic_t link;
	struct pt *pt;
	/* lock for operate cdma */
	spinlock_t lock_cdma;
	struct dma_tcp_send *desc_tx;
	struct dma_tcp_rcv *desc_rx;
	dma_addr_t desc_tx_phy;
	dma_addr_t desc_rx_phy;
	struct tx_queue *tx_queue;
	struct rx_queue *rx_queue;

	struct work_struct net_state_worker;
	struct napi_struct napi;
	struct net_device *ndev;

};

#define	CDMA_RX_CHANNEL		0x0
#define	CDMA_TX_CHANNEL		0x7

//#define DEBUG

#ifdef DEBUG
#define debug_log(...)	pr_info(__VA_ARGS__)
#else
#define debug_log(...)	do {} while (0)
#endif

enum {
	CDMA_SEND = 0,
	CDMA_READ = 1,
	CDMA_WRITE = 2,
	CDMA_GENERAL = 3,
	CDMA_RECEIVE = 4,
	CDMA_LOSSY_COMPRESS = 5,
	CDMA_LOSSY_DECOMPRESS = 6,
	CDMA_SYS = 7,
	CDMA_TCP_SEND = 8,
	CDMA_TCP_RECEIVE = 9,
	CDMA_TYPE_NUM,
};

enum {
	DATA_INT8 = 0,
	DATA_FP16 = 1,
	DATA_FP32 = 2,
	DATA_INT16 = 3,
	DATA_INT32 = 4,
	DATA_BF16 = 5,
	DATA_FP20 = 6,
	DATA_FORMAT_NUM,
};

enum {
	CHAIN_END = 0,
	NOP = 1,
	SYS_TR_WR = 2,
	SYS_MSG_TX_SEND = 3,
	SYS_MSG_TX_WAIT = 4,
	SYS_MSG_RX_SEND = 5,
	SYS_MSG_RX_WAIT = 6,
	SYS_CMD_NUM,
};

enum {
	TCP_SEND = 1,
	TCP_RECEIVE = 2,
	NORMAL_GENERAL_DESC = 3,
	NORMAL_GENERAL_PIO = 4,
};

enum {
	FD = 1,
	LD = 2,
	MD = 3,
	FLD = 4,
};

#define GENERAL_DESC_PHY_OFFSET		0X10000000ULL
#define TCP_SEND_DESC_PHY_OFFSET	0X11000000ULL
#define TCP_RCV_DESC_PHY_OFFSET		0X12000000ULL
#define CDMA_PMU_START_ADDR		0x13000000ULL

#define CDMA_PMU_SIZE		        (1 * 1024 * 1024)

#define CMD_REG_OFFSET			0x0000
#define DESC_UPDT_OFFSET		0x0400
#define CSR_REG_OFFSET			0x1000

#define GENERAL_CMD_SIZE		0x20
#define TCP_SEND_CMD_SIZE		0X10
#define TCP_RCV_CMD_SIZE		0X10

// CMD_GENERAL_REG
#define CDMA_CMD_INTR_ENABLE		0
#define CDMA_CMD_BREAKPOINT		3
#define CDMA_CMD_CMD_TYPE		4
#define CDMA_CMD_SPECIAL_FUNCTION	8
#define CDMA_CMD_FILL_CONSTANT_EN	11
#define CDMA_CMD_SRC_DATA_FORMAT	12
#define CDMA_CMD_SRC_START_ADDR_H13	16
#define CDMA_CMD_DST_START_ADDR_H13	32
#define CDMA_CMD_CMD_LENGTH		0
#define CDMA_CMD_SRC_START_ADDR_L32	32
#define CDMA_CMD_DST_START_ADDR_L32	0

// CDMA_TCP_SEND_REG
#define CMD_TCP_SEND_INTR_ENABLE	0
#define CMD_TCP_SEND_OWN		1
#define CMD_TCP_SEND_FD			2
#define CMD_TCP_SEND_LD			3
#define CMD_TCP_SEND_CMD_TYPE		4
#define CMD_TCP_SEND_BUF_LEN		8
#define CMD_TCP_SEND_BREAKPOINT		24
#define CMD_TCP_SEND_FRAME_LEN		32
#define CMD_TCP_SEND_BUF_ADDR_L32	0
#define CMD_TCP_SEND_BUF_ADDR_H8	32
#define CMD_TCP_SEND_CMD_ID		40

//CMD_TCP_RCV_REG
#define CMD_TCP_RCV_INTR_ENABLE		0
#define CMD_TCP_RCV_OWN			1
#define CMD_TCP_RCV_CMD_TYPE		4
#define CMD_TCP_RCV_BUF_LEN		8
#define CMD_TCP_RCV_BREAKPOINT		24
#define CMD_TCP_RCV_BUF_ADDR_L32	0
#define CMD_TCP_RCV_BUF_ADDR_H8		32
#define CMD_TCP_RCV_CMD_ID		40

// DESCRIPTOR_UPDT
#define REG_DESCRIPTOR_UPDATE		0

// CSR_REG_BASE
#define	CDMA_CSR_0_OFFSET		0x0
#define	CDMA_CSR_1_OFFSET		0x4	//reg_rcv_addr_h32
#define CDMA_CSR_4_OFFSET		0x10
#define CDMA_CSR_5_OFFSET		0x14
#define CDMA_CSR_6_OFFSET		0x18
#define CDMA_CSR_9_OFFSET		0x2c
#define CDMA_CSR_10_OFFSET		0x30
#define CDMA_CSR_PMU_START_ADDR_L32	0x34
#define CDMA_CSR_PMU_START_ADDR_H1	0x38
#define CDMA_CSR_PMU_END_ADDR_L32	0x3c
#define CDMA_CSR_PMU_END_ADDR_H1	0x40
#define CDMA_CSR_19_SETTING		0x54	//intr mask
#define CDMA_CSR_20_SETTING		0x58	//intr
#define REG_BASE_ADDR_REGINE0		0x5c
#define CDMA_CSR_PMU_WR_ADDR_L32	0x110
#define CDMA_CSR_PMU_WR_ADDR_H1         0x114
#define CDMA_CSR_141_OFFSET		0x23c
#define CDMA_CSR_142_OFFSET		0x240
#define CDMA_CSR_143_SETTING		0x244
#define CDMA_CSR_144_SETTING		0x248
#define CDMA_CSR_148_SETTING		0x258
#define CDMA_CSR_149_SETTING		0x25c
#define CDMA_CSR_150_SETTING		0x260
#define CDMA_CSR_151_SETTING		0x264
#define TCP_CSR_02_SETTING		0x308
#define TCP_CSR_03_SETTING		0x30c
#define TCP_CSR_04_SETTING		0x310
#define TCP_CSR_05_SETTING		0x314
#define TCP_CSR_06_SETTING		0x318
#define TCP_CSR_07_SETTING		0x31c
#define TCP_CSR_08_SETTING		0x320
#define TCP_CSR_09_SETTING		0x324
#define TCP_CSR_10_SETTING		0x328
#define TCP_CSR_13_SETTING		0x334
#define TCP_CSR_14_SETTING		0x338
#define TCP_CSR_20_SETTING		0x350
#define MTL_FAB_CSR_00			0x3c0

// CXP_TOP
#define SOFT_RST_CTRL			0x90

// SOFT_RST_CTRL
#define CDMA0_WRAPPER_SOFT_RSTN		0

// CDMA_CSR_0
#define	REG_DES_MODE_ENABLE	0
#define	REG_SYNC_ID_RESET	1
#define REG_DES_CLR		2
#define REG_PERF_MONITOR_EN	3
#define REG_CONNECTION_ON	6
#define	REG_HW_REPLAY_EN	7
#define REG_ETH_CSR_UPDT	10

// CDMA_CSR_4
#define REG_REPLAY_MAX_TIME		19	 //bit 19~22
#define REG_SEC_LENGTH			16	//bit 16~18
#define REG_DES_MAX_ARLEN		23	//bit 23~25
#define REG_REPLAY_RECONTINUE		26

// CDMA_CSR_5
#define REG_TIMEOUT_MAXTIME		0	//bit 0~19

// CDMA_CSR_6
#define REG_CDMA_VLAN_ID		0	//bit 0~15
#define	REG_CDMA_VLAN_ID_VTIR		16	//bit 16~17
#define REG_CDMA_CPC			18	//bit 18~19
#define REG_CDMA_SAIC			20	//bit 20~21
#define REG_CDMA_CIC			22	//bit 22~23

// CDMA_CSR_9
#define REG_DES_ADDR_L32		0

// CDMA_CSR_10

#define REG_DES_ADDR_H1			0
// CDMA_CSR_20_SETTING
#define INTR_CMD_DONE_STATUS		0
#define INTR_TCP_RCV_CMD_DONE		28
#define INTR_TCP_SEND_CMD_DONE		29

// CDMA_CSR_141
#define  REG_INTRA_DIE_READ_ADDR_H8	0	//bit 0~7
#define  REG_INTRA_DIE_WRITE_ADDR_H8	8	//bit 8~15
// CDMA_CSR_142
#define REG_DES_READ_ADDR_H8		0	//bit 0~7
#define REG_DES_WRITE_ADDR_H8		8	//bit 8~15
// CDMA_CSR_143
#define REG_TXCH_READ_ADDR_H8		0
#define REG_TXCH_WRITE_ADDR_H8		8
// CDMA_CSR_144
#define REG_RXCH_READ_ADDR_H8		0
#define REG_RXCH_WRITE_ADDR_H8		8
// MTL_FAB_CSR_00
#define REG_CDMA_TX_CH_ID		0	//bit 0~3
#define REG_CDMA_RX_CH_ID		4	//bit 4~7
#define REG_CDMA_FAB_ARBITRATION	8	//bit 8~9
#define REG_CDMA_FAB_BYPASS		10

// TCP_CSR_02_SETTING
#define REG_TCP_SEND_DES_MODE_ENABLE	0
#define REG_TCP_RECEIVE_DES_MODE_ENABLE	1
//  TCP_CSR_20_SETTING
#define REG_TCP_CSR_UPDT		0

void sg_write32(void __iomem *base, u32 offset, u32 value);
u32 sg_read32(void __iomem *base, u32 offset);
void p2p_enable_desc(void __iomem *csr_reg_base, u8 mode);
int p2p_poll(void __iomem *csr_reg_base, u8 mode);

#endif
