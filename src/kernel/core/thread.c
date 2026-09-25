#include <exyde/thread.h>
#include <exyde/sched.h>
#include <exyde/heap.h>
#include <exyde/vmm.h>
#include <exyde/panic.h>

#define THREAD_STACK_SIZE  (16u * 1024u)

typedef struct {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 rbx;
    u64 rbp;
    u64 rip;
} initial_frame_t;

_Static_assert(sizeof(initial_frame_t) == 56, "initial_frame_t size");

extern void thread_trampoline(void);

static u64 next_thread_id = 0;

thread_t *thread_create(thread_fn_t fn, void *arg, const char *name) {
    if (!fn) return (thread_t *)0;

    thread_t *t = (thread_t *)exy_zalloc(sizeof(thread_t));
    if (!t) return (thread_t *)0;

    u8 *stack = (u8 *)exy_malloc(THREAD_STACK_SIZE);
    if (!stack) {
        exy_free(t);
        return (thread_t *)0;
    }

    vaddr_t top = (vaddr_t)stack + THREAD_STACK_SIZE;
    initial_frame_t *f = (initial_frame_t *)(top - sizeof(initial_frame_t));

    f->r15 = 0;
    f->r14 = 0;
    f->r13 = (u64)arg;
    f->r12 = (u64)fn;
    f->rbx = 0;
    f->rbp = 0;
    f->rip = (u64)thread_trampoline;

    t->rsp              = (u64)f;
    t->state            = THREAD_READY;
    t->id               = next_thread_id++;
    t->ticks_used       = 0;
    t->stack_base       = (vaddr_t)stack;
    t->stack_size       = THREAD_STACK_SIZE;
    t->kernel_stack_top = top;
    t->space            = vmm_kernel_space();
    t->process          = (void *)0;
    t->entry            = fn;
    t->arg              = arg;
    t->next             = (thread_t *)0;
    t->wait_next        = (thread_t *)0;
    t->name             = name;

    sched_enqueue(t);
    return t;
}

thread_t *thread_create_ex(thread_fn_t fn, void *arg, const char *name,
                           vmm_space_t space) {
    thread_t *t = thread_create(fn, arg, name);
    if (t) t->space = space;
    return t;
}
