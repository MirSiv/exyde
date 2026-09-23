#include <exyde/ramfs.h>
#include <exyde/heap.h>
#include <exyde/errno.h>

/* RAMFS node.  vnode_t is embedded as the first member; vn.data
 * points back at the ramfs_node so ops can recover it. */
typedef struct ramfs_node {
    vnode_t    vn;
    char       name[VFS_NAME_MAX + 1];
    u8        *data;       /* regular file contents */
    u64        capacity;   /* allocated bytes for `data` */
    struct ramfs_node *children;
    struct ramfs_node *next;
} ramfs_node_t;

static u64 ramfs_next_ino = 1;

/* ---- helpers -------------------------------------------------------- */

static size_t xs_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

static bool xs_streq(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void xs_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (src[i] && i + 1 < max) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static ramfs_node_t *R(vnode_t *vn) {
    return (ramfs_node_t *)vn->data;
}

/* ---- forward declarations (so the ops table can be defined early) -- */

static int  ramfs_lookup(vnode_t *dir, const char *name, vnode_t **out);
static int  ramfs_create(vnode_t *dir, const char *name, u32 mode, vnode_t **out);
static int  ramfs_mkdir (vnode_t *dir, const char *name, u32 mode);
static int  ramfs_unlink(vnode_t *dir, const char *name);
static int  ramfs_rmdir (vnode_t *dir, const char *name);
static int  ramfs_readdir(vnode_t *dir, u32 index, char *name, size_t n);
static i64  ramfs_read   (vnode_t *vn, void *buf, size_t n, u64 off);
static i64  ramfs_write  (vnode_t *vn, const void *buf, size_t n, u64 off);
static int  ramfs_truncate(vnode_t *vn, u64 size);
static void ramfs_destroy(vnode_t *vn);

static const vfs_ops_t ramfs_ops = {
    .lookup   = ramfs_lookup,
    .create   = ramfs_create,
    .mkdir    = ramfs_mkdir,
    .unlink   = ramfs_unlink,
    .rmdir    = ramfs_rmdir,
    .readdir  = ramfs_readdir,
    .read     = ramfs_read,
    .write    = ramfs_write,
    .truncate = ramfs_truncate,
    .destroy  = ramfs_destroy,
};

/* ---- node construction ---------------------------------------------- */

static vnode_t *ramfs_new_node(u32 type, u32 mode) {
    ramfs_node_t *n = (ramfs_node_t *)kzalloc(sizeof(ramfs_node_t));
    if (!n) return (vnode_t *)0;
    n->vn.ino      = ramfs_next_ino++;
    n->vn.type     = type;
    n->vn.mode     = mode;
    n->vn.refcount = 1;
    n->vn.size     = 0;
    n->vn.parent   = (vnode_t *)0;
    n->vn.ops      = &ramfs_ops;
    n->vn.data     = n;
    n->data        = (u8 *)0;
    n->capacity    = 0;
    n->children    = (ramfs_node_t *)0;
    n->next        = (ramfs_node_t *)0;
    return &n->vn;
}

vnode_t *ramfs_create_root(void) {
    vnode_t *vn = ramfs_new_node(VFS_TYPE_DIR, 0755);
    if (!vn) return (vnode_t *)0;
    vn->parent = vn;
    return vn;
}

static ramfs_node_t *ramfs_find(ramfs_node_t *dir, const char *name) {
    for (ramfs_node_t *c = dir->children; c; c = c->next)
        if (xs_streq(c->name, name)) return c;
    return (ramfs_node_t *)0;
}

/* ---- directory ops -------------------------------------------------- */

static int ramfs_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    ramfs_node_t *d = R(dir);
    ramfs_node_t *c = ramfs_find(d, name);
    if (!c) return -ENOENT;
    vnode_ref(&c->vn);
    *out = &c->vn;
    return 0;
}

static int ramfs_create(vnode_t *dir, const char *name, u32 mode, vnode_t **out) {
    ramfs_node_t *d = R(dir);
    if (ramfs_find(d, name)) return -EEXIST;
    vnode_t *nv = ramfs_new_node(VFS_TYPE_REG, mode);
    if (!nv) return -ENOMEM;
    ramfs_node_t *nn = R(nv);
    xs_strcpy(nn->name, name, sizeof(nn->name));
    nn->vn.parent = dir;
    nn->next = d->children;
    d->children = nn;

    /* The child list holds one reference; the caller gets another. */
    vnode_ref(nv);
    *out = nv;
    return 0;
}

static int ramfs_mkdir(vnode_t *dir, const char *name, u32 mode) {
    ramfs_node_t *d = R(dir);
    if (ramfs_find(d, name)) return -EEXIST;
    vnode_t *nv = ramfs_new_node(VFS_TYPE_DIR, mode);
    if (!nv) return -ENOMEM;
    ramfs_node_t *nn = R(nv);
    xs_strcpy(nn->name, name, sizeof(nn->name));
    nn->vn.parent = dir;
    nn->next = d->children;
    d->children = nn;
    /* refcount 1 belongs to the child list */
    return 0;
}

