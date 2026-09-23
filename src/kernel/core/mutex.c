#include <exyde/mutex.h>
#include <exyde/sched.h>
#include <exyde/arch.h>
#include <exyde/panic.h>

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner  = (thread_t *)0;
    waitq_init(&m->waiters);
}

void mutex_lock_irqoff(mutex_t *m) {
    thread_t *cur = thread_current();
    while (m->locked) {
        waitq_push(&m->waiters, cur);
        sched_block();
    }
    m->locked = 1;
    m->owner  = cur;
}

void mutex_unlock_irqoff(mutex_t *m) {
    if (!m->locked) panic("mutex: unlock of unlocked");
    if (m->owner != thread_current()) panic("mutex: unlock by non-owner");
    m->locked = 0;
    m->owner  = (thread_t *)0;
    thread_t *w = waitq_pop(&m->waiters);
    if (w) sched_unblock(w);
}

void mutex_lock(mutex_t *m) {
    arch_irqs_disable();
    mutex_lock_irqoff(m);
    arch_irqs_enable();
}

bool mutex_trylock(mutex_t *m) {
    arch_irqs_disable();
    if (m->locked) {
        arch_irqs_enable();
        return false;
    }
    m->locked = 1;
    m->owner  = thread_current();
    arch_irqs_enable();
    return true;
}

void mutex_unlock(mutex_t *m) {
    arch_irqs_disable();
    mutex_unlock_irqoff(m);
    arch_irqs_enable();
}
