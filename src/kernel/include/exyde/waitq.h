#ifndef EXYDE_WAITQ_H
#define EXYDE_WAITQ_H

#include <exyde/thread.h>

/* FIFO wait queue used by blocking primitives.  All operations must be
 * called with IRQs disabled. */
typedef struct {
    thread_t *head;
    thread_t *tail;
} waitq_t;

static inline void waitq_init(waitq_t *q) {
    q->head = q->tail = (thread_t *)0;
}

static inline bool waitq_empty(const waitq_t *q) {
    return q->head == (thread_t *)0;
}

static inline void waitq_push(waitq_t *q, thread_t *t) {
    t->wait_next = (thread_t *)0;
    if (q->tail) q->tail->wait_next = t;
    else         q->head = t;
    q->tail = t;
}

static inline thread_t *waitq_pop(waitq_t *q) {
    thread_t *t = q->head;
    if (!t) return (thread_t *)0;
    q->head = t->wait_next;
    if (!q->head) q->tail = (thread_t *)0;
    t->wait_next = (thread_t *)0;
    return t;
}

#endif /* EXYDE_WAITQ_H */
