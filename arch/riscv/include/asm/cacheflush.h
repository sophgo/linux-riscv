/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_CACHEFLUSH_H
#define _ASM_RISCV_CACHEFLUSH_H

#include <linux/mm.h>

static inline void local_flush_icache_all(void)
{
	asm volatile ("fence.i" ::: "memory");
}

#define PG_dcache_clean PG_arch_1

static inline void flush_dcache_page(struct page *page)
{
	/*
	 * HugeTLB pages are always fully mapped and only head page will be
	 * set PG_dcache_clean (see comments in flush_icache_pte()).
	 */
	if (PageHuge(page))
		page = compound_head(page);

	if (test_bit(PG_dcache_clean, &page->flags))
		clear_bit(PG_dcache_clean, &page->flags);
}
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1

/*
 * RISC-V doesn't have an instruction to flush parts of the instruction cache,
 * so instead we just flush the whole thing.
 */
#define flush_icache_range(start, end) flush_icache_all()
#define flush_icache_user_page(vma, pg, addr, len) \
	flush_icache_mm(vma->vm_mm, 0)

#ifdef CONFIG_64BIT
extern u64 new_vmalloc[NR_CPUS / sizeof(u64) + 1];
extern char _end[];
#define flush_cache_vmap flush_cache_vmap
static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
	if (is_vmalloc_or_module_addr((void *)start)) {
		int i;

		/*
		 * We don't care if concurrently a cpu resets this value since
		 * the only place this can happen is in handle_exception() where
		 * an sfence.vma is emitted.
		 */
		for (i = 0; i < ARRAY_SIZE(new_vmalloc); ++i)
			new_vmalloc[i] = -1ULL;
	}
}
#endif

#ifndef CONFIG_SMP

#define flush_icache_all() local_flush_icache_all()
#define flush_icache_mm(mm, local) flush_icache_all()

#else /* CONFIG_SMP */

void flush_icache_all(void);
void flush_icache_mm(struct mm_struct *mm, bool local);

#endif /* CONFIG_SMP */

extern unsigned int riscv_cbom_block_size;
void riscv_init_cbom_blocksize(void);

#ifdef CONFIG_RISCV_DMA_NONCOHERENT
void riscv_noncoherent_supported(void);
#else
static inline void riscv_noncoherent_supported(void) {}
#endif

/*
 * Bits in sys_riscv_flush_icache()'s flags argument.
 */
#define SYS_RISCV_FLUSH_ICACHE_LOCAL 1UL
#define SYS_RISCV_FLUSH_ICACHE_ALL   (SYS_RISCV_FLUSH_ICACHE_LOCAL)

#include <asm-generic/cacheflush.h>

#endif /* _ASM_RISCV_CACHEFLUSH_H */
