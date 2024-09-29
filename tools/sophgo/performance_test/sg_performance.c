// SPDX-License-Identifier: GPL-2.0

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
#include <linux/moduleparam.h>

#define TEST_SIZE	(256)
#define TEST_LOOP	(256)

void arch_wb_cache_pmem(void *addr, size_t size);
void arch_invalidate_pmem(void *addr, size_t size);

static inline void sync_is(void)
{
	asm volatile (".long 0x01b0000b");
}

struct sg_memory_performance{
	uint64_t start_addr;
	uint64_t size;
	int mem_type;
	char *test_type;
};

static unsigned long addr;
static char *type;
static char *operation;

module_param(addr, ulong, S_IRUGO);
module_param(type, charp, S_IRUGO);
module_param(operation, charp, S_IRUGO);

__attribute__((unused)) static uint64_t read_arch_timer(void)
{
#ifdef __aarch64__
	uint64_t cnt = 0;

	__asm__ __volatile__ ("isb\n\t"
				"mrs %0, cntvct_el0\n\t"
		: "=r" (cnt));

	return cnt * 20;
#endif

#ifdef __riscv_xlen
	unsigned long n;

	__asm__ __volatile__("rdtime %0" : "=r"(n));
	return n * 20;
#endif

#ifdef __x86_64__
	uint64_t cnt = 0;

	return cnt;
#endif

}

static int __init sg_performance_init(void)
{
	void *mem_base;
	struct timespec64 ts;
	uint64_t start_time;
	uint64_t end_time;
	uint64_t aarch_timer_start;
	uint64_t aarch_timer_end;
	struct sg_memory_performance performance_test;

	performance_test.start_addr = addr;

	if (!memcmp("cache", type, 5)) {
		performance_test.mem_type = MEMREMAP_WB;
		performance_test.test_type = "cache";
		performance_test.size = TEST_SIZE;
	}
	else if (!memcmp("no-cache", type, 8)) {
		performance_test.mem_type = MEMREMAP_WT;
		performance_test.test_type = "no-cache";
		performance_test.size = TEST_SIZE;
	}
	else {
		pr_err("input type error\n");
		return -1;
	}

	mem_base = memremap(performance_test.start_addr, performance_test.size * TEST_LOOP,
		   performance_test.mem_type);

	if (!memcmp("read", operation, 4)) {
		ktime_get_real_ts64(&ts);
		start_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		aarch_timer_start = read_arch_timer();

		for (int i = 0; i < TEST_LOOP; i++) {
			for (int j = 0; j < (TEST_SIZE / 8); j++)
				readq(mem_base + i * TEST_SIZE + j * 8);
			if (performance_test.mem_type == MEMREMAP_WB) {
				arch_invalidate_pmem(mem_base + i * TEST_SIZE, performance_test.size);
				sync_is();
			}
		}

		ktime_get_real_ts64(&ts);
		end_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		aarch_timer_end = read_arch_timer();
	} else if (!memcmp("write", operation, 5)) {
		ktime_get_real_ts64(&ts);
		start_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		aarch_timer_start = read_arch_timer();

		for (int i = 0; i < TEST_LOOP; i++) {
			for (int j = 0; j < (TEST_SIZE / 8); j++)
				writeq(0x5a, mem_base + i * TEST_SIZE + j * 8);
			if (performance_test.mem_type == MEMREMAP_WB) {
				arch_wb_cache_pmem(mem_base + i * TEST_SIZE, performance_test.size);
				arch_invalidate_pmem(mem_base + i * TEST_SIZE, performance_test.size);
				sync_is();
			}
		}

		ktime_get_real_ts64(&ts);
		end_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		aarch_timer_end = read_arch_timer();
	}
	else {
		pr_err("input operation error\n");
		return -1;
	}

	memunmap(mem_base);
	pr_info("%s %llu bytes, %lluns, aarch:%llu\n", performance_test.test_type, performance_test.size,
		(end_time - start_time) / TEST_LOOP, (aarch_timer_end - aarch_timer_start) / TEST_LOOP);

	return 0;

}

static void __exit sg_performance_exit(void)
{
	pr_info("sg performance test exit!\n");
}

module_init(sg_performance_init);
module_exit(sg_performance_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kai.ma");
MODULE_DESCRIPTION("ddr/axi sram R/W performance test driver");
