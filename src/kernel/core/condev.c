#include <exyde/condev.h>
#include <exyde/console.h>
#include <exyde/heap.h>
#include <exyde/errno.h>

static vnode_t *console_vnode;

static i64 con_read(vnode_t *vn, void *buf, size_t n, u64 off) {
    (void)vn; (void)buf; (void)n; (void)off;
    return 0;   /* EOF: no input yet (keyboard driver arrives later) */
}

static i64 con_write(vnode_t *vn, const void *buf, size_t n, u64 off) {
    (void)vn; (void)off;
    const char *p = (const char *)buf;
    for (size_t i = 0; i < n; ++i) console_write_char(p[i]);
    return (i64)n;
}

static const vfs_ops_t con_ops = {
    .read  = con_read,
    .write = con_write,
};

void condev_init(void) {
    if (console_vnode) return;
    vnode_t *vn = (vnode_t *)exy_zalloc(sizeof(vnode_t));
    if (!vn) return;
    vn->ino      = 0;
    vn->type     = VFS_TYPE_CHR;
    vn->mode     = 0600;
    vn->refcount = 1;
    vn->size     = 0;
    vn->parent   = (vnode_t *)0;
    vn->data     = (void *)0;
    vn->ops      = &con_ops;
    console_vnode = vn;
}

vnode_t *condev_get(void) { return console_vnode; }
vnode_t *condev_ref(void) { return vnode_ref(console_vnode); }
