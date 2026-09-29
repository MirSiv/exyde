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
 * The transitional VFS / fd / brk syscalls of the pre-11.5 era are
 * gone.  All I/O and memory growth now live in userspace. */

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

/* Phase 11.5.2: process management from userspace.
 *
 * Number 23 was SYS_SPAWN (spawn from the kernel's embedded ELF
 * table).  It was retired in Phase 11.5.8 when the kernel stopped
 * carrying an ELF table; all spawning now goes through SYS_SPAWN_ELF
 * below.  The number remains reserved and must not be reused. */
#define SYS_WAIT             24  /* (proc_handle)                  -> exit_code */
#define SYS_GET_BOOTSTRAP    25  /* ()                             -> handle  */

/* Phase 11.5.7: spawn from an ELF image supplied by userspace.
 * `elf_u` is a user VA holding the whole ELF64 static executable;
 * `name_u` is a user VA holding a NUL-terminated label (only used
 * as the process name and argv[0]).  This is the only spawn path
 * after Phase 11.5.8. */
#define SYS_SPAWN_ELF        26  /* (elf_u, elf_size, name_u, cap, flags) -> handle */

/* Kernel console I/O, no fd, no VFS.  Stop-gap primitives until
 * Phase 13 (device servers).  Number 1 used to carry the transitional
 * fd-based SYS_WRITE.
 *
 * Phase 12.0: renamed from SYS_KPUTS.  The 'k' prefix is a Linux
 * convention that does not belong in the Exyde namespace; the
 * kernel-side helper prefix is already `exy_` (pending refactor).
 * SYS_EXY_GETS is non-blocking: it returns whatever is currently
 * in the UART RX FIFO (1..max bytes) or -EAGAIN if nothing is
 * available.  Blocking input is a userspace concern (the console
 * server polls with SYS_YIELD). */
#define SYS_EXY_PUTS          1  /* (buf, len)               -> bytes       */
#define SYS_EXY_GETS         27  /* (buf, max)               -> bytes / -EAGAIN */

/* Numbers 6, 7, 8, 9, 11 were transitional VFS / fd / brk syscalls.
 * They are retired; the numbers remain reserved and must not be
 * reused. */

#define SYSCALL_MAX        28

/* Called from the arch syscall entry (ring 0, on the current thread's
 * kernel stack, IF enabled).  Returns a signed value: >=0 success,
 * -errno on failure. */
sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4);

/* Arch-side initialization: programs MSRs, sets up GS-based state.
 * Must be called once, before any syscall can be made. */
void syscall_arch_init(void);

#endif /* EXYDE_SYSCALL_H */
