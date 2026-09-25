#ifndef EXYDE_USERSPACE_ABI_H
#define EXYDE_USERSPACE_ABI_H

/* Native Exyde syscall numbers and errno values.  These MUST match
 * the kernel-side definitions in
 *     src/kernel/include/exyde/syscall.h
 *     src/kernel/include/exyde/errno.h
 * The kernel is the source of truth: any change there must be
 * mirrored here in the same commit.
 *
 * Syscall convention (x86_64):
 *     rax       = nr
 *     rdi, rsi, rdx, r10, r8 = a0..a4
 *     syscall
 *     rax       = result: >= 0 on success, -errno on error.
 *
 * Phase 11.5.0: this is the microkernel ABI.  See EXYDE.md.
 *
 * The VFS/fd/brk numbers below are TRANSITIONAL.  They keep pre-11.5
 * userspace (init.elf) alive while VFS moves to userspace step by
 * step (see EXYDE_PHASES.md, Phase 11.5).  They are not part of the
 * target microkernel ABI and will be removed in Phase 11.5.6.  Do
 * not write new code against them. */

/* ---- microkernel core (permanent) ------------------------------- */

#define SYS_PING           0
#define SYS_EXIT           2
#define SYS_HANDLE_CREATE  3
#define SYS_HANDLE_CLOSE   4
#define SYS_HANDLE_QUERY   5
#define SYS_GETPID         10

#define SYS_IPC_CREATE     12
#define SYS_IPC_SEND       13
#define SYS_IPC_RECV       14
#define SYS_IPC_TRY_SEND   15
#define SYS_IPC_TRY_RECV   16
#define SYS_MAP            17
#define SYS_UNMAP          18
#define SYS_YIELD          19

/* Phase 11.5.1: IPC with one capability per message. */
#define SYS_IPC_SEND_CAP     20
#define SYS_IPC_RECV_CAP     21
#define SYS_IPC_TRY_RECV_CAP 22

/* ---- transitional: VFS / fd / brk (remove in 11.5.6) ------------ */

#define SYS_WRITE          1
#define SYS_READ           6
#define SYS_OPEN           7
#define SYS_CLOSE          8
#define SYS_LSEEK          9
#define SYS_BRK            11

#define SYSCALL_MAX        23

/* ---- errno values ----------------------------------------------- */
/* Linux-compatible, so Phase 17 translation is trivial. */

#define E_OK         0
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define EPIPE        32
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOSYS       38
#define ENOTEMPTY    39

#endif /* EXYDE_USERSPACE_ABI_H */
