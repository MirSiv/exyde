#ifndef EXYDE_THREAD_H
#define EXYDE_THREAD_H

#include <exyde/types.h>
#include <exyde/vmm.h>

typedef struct thread thread_t;

typedef enum {
    THREAD_READY = 0,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD,
} thread_state_t;

typedef void (*thread_fn_t)(void *arg);

struct thread {
    u64             rsp;
    thread_state_t  state;
    u64             id;
    u32             ticks_used;
    u32             _pad;
    vaddr_t         stack_base;
    size_t          stack_size;
    vaddr_t         kernel_stack_top;
    vmm_space_t     space;
    void           *process;
    thread_fn_t     entry;
    void           *arg;
    thread_t       *next;       /* ready-queue link */
    thread_t       *wait_next;  /* wait-queue link (mutex/sem/condvar) */
    const char     *name;
};

#endif /* EXYDE_THREAD_H */
