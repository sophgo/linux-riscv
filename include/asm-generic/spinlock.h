/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_GENERIC_SPINLOCK_H
#define __ASM_GENERIC_SPINLOCK_H

#ifdef CONFIG_QUEUED_SPINLOCKS
#include <asm-generic/qspinlock.h>
#else
#include <asm-generic/ticket_spinlock.h>
#endif
#include <asm/qrwlock.h>

/* See include/linux/spinlock.h */
#ifndef smp_mb__after_spinlock
#define smp_mb__after_spinlock()	smp_mb()
#endif

#endif /* __ASM_GENERIC_SPINLOCK_H */
