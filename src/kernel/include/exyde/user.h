#ifndef EXYDE_USER_H
#define EXYDE_USER_H

#include <exyde/types.h>

/* Snapshot handed to a CPL-3 fault handler.  Arch-neutral. */
typedef struct {
    u64 int_no;
    u64 err_code;
    u64 rip;
    u64 rax;     /* useful for post-syscall faults (e.g. int3 after syscall) */
    u64 rsp;
} user_fault_t;

typedef void (*arch_user_fault_fn)(const user_fault_t *f);

void arch_set_user_fault_handler(arch_user_fault_fn fn);

/* Update TSS.RSP0 and the GS-based kernel stack pointer for the next
 * CPL 3 -> CPL 0 transition.  Called from the scheduler on every
 * thread switch. */
void arch_set_kernel_stack(vaddr_t rsp0);

/* Drop into user mode at `entry` with `user_stack_top` as RSP.
 * Never returns. */
void arch_enter_user_mode(vaddr_t entry, vaddr_t user_stack_top)
    __attribute__((noreturn));

#endif /* EXYDE_USER_H */
