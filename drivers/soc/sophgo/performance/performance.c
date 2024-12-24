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

#define IO	(0xff)
#define DDR_SIZE	(4096)
#define SRAM_SIZE	(4096)

struct sg_memory_performance performance_test [] = {
	[0] = {
		.start_addr = 0x7010000000,
                .size = SRAM_SIZE,
                .mem_type = MEMREMAP_WB,
                .test_type = "sram memory WB(cache):",
	},
	[1] = {
		.start_addr = 0x7010001000,
                .size = SRAM_SIZE,
                .mem_type = MEMREMAP_WT,
                .test_type = "sram memory WT(no-cache):",
	},
	[2] = {
		.start_addr = 0x7010002000,
                .size = SRAM_SIZE,
                .mem_type = IO,
                .test_type = "sram memory (IO):",
	},
	[3] = {
		.start_addr = 0x1c0000000,
                .size = DDR_SIZE,
                .mem_type = MEMREMAP_WB,
                .test_type = "ddr memory WB(cache):",
	},
	[4] = {
		.start_addr = 0x1c0001000,
                .size = DDR_SIZE,
                .mem_type = MEMREMAP_WT,
                .test_type = "ddr memory WT(no-cache):",
	},
	[5] = {
		.start_addr = 0x1c0002000,
                .size = DDR_SIZE,
                .mem_type = IO,
                .test_type = "ddr memory (IO):",
	},
};

static int sgcard_probe(struct platform_device *pdev)
{
	void *mem_base;
	uint64_t mem_size = 0x1000;
	struct timespec64 ts;
	uint64_t start_time;
	uint64_t end_time;
	uint64_t aarch_timer_start;
	uint64_t aarch_timer_end;

	for (int i = 0; i < 6; i++) {
		if (performance_test[i].mem_type == IO) {
			mem_base = ioremap(performance_test[i].start_addr, performance_test[i].size);
		} else {
			mem_base = memremap(performance_test[i].start_addr, performance_test[i].size, performance_test[i].mem_type);
		}

		ktime_get_real_ts64(&ts);
		start_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		aarch_timer_start = read_arch_timer();
		memset(mem_base, 0x5a, performance_test[i].size);
		if (performance_test[i].mem_type == MEMREMAP_WB) {
			arch_wb_cache_pmem(mem_base, performance_test[i].size);
			sync_is();
		}
		ktime_get_real_ts64(&ts);
		end_time = (uint64_t)(ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
		aarch_timer_end = read_arch_timer();

		pr_err("%s %llu bytes, %lluns, aarch:%llu\n", performance_test[i].test_type, performance_test[i].size,
			end_time - start_time, aarch_timer_end - aarch_timer_start);
	}

	return 0;
}

static int sgcard_remove(struct platform_device *pdev)
{
        return 0;
}

static struct of_device_id sophgo_card_of_match[] = {
	{ .compatible = "sophgo,sophgo-memory-performance",},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_card_of_match);

static struct platform_driver sg_card_platform_driver = {
	.driver = {
		.name		= "sophgo,sophgo-memory-performance",
		.of_match_table	= sophgo_card_of_match,
	},
	.probe			= sgcard_probe,
	.remove			= sgcard_remove,
};

static struct platform_driver *const drivers[] = {
	&sg_card_platform_driver,
};

module_platform_driver(sg_card_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tingzhu.wang");
MODULE_DESCRIPTION("performance driver for sg runtime");