static int ramfs_unlink(vnode_t *dir, const char *name) {
    ramfs_node_t *d = R(dir);
    ramfs_node_t **pp = &d->children;
    while (*pp) {
        ramfs_node_t *c = *pp;
        if (xs_streq(c->name, name)) {
            if (c->vn.type == VFS_TYPE_DIR) return -EISDIR;
            *pp = c->next;
            c->next = (ramfs_node_t *)0;
            vnode_unref(&c->vn);
            return 0;
        }
        pp = &c->next;
    }
    return -ENOENT;
}

static int ramfs_rmdir(vnode_t *dir, const char *name) {
    ramfs_node_t *d = R(dir);
    ramfs_node_t **pp = &d->children;
    while (*pp) {
        ramfs_node_t *c = *pp;
        if (xs_streq(c->name, name)) {
            if (c->vn.type != VFS_TYPE_DIR) return -ENOTDIR;
            if (c->children) return -ENOTEMPTY;
            *pp = c->next;
            c->next = (ramfs_node_t *)0;
            vnode_unref(&c->vn);
            return 0;
        }
        pp = &c->next;
    }
    return -ENOENT;
}

static int ramfs_readdir(vnode_t *dir, u32 index, char *name, size_t n) {
    ramfs_node_t *d = R(dir);
    u32 i = 0;
    for (ramfs_node_t *c = d->children; c; c = c->next, ++i) {
        if (i == index) {
            size_t len = xs_strlen(c->name);
            if (len + 1 > n) return -ERANGE;
            for (size_t k = 0; k <= len; ++k) name[k] = c->name[k];
            return 0;
        }
    }
    return -ENOENT;
}

/* ---- file ops ------------------------------------------------------- */

static i64 ramfs_read(vnode_t *vn, void *buf, size_t n, u64 off) {
    ramfs_node_t *node = R(vn);
    if (vn->type != VFS_TYPE_REG) return -EISDIR;
    if (off >= vn->size) return 0;
    u64 avail = vn->size - off;
    if ((u64)n > avail) n = (size_t)avail;
    u8 *dst = (u8 *)buf;
    for (size_t i = 0; i < n; ++i) dst[i] = node->data[off + i];
    return (i64)n;
}

static i64 ramfs_write(vnode_t *vn, const void *buf, size_t n, u64 off) {
    ramfs_node_t *node = R(vn);
    if (vn->type != VFS_TYPE_REG) return -EISDIR;
    if (n == 0) return 0;

    u64 need = off + (u64)n;
    if (need < off) return -EINVAL;   /* overflow */

    if (need > node->capacity) {
        u64 newcap = node->capacity ? node->capacity : 64;
        while (newcap < need) {
            if (newcap > (1ULL << 62)) return -ENOMEM;
            newcap *= 2;
        }
        u8 *nd = (u8 *)kmalloc((size_t)newcap);
        if (!nd) return -ENOMEM;
        for (u64 i = 0; i < vn->size; ++i) nd[i] = node->data[i];
        for (u64 i = vn->size; i < off && i < newcap; ++i) nd[i] = 0;
        if (node->data) kfree(node->data);
        node->data = nd;
        node->capacity = newcap;
    }

    u8 *dst = node->data;
    const u8 *src = (const u8 *)buf;
    for (size_t i = 0; i < n; ++i) dst[off + i] = src[i];
    if (off + (u64)n > vn->size) vn->size = off + (u64)n;
    return (i64)n;
}

static int ramfs_truncate(vnode_t *vn, u64 size) {
    ramfs_node_t *node = R(vn);
    if (vn->type != VFS_TYPE_REG) return -EISDIR;
    if (size == vn->size) return 0;

    if (size <= node->capacity) {
        if (size > vn->size) {
            for (u64 i = vn->size; i < size; ++i) node->data[i] = 0;
        }
        vn->size = size;
        return 0;
    }

    u8 *nd = (u8 *)kzalloc((size_t)size);
    if (!nd) return -ENOMEM;
    if (node->data) {
        for (u64 i = 0; i < vn->size && i < size; ++i) nd[i] = node->data[i];
        kfree(node->data);
    }
    node->data     = nd;
    node->capacity = size;
    vn->size       = size;
    return 0;
}

/* ---- lifecycle ------------------------------------------------------ */

static void ramfs_destroy(vnode_t *vn) {
    ramfs_node_t *node = R(vn);
    ramfs_node_t *c = node->children;
    while (c) {
        ramfs_node_t *nxt = c->next;
        c->next = (ramfs_node_t *)0;
        vnode_unref(&c->vn);
        c = nxt;
    }
    if (node->data) kfree(node->data);
    kfree(node);
}
