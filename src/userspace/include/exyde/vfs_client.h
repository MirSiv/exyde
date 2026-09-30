#ifndef EXYDE_USERSPACE_VFS_CLIENT_H
#define EXYDE_USERSPACE_VFS_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <exyde/micro.h>

/* Client side of the VFS RPC protocol (multiplexed, Phase 12.3a).
 *
 * Initialise once with vfs_client_init(ch_req).  ch_req is the
 * request channel shared by every client of the same server.  On
 * success the client has created its private reply channel
 * internally, sent ATTACH carrying it, and stored the assigned
 * client_id which is stamped into every subsequent request.
 *
 * The client is single-threaded and synchronous: at most one RPC is
 * in flight at any time.  fd values are u32 opaque tokens minted by
 * the server, namespaced per client. */
int  vfs_client_init(exyde_handle_t ch_req);

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
