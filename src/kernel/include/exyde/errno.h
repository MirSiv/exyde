#ifndef EXYDE_ERRNO_H
#define EXYDE_ERRNO_H

/* Linux-compatible error numbers, so future Linux ABI translation
 * (Phase 17) is trivial.  Exyde semantics do not depend on them. */
#define E_OK        0
#define EPERM       1
#define ENOENT      2
#define ESRCH       3
#define EINTR       4
#define EIO         5
#define ENXIO       6
#define E2BIG       7
#define ENOEXEC     8
#define EBADF       9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define ENODEV      19
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENFILE      23
#define EMFILE      24
#define ENOSPC      28
#define ESPIPE      29
#define EROFS       30
#define EPIPE       32
#define ERANGE      34
#define ENAMETOOLONG 36
#define ENOSYS      38
#define ENOTEMPTY   39

/* A syscall returns a signed long: >= 0 on success, -errno on error. */
typedef long sysret_t;

#define SYSRET_OK(v)   ((sysret_t)(v))
#define SYSRET_ERR(e)  ((sysret_t)(-(long)(e)))

#endif /* EXYDE_ERRNO_H */
