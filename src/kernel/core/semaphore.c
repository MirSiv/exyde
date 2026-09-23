#include <exyde/semaphore.h>
#include <exyde/sched.h>
#include <exyde/arch.h>

void sem_init(semaphore_t *s, int initial) {
    s->count = initial;
    waitq_init(&s->waiters);
}

void sem_wait(semaphore_t *s) {
    arch_irqs_disable();
    while (s->count <= 0) {
        waitq_push(&s->waiters, thread_current());
        sched_block();
    }
    s->count--;
    arch_irqs_enable();
}

void sem_post(semaphore_t *s) {
    arch_irqs_disable();
    s->count++;
    thread_t *w = waitq_pop(&s->waiters);
    if (w) sched_unblock(w);
    arch_irqs_enable();
}

bool sem_trywait(semaphore_t *s) {
    arch_irqs_disable();
    if (s->count <= 0) {
        arch_irqs_enable();
        return false;
    }
    s->count--;
    arch_irqs_enable();
    return true;
}
