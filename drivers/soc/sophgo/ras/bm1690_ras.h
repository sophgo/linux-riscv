/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __BM1690_RAS_H__
#define __BM1690_RAS_H__

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
#include <linux/of_address.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>

struct ap_status {
	uint64_t user_space_kick_time;
	uint64_t kernel_space_kick_time;
};

struct tp_status {
	uint64_t kernel_exec_time;
	uint64_t tp_alive_time;
	uint64_t tp_read_loop_time;
};

struct sophgo_card_ras_status {
	uint32_t tpu_index;
	uint32_t tpu_num;
	uint64_t status_pa;
	void *status_va;
	uint64_t last_exec_time;
	uint64_t last_alive_time;
	struct delayed_work ras_delayed_work;
};

#endif
