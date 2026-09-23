#ifndef EXYDE_SEMAPHORE_H
#define EXYDE_SEMAPHORE_H

#include <exyde/types.h>
#include <exyde/waitq.h>

/* Counting semaphore (Dijkstra).  wait/post are blocking. */
typedef struct {
    volatile int count;
    waitq_t      waiters;
} semaphore_t;

void sem_init(semaphore_t *s, int initial);
void sem_wait(semaphore_t *s);   /* P */
void sem_post(semaphore_t *s);   /* V */
bool sem_trywait(semaphore_t *s);

#endif /* EXYDE_SEMAPHORE_H */
