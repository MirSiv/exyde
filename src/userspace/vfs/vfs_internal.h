#ifndef EXYDE_VFS_INTERNAL_H
#define EXYDE_VFS_INTERNAL_H

/* Userspace-side copy of the Exyde VFS types and interface.
 *
 * Originally copied from the kernel VFS in Phase 11.5.3.  The kernel
 * copy was removed in 11.5.6; this is the only VFS now. */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VFS_NAME_MAX  255
#define VFS_PATH_MAX  1024

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;
typedef int64_t  i64;

#define VFS_TYPE_NONE     0u
#define VFS_TYPE_REG      1u
#define VFS_TYPE_DIR      2u
#define VFS_TYPE_CHR      3u
#define VFS_TYPE_BLK      4u
#define VFS_TYPE_FIFO     5u
#define VFS_TYPE_SYMLINK  6u

#define VFS_O_RDONLY  0x0001u
#define VFS_O_WRONLY  0x0002u
#define VFS_O_RDWR    0x0003u
#define VFS_O_CREAT   0x0100u
#define VFS_O_EXCL    0x0200u
#define VFS_O_TRUNC   0x0400u
#define VFS_O_APPEND  0x0800u

#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

typedef struct vnode vnode_t;
typedef struct file  file_t;

typedef struct vfs_ops {
    int (*lookup)(vnode_t *dir, const char *name, vnode_t **out);
    int (*create)(vnode_t *dir, const char *name, u32 mode, vnode_t **out);
    int (*mkdir)(vnode_t *dir, const char *name, u32 mode);
    int (*unlink)(vnode_t *dir, const char *name);
    int (*rmdir)(vnode_t *dir, const char *name);
    int (*readdir)(vnode_t *dir, u32 index, char *name, size_t n);

    i64 (*read)(vnode_t *vn, void *buf, size_t n, u64 off);
    i64 (*write)(vnode_t *vn, const void *buf, size_t n, u64 off);
    int (*truncate)(vnode_t *vn, u64 size);

    void (*destroy)(vnode_t *vn);
} vfs_ops_t;

struct vnode {
    u64              ino;
    u32              type;
    u32              mode;
    u32              refcount;
    u32              _pad;
    u64              size;
    vnode_t         *parent;
    void            *data;
    const vfs_ops_t *ops;
};

struct file {
    vnode_t *vn;
    u64      offset;
    u32      flags;
    u32      refcount;
};

/* ---- VFS core --------------------------------------------------- */

void vfs_init(void);
void vfs_mount_root(vnode_t *root);

vnode_t *vnode_ref(vnode_t *vn);
void     vnode_unref(vnode_t *vn);

int  vfs_resolve(const char *path, vnode_t **out);
int  vfs_open(const char *path, u32 flags, u32 mode, file_t **out);
int  vfs_close(file_t *f);
i64  vfs_read(file_t *f, void *buf, size_t n);
i64  vfs_write(file_t *f, const void *buf, size_t n);
i64  vfs_seek(file_t *f, i64 off, int whence);
int  vfs_mkdir(const char *path, u32 mode);
int  vfs_unlink(const char *path);
int  vfs_rmdir(const char *path);

/* ---- RAMFS ------------------------------------------------------ */

vnode_t *ramfs_create_root(void);

/* ---- server-side fd table -------------------------------------- */

#define VFS_FD_MAX 64

typedef struct {
    file_t *files[VFS_FD_MAX];
} vfs_fd_table_t;

void    vfs_fd_init(vfs_fd_table_t *t);
int     vfs_fd_alloc(vfs_fd_table_t *t, file_t *f);
file_t *vfs_fd_get(vfs_fd_table_t *t, int fd);
int     vfs_fd_close(vfs_fd_table_t *t, int fd);

#endif /* EXYDE_VFS_INTERNAL_H */
