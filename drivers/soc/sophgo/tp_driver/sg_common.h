/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SG_COMMON_
#define _SG_COMMON_
#include <linux/cdev.h>
#include "sg_io.h"
#ifdef PLAT_BM1686
#include "sg_clkrst.h"
#endif

#define DEV_NAME		"sg-tpu"
#define MAX_NAME_LEN		128
#define MAX_CARD_NUMBER		1
#define CORE_NUM		8

#ifdef DEBUG
#define DBG_MSG(fmt, args...) \
	pr_info("[sg]%s:" fmt, __func__, ##args)
#else
#define DBG_MSG(fmt, args...)
#endif

struct sg_dev {
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	dev_t devno;
	char dev_name[MAX_NAME_LEN];
	unsigned int id;
	unsigned int tpu_num;

	struct sg_core *core_info[CORE_NUM];
	struct sg_vaddr vaddr;
#ifdef PLAT_BM1686
	struct sg_clkrst clkrst;
#endif
};

/* TPU core info */
struct sg_core {
	void __iomem *tpu_sys_base_vaddr;
};

#endif
