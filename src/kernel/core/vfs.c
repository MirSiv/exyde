#include <exyde/vfs.h>
#include <exyde/heap.h>
#include <exyde/panic.h>

static vnode_t *vfs_root = (vnode_t *)0;

/* ---- tiny string helpers (no libc in the kernel) -------------------- */

static size_t xs_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

/* ---- vnode lifecycle ------------------------------------------------ */

vnode_t *vnode_ref(vnode_t *vn) {
    if (vn) vn->refcount++;
    return vn;
}

void vnode_unref(vnode_t *vn) {
    if (!vn) return;
    if (vn->refcount == 0) return;   /* guard against double unref */
    if (--vn->refcount == 0) {
        if (vn->ops && vn->ops->destroy) vn->ops->destroy(vn);
    }
}

/* ---- access-mode helpers -------------------------------------------- */

static inline bool acc_read(u32 flags) {
    return (flags & VFS_O_RDWR) != VFS_O_WRONLY;
}
static inline bool acc_write(u32 flags) {
    return (flags & VFS_O_RDWR) != VFS_O_RDONLY;
}

/* ---- path walking --------------------------------------------------- */

static int resolve_from(vnode_t *start, const char *path, vnode_t **out) {
    vnode_t *cur = vnode_ref(start);
    const char *p = path;

    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;

        char name[VFS_NAME_MAX + 1];
        size_t len = 0;
        while (p[len] && p[len] != '/') {
            if (len >= VFS_NAME_MAX) {
                vnode_unref(cur);
                return -ENAMETOOLONG;
            }
            name[len] = p[len];
            ++len;
        }
        name[len] = '\0';
        p += len;

        if (len == 1 && name[0] == '.') continue;

        if (len == 2 && name[0] == '.' && name[1] == '.') {
            vnode_t *par = cur->parent ? cur->parent : cur;
            if (par != cur) {
                vnode_ref(par);
                vnode_unref(cur);
                cur = par;
            }
            continue;
        }

        if (cur->type != VFS_TYPE_DIR || !cur->ops || !cur->ops->lookup) {
            vnode_unref(cur);
            return -ENOTDIR;
        }
        vnode_t *next = (vnode_t *)0;
        int r = cur->ops->lookup(cur, name, &next);
        if (r < 0) {
            vnode_unref(cur);
            return r;
        }
        vnode_unref(cur);
        cur = next;
    }

    *out = cur;
    return 0;
}

void vfs_init(void) {
    vfs_root = (vnode_t *)0;
}

void vfs_mount_root(vnode_t *root) {
    if (vfs_root) vnode_unref(vfs_root);
    vfs_root = root;
    if (vfs_root) vfs_root->parent = vfs_root;
}

int vfs_resolve(const char *path, vnode_t **out) {
    if (!vfs_root) return -ENODEV;
    if (!path || !out) return -EINVAL;
    if (path[0] != '/') return -EINVAL;   /* absolute paths only (no cwd yet) */
    return resolve_from(vfs_root, path, out);
}

/* Split "/a/b/c" into parent "/a/b" and name "c".  On success the
 * caller owns *parent_out. */
static int split_parent(const char *path, char *name_out, vnode_t **parent_out) {
    size_t len = xs_strlen(path);
    if (len == 0 || len > VFS_PATH_MAX) return -ENAMETOOLONG;

    size_t i = len;
    while (i > 0 && path[i - 1] != '/') --i;
    if (i >= len) return -EINVAL;   /* trailing '/' */

    size_t nlen = len - i;
    if (nlen == 0 || nlen > VFS_NAME_MAX) return -ENAMETOOLONG;
    for (size_t k = 0; k < nlen; ++k) name_out[k] = path[i + k];
    name_out[nlen] = '\0';

    char dirpath[VFS_PATH_MAX + 1];
    if (i == 0) {
        dirpath[0] = '/';
        dirpath[1] = '\0';
    } else {
        for (size_t k = 0; k < i; ++k) dirpath[k] = path[k];
        dirpath[i] = '\0';
    }
    return vfs_resolve(dirpath, parent_out);
}

/* ---- open / close --------------------------------------------------- */

