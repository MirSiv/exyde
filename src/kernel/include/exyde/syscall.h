#ifndef EXYDE_SYSCALL_H
#define EXYDE_SYSCALL_H

#include <exyde/types.h>
#include <exyde/errno.h>

/* Native Exyde ABI, Phase 7-10.  Numbers are stable. */
#define SYS_PING           0    /* ()                       -> u64         */
#define SYS_WRITE          1    /* (fd, buf, count)         -> bytes       */
#define SYS_EXIT           2    /* (code)                   -> never        */
#define SYS_HANDLE_CREATE  3    /* (kind, rights, object)   -> handle       */
#define SYS_HANDLE_CLOSE   4    /* (handle)                 -> 0            */
#define SYS_HANDLE_QUERY   5    /* (handle, required)       -> 1 / -EPERM   */
#define SYS_READ           6    /* (fd, buf, count)         -> bytes       */
#define SYS_OPEN           7    /* (path, flags, mode)      -> fd          */
#define SYS_CLOSE          8    /* (fd)                     -> 0            */
#define SYS_LSEEK          9    /* (fd, off, whence)        -> new offset  */
#define SYS_GETPID         10   /* ()                       -> pid          */

#define SYSCALL_MAX        11

/* Called from the arch syscall entry (ring 0, on the current thread's
 * kernel stack, IF enabled).  Returns a signed value: >=0 success,
 * -errno on failure. */
sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4);

/* Arch-side initialization: programs MSRs, sets up GS-based state.
 * Must be called once, before any syscall can be made. */
void syscall_arch_init(void);

#endif /* EXYDE_SYSCALL_H */
