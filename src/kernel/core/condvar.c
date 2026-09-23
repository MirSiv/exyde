#include <exyde/condvar.h>
#include <exyde/sched.h>
#include <exyde/arch.h>

void cond_init(condvar_t *c) {
    waitq_init(&c->waiters);
}

void cond_wait(condvar_t *c, mutex_t *m) {
    arch_irqs_disable();
    waitq_push(&c->waiters, thread_current());
    mutex_unlock_irqoff(m);
    sched_block();
    mutex_lock_irqoff(m);
    arch_irqs_enable();
}

void cond_signal(condvar_t *c) {
    arch_irqs_disable();
    thread_t *w = waitq_pop(&c->waiters);
    if (w) sched_unblock(w);
    arch_irqs_enable();
}

void cond_broadcast(condvar_t *c) {
    arch_irqs_disable();
    thread_t *w;
    while ((w = waitq_pop(&c->waiters))) {
        sched_unblock(w);
    }
    arch_irqs_enable();
}
