/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_RISCV_LRSC_H
#define _ASM_RISCV_LRSC_H

#ifdef CONFIG_SOPHGO_SG2042_MULTI_SOCKETS_LRSC_DELAY
#include <linux/cache.h>
#include <asm/delay.h>

#define NEED_DELAY	 64

#define ADDR_NUM	128
extern unsigned short lrsc_addr[ADDR_NUM] ____cacheline_aligned;

// 64-bit integer hash by Thomas Wang
static __always_inline unsigned long hash_wang64(unsigned long key)
{
    key = (~key) + (key << 21);
    key =  key ^ (key >> 24);
    key =  (key + (key << 3)) + (key << 8);
    key =  key ^ (key >> 14);
    key =  (key + (key << 2)) + (key << 4);
    key =  key ^ (key >> 28);
    key =  key + (key << 31);
    return key;
}

static __always_inline void pre_lrsc(unsigned long addr)
{
	unsigned int idx = hash_wang64(addr) % ADDR_NUM;
	unsigned short count = lrsc_addr[idx]++;
	unsigned int nsecs;

	if (count >= NEED_DELAY) {
		nsecs = ((count % 5) + 1) * 100;

		ndelay(nsecs);
	}
}

static __always_inline void post_lrsc(unsigned long addr)
{
	unsigned int idx = hash_wang64(addr) % ADDR_NUM;
	unsigned short count = lrsc_addr[idx]--;

	if (count == 0)
		lrsc_addr[idx] = 0;
}
#else
static __always_inline void pre_lrsc(unsigned long addr) {}
static __always_inline void post_lrsc(unsigned long addr) {}
#endif

#endif