int vfs_open(const char *path, u32 flags, u32 mode, file_t **out) {
    if (!out) return -EINVAL;

    vnode_t *vn = (vnode_t *)0;
    int r = vfs_resolve(path, &vn);

    if (r == -ENOENT && (flags & VFS_O_CREAT)) {
        char name[VFS_NAME_MAX + 1];
        vnode_t *parent = (vnode_t *)0;
        r = split_parent(path, name, &parent);
        if (r < 0) return r;
        if (parent->type != VFS_TYPE_DIR || !parent->ops || !parent->ops->create) {
            vnode_unref(parent);
            return -ENOTDIR;
        }
        r = parent->ops->create(parent, name, mode, &vn);
        vnode_unref(parent);
        if (r < 0) return r;
    } else if (r < 0) {
        return r;
    } else if ((flags & (VFS_O_CREAT | VFS_O_EXCL)) == (VFS_O_CREAT | VFS_O_EXCL)) {
        vnode_unref(vn);
        return -EEXIST;
    }

    if ((flags & VFS_O_TRUNC) && acc_write(flags) &&
        vn->type == VFS_TYPE_REG && vn->ops && vn->ops->truncate) {
        r = vn->ops->truncate(vn, 0);
        if (r < 0) { vnode_unref(vn); return r; }
    }

    file_t *f = (file_t *)kzalloc(sizeof(file_t));
    if (!f) { vnode_unref(vn); return -ENOMEM; }
    f->vn       = vn;   /* takes the reference from vfs_resolve/create */
    f->offset   = 0;
    f->flags    = flags;
    f->refcount = 1;
    *out = f;
    return 0;
}

int vfs_close(file_t *f) {
    if (!f) return -EINVAL;
    if (f->refcount > 1) {
        f->refcount--;
        return 0;
    }
    vnode_unref(f->vn);
    kfree(f);
    return 0;
}

/* ---- I/O ------------------------------------------------------------ */

i64 vfs_read(file_t *f, void *buf, size_t n) {
    if (!f || !buf) return -EINVAL;
    if (!acc_read(f->flags)) return -EBADF;
    vnode_t *vn = f->vn;
    if (!vn->ops || !vn->ops->read) return -EISDIR;
    i64 r = vn->ops->read(vn, buf, n, f->offset);
    if (r > 0) f->offset += (u64)r;
    return r;
}

i64 vfs_write(file_t *f, const void *buf, size_t n) {
    if (!f || !buf) return -EINVAL;
    if (!acc_write(f->flags)) return -EBADF;
    vnode_t *vn = f->vn;
    if (!vn->ops || !vn->ops->write) return -EISDIR;
    u64 off = (f->flags & VFS_O_APPEND) ? vn->size : f->offset;
    i64 r = vn->ops->write(vn, buf, n, off);
    if (r > 0) f->offset = off + (u64)r;
    return r;
}

i64 vfs_seek(file_t *f, i64 off, int whence) {
    if (!f) return -EINVAL;
    i64 base;
    switch (whence) {
        case VFS_SEEK_SET: base = 0;                    break;
        case VFS_SEEK_CUR: base = (i64)f->offset;       break;
        case VFS_SEEK_END: base = (i64)f->vn->size;     break;
        default: return -EINVAL;
    }
    i64 new_off = base + off;
    if (new_off < 0) return -EINVAL;
    f->offset = (u64)new_off;
    return new_off;
}

/* ---- directory mutation --------------------------------------------- */

int vfs_mkdir(const char *path, u32 mode) {
    char name[VFS_NAME_MAX + 1];
    vnode_t *parent = (vnode_t *)0;
    int r = split_parent(path, name, &parent);
    if (r < 0) return r;
    if (parent->type != VFS_TYPE_DIR || !parent->ops || !parent->ops->mkdir) {
        vnode_unref(parent);
        return -ENOTDIR;
    }
    r = parent->ops->mkdir(parent, name, mode);
    vnode_unref(parent);
    return r;
}

int vfs_unlink(const char *path) {
    char name[VFS_NAME_MAX + 1];
    vnode_t *parent = (vnode_t *)0;
    int r = split_parent(path, name, &parent);
    if (r < 0) return r;
    if (parent->type != VFS_TYPE_DIR || !parent->ops || !parent->ops->unlink) {
        vnode_unref(parent);
        return -ENOTDIR;
    }
    r = parent->ops->unlink(parent, name);
    vnode_unref(parent);
    return r;
}

int vfs_rmdir(const char *path) {
    char name[VFS_NAME_MAX + 1];
    vnode_t *parent = (vnode_t *)0;
    int r = split_parent(path, name, &parent);
    if (r < 0) return r;
    if (parent->type != VFS_TYPE_DIR || !parent->ops || !parent->ops->rmdir) {
        vnode_unref(parent);
        return -ENOTDIR;
    }
    r = parent->ops->rmdir(parent, name);
    vnode_unref(parent);
    return r;
}
