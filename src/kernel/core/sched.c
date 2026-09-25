#include <exyde/sched.h>
#include <exyde/thread.h>
#include <exyde/arch.h>
#include <exyde/vmm.h>
#include <exyde/user.h>
#include <exyde/heap.h>
#include <exyde/process.h>
#include <exyde/panic.h>

#define SCHED_QUANTUM_TICKS 5u
#define BOOT_STACK_SIZE     (16u * 1024u)

extern void thread_switch(u64 *from_rsp, u64 *to_rsp);

static thread_t      *ready_head;
static thread_t      *ready_tail;
static thread_t      *current;
static size_t         ready_count;
static volatile int   need_resched;

/* Zombie list: threads that called thread_exit.  They are not freed
 * until another thread is running on the CPU, so we never exy_free the
 * stack we are currently executing on. */
static thread_t      *zombie_head;
static thread_t      *zombie_tail;

/* Real kernel stack for the boot/idle thread. */
static u8 boot_stack[BOOT_STACK_SIZE] __attribute__((aligned(16)));
static thread_t boot_thread;

void sched_init(void) {
    boot_thread.rsp              = 0;
    boot_thread.state            = THREAD_RUNNING;
    boot_thread.id               = 0;
    boot_thread.ticks_used       = 0;
    boot_thread.stack_base       = 0;
    boot_thread.stack_size       = 0;
    boot_thread.kernel_stack_top = (vaddr_t)(uintptr_t)(boot_stack + sizeof(boot_stack));
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
    zombie_head  = (thread_t *)0;
    zombie_tail  = (thread_t *)0;
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

/* ---- zombie handling ------------------------------------------------ */

static void zombie_push(thread_t *t) {
    t->next = (thread_t *)0;
    if (zombie_tail) {
        zombie_tail->next = t;
        zombie_tail = t;
    } else {
        zombie_head = zombie_tail = t;
    }
}

static void free_thread(thread_t *t) {
    /* The main thread of a user process owns the process's primary
     * reference.  Release it here.  If a parent still holds a handle
     * on the process (SYS_WAIT pending), the process survives as a
     * zombie-like object with exited=1 and an exit_code until that
     * handle is closed. */
    if (t->process) {
        process_t *p = (process_t *)t->process;
        if (p->main_thread == t) {
            process_unref(p);
        }
    }
    /* Drop the thread's own reference to the address space.  For
     * kernel-space threads this is a no-op. */
    if (t->space) vmm_space_unref(t->space);
    if (t->stack_base) exy_free((void *)(uintptr_t)t->stack_base);
    exy_free(t);
}

/* Runs on the newly-current thread's stack.  Any zombie is guaranteed
 * not to be the one on the CPU, so all of them can be freed. */
static void reap_zombies(void) {
    thread_t *z = zombie_head;
    zombie_head = zombie_tail = (thread_t *)0;
    while (z) {
        thread_t *nxt = z->next;
        z->next = (thread_t *)0;
        free_thread(z);
        z = nxt;
    }
}

/* ---- scheduling ----------------------------------------------------- */

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

    /* We are now running on next's stack: any zombie is safe to free. */
    reap_zombies();
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
    zombie_push(current);
    sched_schedule();
    panic("thread_exit: returned");
}

void sched_block(void) {
    current->state = THREAD_BLOCKED;
    sched_schedule();
}

void sched_unblock(thread_t *t) {
    if (t->state != THREAD_BLOCKED) return;
    t->state = THREAD_READY;
    sched_enqueue(t);
}
