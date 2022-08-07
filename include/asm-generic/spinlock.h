/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_GENERIC_SPINLOCK_H
#define __ASM_GENERIC_SPINLOCK_H

#include <asm-generic/ticket_spinlock.h>
#include <asm/qrwlock.h>

/* See include/linux/spinlock.h */
#ifndef smp_mb__after_spinlock
#define smp_mb__after_spinlock()	smp_mb()
#endif

#endif /* __ASM_GENERIC_SPINLOCK_H */
