#ifndef EXYDE_SPINLOCK_H
#define EXYDE_SPINLOCK_H

#include <exyde/types.h>
#include <exyde/arch.h>

/* Kernel spinlock.  On the current single-CPU configuration this
 * degenerates to "disable IRQs" — the atomic flag is only a guard
 * against programmer error.  The structure is already shaped for
 * multi-CPU: on SMP it will gain a proper pause-loop with IF enabled.
 *
 * NOT reentrant.  Do not hold across a blocking call. */
typedef struct {
    volatile int locked;
    u64          saved_flags;
} spinlock_t;

#define SPINLOCK_INIT { 0, 0 }

static inline void spin_lock(spinlock_t *l) {
    u64 flags = arch_irqs_save_and_disable();
    while (__atomic_exchange_n(&l->locked, 1, __ATOMIC_ACQUIRE)) {
        /* On SMP: cpu_relax(); here this loop is unreachable because
         * IRQs are off and no other context can hold the lock. */
    }
    l->saved_flags = flags;
}

static inline void spin_unlock(spinlock_t *l) {
    __atomic_store_n(&l->locked, 0, __ATOMIC_RELEASE);
    arch_irqs_restore(l->saved_flags);
}

#endif /* EXYDE_SPINLOCK_H */
