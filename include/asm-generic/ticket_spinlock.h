/* SPDX-License-Identifier: GPL-2.0 */

/*
 * 'Generic' ticket-lock implementation.
 *
 * It relies on atomic_fetch_add() having well defined forward progress
 * guarantees under contention. If your architecture cannot provide this, stick
 * to a test-and-set lock.
 *
 * It also relies on atomic_fetch_add() being safe vs smp_store_release() on a
 * sub-word of the value. This is generally true for anything LL/SC although
 * you'd be hard pressed to find anything useful in architecture specifications
 * about this. If your architecture cannot do this you might be better off with
 * a test-and-set.
 *
 * It further assumes atomic_*_release() + atomic_*_acquire() is RCpc and hence
 * uses smp_mb__after_spinlock which is RCsc to create an RCsc hot path, See
 * include/linux/spinlock.h
 *
 * The implementation uses smp_cond_load_acquire() to spin, so if the
 * architecture has WFE like instructions to sleep instead of poll for word
 * modifications be sure to implement that (see ARM64 for example).
 *
 */

#ifndef __ASM_GENERIC_TICKET_SPINLOCK_H
#define __ASM_GENERIC_TICKET_SPINLOCK_H

#include <linux/atomic.h>
#include <asm-generic/spinlock_types.h>

static __always_inline void ticket_spin_lock(arch_spinlock_t *lock)
{
	u32 val = atomic_fetch_add_acquire(1<<16, &lock->val);
	u16 ticket = val >> 16;

	if (ticket == (u16)val)
		return;

	atomic_cond_read_acquire(&lock->val, ticket == (u16)VAL);
}

static __always_inline bool ticket_spin_trylock(arch_spinlock_t *lock)
{
	u32 old = atomic_read(&lock->val);

	if ((old >> 16) != (old & 0xffff))
		return false;

	return atomic_try_cmpxchg_acquire(&lock->val, &old, old + (1<<16));
}

static __always_inline void ticket_spin_unlock(arch_spinlock_t *lock)
{
	u16 *ptr = (u16 *)lock + IS_ENABLED(CONFIG_CPU_BIG_ENDIAN);
	u32 val = atomic_read(&lock->val);

	smp_store_release(ptr, (u16)val + 1);
}

static __always_inline int ticket_spin_value_unlocked(arch_spinlock_t lock)
{
	u32 val = lock.val.counter;

	return ((val >> 16) == (val & 0xffff));
}

static __always_inline int ticket_spin_is_locked(arch_spinlock_t *lock)
{
	arch_spinlock_t val = READ_ONCE(*lock);

	return !ticket_spin_value_unlocked(val);
}

static __always_inline int ticket_spin_is_contended(arch_spinlock_t *lock)
{
	u32 val = atomic_read(&lock->val);

	return (s16)((val >> 16) - (val & 0xffff)) > 1;
}

/*
 * Remapping spinlock architecture specific functions to the corresponding
 * ticket spinlock functions.
 */
#define arch_spin_is_locked(l)		ticket_spin_is_locked(l)
#define arch_spin_is_contended(l)	ticket_spin_is_contended(l)
#define arch_spin_value_unlocked(l)	ticket_spin_value_unlocked(l)
#define arch_spin_lock(l)		ticket_spin_lock(l)
#define arch_spin_trylock(l)		ticket_spin_trylock(l)
#define arch_spin_unlock(l)		ticket_spin_unlock(l)

#endif /* __ASM_GENERIC_TICKET_SPINLOCK_H */
