/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#include <asm/kmap_size.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>

#define flush_cache_kmaps() do {} while (0)

extern pte_t *pkmap_page_table;
#endif
