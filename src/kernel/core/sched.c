#include <exyde/sched.h>
#include <exyde/thread.h>
#include <exyde/arch.h>
#include <exyde/vmm.h>
#include <exyde/user.h>
#include <exyde/panic.h>

#define SCHED_QUANTUM_TICKS 5u

extern void thread_switch(u64 *from_rsp, u64 *to_rsp);

static thread_t      *ready_head;
static thread_t      *ready_tail;
static thread_t      *current;
static size_t         ready_count;
static volatile int   need_resched;

void sched_init(void) {
    static thread_t boot_thread;
    boot_thread.rsp              = 0;
    boot_thread.state            = THREAD_RUNNING;
    boot_thread.id               = 0;
    boot_thread.ticks_used       = 0;
    boot_thread.stack_base       = 0;
    boot_thread.stack_size       = 0;
    boot_thread.kernel_stack_top = 0;
    boot_thread.space            = vmm_kernel_space();
    boot_thread.process          = (void *)0;
    boot_thread.entry            = (thread_fn_t)0;
    boot_thread.arg              = (void *)0;
    boot_thread.next             = (thread_t *)0;
    boot_thread.wait_next        = (thread_t *)0;
    boot_thread.name             = "boot";

    current      = &boot_thread;
    ready_head   = (thread_t *)0;
    ready_tail   = (thread_t *)0;
    ready_count  = 0;
    need_resched = 0;
}

thread_t *thread_current(void) {
    return current;
}

void sched_enqueue(thread_t *t) {
    t->next = (thread_t *)0;
    if (ready_tail) {
        ready_tail->next = t;
        ready_tail = t;
    } else {
        ready_head = ready_tail = t;
    }
    ready_count++;
}

static thread_t *ready_dequeue(void) {
    thread_t *t = ready_head;
    if (!t) return (thread_t *)0;
    ready_head = t->next;
    if (!ready_head) ready_tail = (thread_t *)0;
    t->next = (thread_t *)0;
    ready_count--;
    return t;
}

size_t sched_ready_count(void) {
    return ready_count;
}

static void sched_schedule(void) {
    thread_t *prev = current;
    thread_t *next = ready_dequeue();

    if (!next) {
        if (prev->state == THREAD_RUNNING) return;
        panic("sched: no runnable thread");
    }

    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        sched_enqueue(prev);
    }
    next->state      = THREAD_RUNNING;
    next->ticks_used = 0;
    current          = next;

    arch_set_kernel_stack(next->kernel_stack_top);

    if (next->space != prev->space) {
        vmm_switch(next->space);
    }

    thread_switch(&prev->rsp, &next->rsp);
}

void sched_yield(void) {
    arch_irqs_disable();
    if (ready_head) {
        sched_schedule();
    }
    arch_irqs_enable();
}

void sched_tick(void) {
    if (!current) return;
    if (++current->ticks_used >= SCHED_QUANTUM_TICKS) {
        current->ticks_used = 0;
        if (ready_head) {
            need_resched = 1;
        }
    }
}

void sched_preempt_point(void) {
    if (need_resched) {
        need_resched = 0;
        sched_schedule();
    }
}

void thread_exit(void) {
    arch_irqs_disable();
    current->state = THREAD_DEAD;
    sched_schedule();
    panic("thread_exit: returned");
}

void sched_block(void) {
    current->state = THREAD_BLOCKED;
    sched_schedule();
    /* On return, IRQs are still disabled (as they were on entry). */
}

void sched_unblock(thread_t *t) {
    if (t->state != THREAD_BLOCKED) return;
    t->state = THREAD_READY;
    sched_enqueue(t);
}
