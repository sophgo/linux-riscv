/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#include <asm/kmap_size.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>

#define PKMAP_BASE       ((FIXADDR_START - PMD_SIZE) & (PMD_MASK))
#define LAST_PKMAP       (PMD_SIZE >> PAGE_SHIFT)
#define LAST_PKMAP_MASK  (LAST_PKMAP - 1)
#define PKMAP_NR(virt)   (((virt) - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)   (PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define flush_cache_kmaps() do {} while (0)

#define arch_kmap_local_post_map(vaddr, pteval)	local_flush_tlb_page(vaddr)
#define arch_kmap_local_post_unmap(vaddr)	local_flush_tlb_page(vaddr)

extern pte_t *pkmap_page_table;
#endif
