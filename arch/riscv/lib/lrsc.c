/* SPDX-License-Identifier: GPL-2.0-only */

#ifdef CONFIG_SOPHGO_SG2042_MULTI_SOCKETS_LRSC_DELAY
#include <linux/types.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <asm/lrsc.h>

unsigned short lrsc_addr[ADDR_NUM] ____cacheline_aligned = {0};
EXPORT_SYMBOL(lrsc_addr);
#endif
