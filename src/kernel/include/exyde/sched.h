#ifndef EXYDE_SCHED_H
#define EXYDE_SCHED_H

#include <exyde/types.h>
#include <exyde/thread.h>

void      sched_init(void);

thread_t *thread_create(thread_fn_t fn, void *arg, const char *name);
thread_t *thread_create_ex(thread_fn_t fn, void *arg, const char *name,
                           vmm_space_t space);

thread_t *thread_current(void);
void      sched_enqueue(thread_t *t);
void      sched_yield(void);
void      sched_tick(void);
void      sched_preempt_point(void);
void      thread_exit(void) __attribute__((noreturn));
size_t    sched_ready_count(void);

/* Block the current thread.  Caller MUST have IRQs disabled and MUST
 * have already arranged for another thread to call sched_unblock().
 * Returns with IRQs still disabled; the caller is expected to restore
 * IRQ state. */
void      sched_block(void);

/* Move a BLOCKED thread back to the ready queue.  Caller MUST have
 * IRQs disabled.  No-op if the thread is not BLOCKED. */
void      sched_unblock(thread_t *t);

#endif /* EXYDE_SCHED_H */
