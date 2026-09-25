#ifndef EXYDE_USERSPACE_VFS_CLIENT_H
#define EXYDE_USERSPACE_VFS_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <exyde/micro.h>

/* Client side of the VFS RPC protocol.
 *
 * Initialise once with vfs_client_init(ch_req, ch_resp).  ch_req
 * is the channel handed to exy-vfs as its bootstrap; the client
 * first sends VFS_OP_ATTACH on it, carrying ch_resp as a capability.
 * ch_resp is the channel the server will use for all replies.
 *
 * The client is single-threaded and synchronous: at most one RPC is
 * in flight at any time.  fd values are u32 opaque tokens minted by
 * the server. */

int  vfs_client_init(exyde_handle_t ch_req, exyde_handle_t ch_resp);

int  vfs_client_open(const char *path, uint32_t flags, uint32_t mode);
int  vfs_client_close(int fd);
long vfs_client_read (int fd, void *buf, size_t len);
long vfs_client_write(int fd, const void *buf, size_t len);
long vfs_client_seek (int fd, long off, int whence);
int  vfs_client_mkdir(const char *path, uint32_t mode);
int  vfs_client_unlink(const char *path);
int  vfs_client_rmdir (const char *path);
int  vfs_client_readdir(int fd, uint32_t index, char *name, size_t n);
int  vfs_client_shutdown(void);

#endif /* EXYDE_USERSPACE_VFS_CLIENT_H */
