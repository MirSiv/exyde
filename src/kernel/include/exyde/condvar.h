#ifndef EXYDE_CONDVAR_H
#define EXYDE_CONDVAR_H

#include <exyde/types.h>
#include <exyde/mutex.h>
#include <exyde/waitq.h>

/* Condition variable.  Always used together with a mutex; the caller
 * checks the condition under the mutex before waiting. */
typedef struct {
    waitq_t waiters;
} condvar_t;

void cond_init(condvar_t *c);
void cond_wait(condvar_t *c, mutex_t *m);
void cond_signal(condvar_t *c);
void cond_broadcast(condvar_t *c);

#endif /* EXYDE_CONDVAR_H */
