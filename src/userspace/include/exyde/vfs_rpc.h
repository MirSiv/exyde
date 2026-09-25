#ifndef EXYDE_USERSPACE_VFS_RPC_H
#define EXYDE_USERSPACE_VFS_RPC_H

#include <stdint.h>

/* VFS RPC protocol, Phase 11.5.3.
 *
 * One client <-> one server over one IPC channel.  Fixed-size
 * messages, synchronous request/response.  Native byte order (x86_64
 * little-endian); a portable wire format comes when there is a
 * second architecture.
 *
 * fd is a u32 index into the SERVER's fd table.  The client treats
 * it as opaque and hands it back in CLOSE / READ / WRITE / SEEK /
 * READDIR.  Per-client fd namespacing is a 11.5.4 concern. */

#define VFS_RPC_PATH_MAX 256
#define VFS_RPC_DATA_MAX 512
#define VFS_RPC_MSG_SIZE 1024

/* ---- op codes --------------------------------------------------- */

#define VFS_OP_ATTACH    0   /* client->server: hand over reply channel */
#define VFS_OP_OPEN      1
#define VFS_OP_CLOSE     2
#define VFS_OP_READ      3
#define VFS_OP_WRITE     4
#define VFS_OP_SEEK      5
#define VFS_OP_MKDIR     6
#define VFS_OP_UNLINK    7
#define VFS_OP_RMDIR     8
#define VFS_OP_READDIR   9
#define VFS_OP_SHUTDOWN  10

/* ---- message layout --------------------------------------------- */

struct vfs_req {
    uint32_t op;                    /* VFS_OP_*                    */
    uint32_t fd;                    /* CLOSE/READ/WRITE/SEEK/READDIR */
    uint64_t arg0;                  /* flags / off / mode / index / len */
    uint64_t arg1;                  /* mode / whence               */
    uint32_t data_len;              /* WRITE: bytes in data[]      */
    uint32_t _pad;
    char     path[VFS_RPC_PATH_MAX];  /* OPEN/MKDIR/UNLINK/RMDIR, NUL-terminated */
    uint8_t  data[VFS_RPC_DATA_MAX];  /* WRITE payload */
};

struct vfs_rsp {
    int64_t  ret;                   /* >=0 success, -errno on failure */
    uint64_t aux;                   /* OPEN: fd; SEEK: new_off      */
    uint32_t data_len;              /* READ/READDIR: bytes in data[] */
    uint32_t _pad;
    uint8_t  data[VFS_RPC_DATA_MAX];  /* READ content / READDIR name */
};

_Static_assert(sizeof(struct vfs_req) <= VFS_RPC_MSG_SIZE,
               "vfs_req must fit in one RPC message");
_Static_assert(sizeof(struct vfs_rsp) <= VFS_RPC_MSG_SIZE,
               "vfs_rsp must fit in one RPC message");

#endif /* EXYDE_USERSPACE_VFS_RPC_H */
