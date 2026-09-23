#ifndef EXYDE_VFS_H
#define EXYDE_VFS_H

#include <exyde/types.h>
#include <exyde/errno.h>

#define VFS_NAME_MAX  255
#define VFS_PATH_MAX  1024

/* vnode types (Unix-visible behaviour, Exyde-internal naming). */
#define VFS_TYPE_NONE     0u
#define VFS_TYPE_REG      1u
#define VFS_TYPE_DIR      2u
#define VFS_TYPE_CHR      3u
#define VFS_TYPE_BLK      4u
#define VFS_TYPE_FIFO     5u
#define VFS_TYPE_SYMLINK  6u

/* open() flags.  Read/write access is encoded in the low two bits,
 * mirroring the Unix convention without copying Linux's bit layout. */
#define VFS_O_RDONLY  0x0001u
#define VFS_O_WRONLY  0x0002u
#define VFS_O_RDWR    0x0003u
#define VFS_O_CREAT   0x0100u
#define VFS_O_EXCL    0x0200u
#define VFS_O_TRUNC   0x0400u
#define VFS_O_APPEND  0x0800u

/* seek() whence */
#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

typedef struct vnode vnode_t;
typedef struct file  file_t;

/* Per-filesystem operation table.  A vnode points at one of these. */
typedef struct vfs_ops {
    /* Directory operations.  `dir` is of type VFS_TYPE_DIR. */
    int (*lookup)(vnode_t *dir, const char *name, vnode_t **out);
    int (*create)(vnode_t *dir, const char *name, u32 mode, vnode_t **out);
    int (*mkdir)(vnode_t *dir, const char *name, u32 mode);
    int (*unlink)(vnode_t *dir, const char *name);
    int (*rmdir)(vnode_t *dir, const char *name);
    int (*readdir)(vnode_t *dir, u32 index, char *name, size_t n);

    /* Regular-file operations.  `off` is the byte offset. */
    i64 (*read)(vnode_t *vn, void *buf, size_t n, u64 off);
    i64 (*write)(vnode_t *vn, const void *buf, size_t n, u64 off);
    int (*truncate)(vnode_t *vn, u64 size);

    /* Lifecycle: invoked when refcount drops to zero. */
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

/* ---- VFS core ------------------------------------------------------- */

void vfs_init(void);

/* Install `root` as the filesystem root.  Consumes the caller's
 * reference.  Must be called exactly once after vfs_init(). */
void vfs_mount_root(vnode_t *root);

/* Reference counting. */
vnode_t *vnode_ref(vnode_t *vn);
void     vnode_unref(vnode_t *vn);

/* Resolve an absolute path.  On success the caller owns a reference
 * to *out and must vnode_unref() it. */
int vfs_resolve(const char *path, vnode_t **out);

/* Open / close a file.  On success the caller owns *out and must
 * eventually vfs_close() it. */
int vfs_open(const char *path, u32 flags, u32 mode, file_t **out);
int vfs_close(file_t *f);

i64 vfs_read(file_t *f, void *buf, size_t n);
i64 vfs_write(file_t *f, const void *buf, size_t n);
i64 vfs_seek(file_t *f, i64 off, int whence);

/* Directory mutation.  All paths must be absolute. */
int vfs_mkdir(const char *path, u32 mode);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);

#endif /* EXYDE_VFS_H */
