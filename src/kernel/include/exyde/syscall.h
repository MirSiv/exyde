#ifndef EXYDE_SYSCALL_H
#define EXYDE_SYSCALL_H

#include <exyde/types.h>
#include <exyde/errno.h>

/* Native Exyde ABI.
 *
 * Phase 11.5.0: this is the microkernel ABI.  The kernel exposes
 * capabilities, IPC, memory primitives, and process/thread primitives,
 * and nothing else.
 *
 * The VFS/fd/brk syscalls below are TRANSITIONAL.  They still exist so
 * that pre-11.5 userspace (init.elf) keeps working while VFS is moved
 * to a userspace server step by step (see EXYDE_PHASES.md, Phase 11.5).
 * They are NOT part of the microkernel target ABI and will be removed
 * in Phase 11.5.6.  Do not write new code against them. */

/* ---- microkernel core (permanent) ---------------------------------- */

#define SYS_PING           0    /* ()                       -> u64         */
#define SYS_EXIT           2    /* (code)                   -> never       */
#define SYS_HANDLE_CREATE  3    /* (kind, rights, object)   -> handle      */
#define SYS_HANDLE_CLOSE   4    /* (handle)                 -> 0           */
#define SYS_HANDLE_QUERY   5    /* (handle, required)       -> 1 / -EPERM  */
#define SYS_GETPID         10   /* ()                       -> pid         */

#define SYS_IPC_CREATE     12   /* (msg_size, capacity)     -> handle      */
#define SYS_IPC_SEND       13   /* (handle, buf, len)       -> 0           */
#define SYS_IPC_RECV       14   /* (handle, buf, max)       -> msg_size    */
#define SYS_IPC_TRY_SEND   15   /* (handle, buf, len)       -> 0 / -EAGAIN */
#define SYS_IPC_TRY_RECV   16   /* (handle, buf, max)       -> size/EAGAIN */
#define SYS_MAP            17   /* (hint, npages, flags)    -> vaddr       */
#define SYS_UNMAP          18   /* (vaddr, npages)          -> 0           */
#define SYS_YIELD          19   /* ()                       -> 0           */

/* Phase 11.5.1: IPC with one capability per message.  action is
 * 1 (TRANSFER -- sender loses the handle) or 2 (DUPLICATE --
 * sender keeps it).  cap == HANDLE_INVALID means plain send.
 * RECV_CAP always writes the resulting handle (or INVALID) to
 * *out_cap_u. */
#define SYS_IPC_SEND_CAP     20  /* (ch, buf, len, cap, act)  -> 0          */
#define SYS_IPC_RECV_CAP     21  /* (ch, buf, max, out_cap)   -> msg_size   */
#define SYS_IPC_TRY_RECV_CAP 22  /* (ch, buf, max, out_cap)   -> size/EAGAIN */

/* Phase 11.5.2: process management from userspace. */
#define SYS_SPAWN            23  /* (name, argv, argc, cap, flags) -> handle */
#define SYS_WAIT             24  /* (proc_handle)                  -> exit_code */
#define SYS_GET_BOOTSTRAP    25  /* ()                             -> handle  */

/* Kernel console write, no fd, no VFS.  Used by userspace before a
 * console server is reachable (bootstrap diagnostics) and as a
 * permanent low-level debug primitive.  Number 1 previously carried
 * the transitional fd-based SYS_WRITE, which is being retired in
 * Phase 11.5.6. */
#define SYS_KPUTS          1    /* (buf, len)               -> bytes       */

/* ---- transitional: VFS / fd / brk (remove in 11.5.6) --------------- */

#define SYS_READ           6
#define SYS_OPEN           7
#define SYS_CLOSE          8
#define SYS_LSEEK          9
#define SYS_BRK            11

#define SYSCALL_MAX        26

/* Called from the arch syscall entry (ring 0, on the current thread's
 * kernel stack, IF enabled).  Returns a signed value: >=0 success,
 * -errno on failure. */
sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4);

/* Arch-side initialization: programs MSRs, sets up GS-based state.
 * Must be called once, before any syscall can be made. */
void syscall_arch_init(void);

#endif /* EXYDE_SYSCALL_H */
