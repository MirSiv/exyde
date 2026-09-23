#ifndef EXYDE_USERSPACE_ERRNO_H
#define EXYDE_USERSPACE_ERRNO_H

#include <exyde/abi.h>

/* Process-local errno.  Single-threaded for now; when userspace
 * threads arrive, this becomes __thread. */
extern int errno;

#endif /* EXYDE_USERSPACE_ERRNO_H */
