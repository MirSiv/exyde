#ifndef EXYDE_MUTEX_H
#define EXYDE_MUTEX_H

#include <exyde/types.h>
#include <exyde/thread.h>
#include <exyde/waitq.h>

/* Blocking mutual-exclusion lock.  Not recursive. */
typedef struct {
    volatile int locked;
    thread_t    *owner;
    waitq_t      waiters;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
bool mutex_trylock(mutex_t *m);
void mutex_unlock(mutex_t *m);

/* Internal helpers used by condvar: assume IRQs are disabled and leave
 * them disabled.  Do not call from user code. */
void mutex_lock_irqoff(mutex_t *m);
void mutex_unlock_irqoff(mutex_t *m);

#endif /* EXYDE_MUTEX_H */
