#ifndef EXYDE_LIBC_INTERNAL_SYSCALL_H
#define EXYDE_LIBC_INTERNAL_SYSCALL_H

/* Raw syscall primitive.  Matches src/kernel/arch/x86_64/cpu/syscall.asm:
 *     rax = nr,  rdi/rsi/rdx/r10/r8 = a0..a4
 *     syscall
 *     rax = result (>= 0 or -errno). */

static inline long __exyde_syscall(long n,
                                   long a0, long a1, long a2,
                                   long a3, long a4)
{
    long ret;
    register long r10 __asm__("r10") = a3;
    register long r8  __asm__("r8")  = a4;
    __asm__ volatile ("syscall"
        : "=a" (ret)
        : "a" (n), "D" (a0), "S" (a1), "d" (a2),
          "r" (r10), "r" (r8)
        : "rcx", "r11", "memory");
    return ret;
}

#endif /* EXYDE_LIBC_INTERNAL_SYSCALL_H */
