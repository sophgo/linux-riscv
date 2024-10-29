/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOPHGO_P2P_H_
#define _SOPHGO_P2P_H_

#include <linux/kernel.h>
#include "sophgo_common.h"

struct veth_addr {
	u64 paddr;
	struct sk_buff *skb;
};

#define ETH_MTU 1600
#define QUENE_LEN_CPU 0x70000
#endif
