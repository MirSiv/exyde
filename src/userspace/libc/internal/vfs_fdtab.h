#ifndef EXYDE_LIBC_INTERNAL_VFS_FDTAB_H
#define EXYDE_LIBC_INTERNAL_VFS_FDTAB_H

#include <stdint.h>

/* Per-process local fd table for the POSIX layer over libvfs.
 *
 * fd 0/1/2 are reserved for stdin/stdout/stderr and are NOT managed
 * here.  open() allocates the lowest free slot >= 3 and records the
 * server-side fd returned by exy-vfs.  read/write/close/lseek
 * translate the local fd back to the server fd and issue the RPC. */

#define VFS_FD_LOCAL_MAX 64

void vfs_fdtab_init(void);
int  vfs_fdtab_alloc(uint32_t server_fd);
int  vfs_fdtab_get(int local_fd, uint32_t *out_server_fd);
int  vfs_fdtab_free(int local_fd);

#endif /* EXYDE_LIBC_INTERNAL_VFS_FDTAB_H */
