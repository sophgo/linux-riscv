/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOPHGO_VIRTUAL_ETH_H_
#define _SOPHGO_VIRTUAL_ETH_H_

// #include "sophgo_pt.h"

#define PT_ALIGN 8

#define SHM_HOST_PHY_OFFSET 0x00
#define SHM_HOST_LEN_OFFSET 0x08
#define SHM_HOST_HEAD_OFFSET 0x10
#define SHM_HOST_TAIL_OFFSET 0x14
/* reserved 8 bytes */
#define SHM_HOST_RX_OFFSET 0x00
#define SHM_HOST_TX_OFFSET 0x20

#define SHM_SOC_TX_OFFSET SHM_HOST_RX_OFFSET
#define SHM_SOC_RX_OFFSET SHM_HOST_TX_OFFSET

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 dma_addr_t;

struct veth_addr {
	u64 paddr;
	struct sk_buff *skb;
};

#define HOST_READY_FLAG 0x01234567
#define DEVICE_READY_FLAG 0x89abcde

#define PT_ALIGN 8
#define VETH_DEFAULT_MTU 65536

#define SHM_HOST_PHY_OFFSET 0x00
#define SHM_HOST_LEN_OFFSET 0x08
#define SHM_HOST_HEAD_OFFSET 0x10
#define SHM_HOST_TAIL_OFFSET 0x14
#define SHM_HANDSHAKE_OFFSET 0x40
#define VETH_IPADDRESS_REG 0x44

#define TOP_MISC_GP_REG14_STS_OFFSET 0x0b8
#define TOP_MISC_GP_REG14_SET_OFFSET 0x190
#define TOP_MISC_GP_REG14_CLR_OFFSET 0x194

#define TOP_MISC_GP_REG15_STS_OFFSET 0x0bc
#define TOP_MISC_GP_REG15_SET_OFFSET 0x198
#define TOP_MISC_GP_REG15_CLR_OFFSET 0x19c

struct veth_dev {
	struct platform_device *pdev;

	void __iomem *shm_mem;
	void __iomem *rx_mem;
	void __iomem *tx_mem;
	void __iomem *irq_mem;

	struct resource *shm_res;
	struct resource *rx_res;
	struct resource *tx_res;
	struct resource *irq_res;

	int rx_irq;
	atomic_t link;
	struct pt *pt;
	/* lock for operate cdma */
	spinlock_t lock_cdma;

	struct work_struct net_state_worker;
	struct napi_struct napi;
	struct net_device *ndev;
};

#define ETH_MTU 65536
#define QUENE_LEN_CPU 0x70000
#endif
