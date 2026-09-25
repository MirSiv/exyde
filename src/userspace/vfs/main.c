/* exy-vfs -- userspace VFS server, Phase 11.5.3.
 *
 * Spawned by init.elf with a bootstrap channel (ch_req).  The very
 * first message on ch_req must be VFS_OP_ATTACH and must carry the
 * reply channel (ch_resp) as a capability.  After that the server
 * loops: recv request from ch_req, send reply on g_resp.
 *
 * Requests and replies use separate channels so the client can never
 * read its own request back as a reply. */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <exyde/micro.h>
#include <exyde/vfs_rpc.h>
#include "vfs_internal.h"

static vfs_fd_table_t fdtab;
static exyde_handle_t g_resp = EXYDE_HANDLE_INVALID;

static u8 reply_buf[VFS_RPC_MSG_SIZE];

static void reply_ok(i64 ret, u64 aux,
                     const void *data, u32 data_len) {
    struct vfs_rsp *r = (struct vfs_rsp *)reply_buf;
    memset(r, 0, sizeof *r);
    r->ret      = ret;
    r->aux      = aux;
    r->data_len = data_len;
    if (data_len && data) memcpy(r->data, data, data_len);
    exyde_ipc_send(g_resp, reply_buf, VFS_RPC_MSG_SIZE);
}

static void reply_err(int err) {
    struct vfs_rsp *r = (struct vfs_rsp *)reply_buf;
    memset(r, 0, sizeof *r);
    r->ret = -(i64)err;
    exyde_ipc_send(g_resp, reply_buf, VFS_RPC_MSG_SIZE);
}

static int check_path(const struct vfs_req *q) {
    for (size_t i = 0; i < VFS_RPC_PATH_MAX; ++i) {
        if (q->path[i] == '\0') return 0;
    }
    return -EINVAL;
}

static int op_open(const struct vfs_req *q) {
    file_t *f = (file_t *)0;
    int r = vfs_open(q->path, (u32)q->arg0, (u32)q->arg1, &f);
    if (r < 0) return r;
    int fd = vfs_fd_alloc(&fdtab, f);
    if (fd < 0) { vfs_close(f); return fd; }
    return fd;
}

static void handle(const struct vfs_req *q) {
    switch (q->op) {
    case VFS_OP_OPEN: {
        int r = op_open(q);
        if (r < 0) reply_err(-r);
        else       reply_ok (0, (u64)r, (void *)0, 0);
        break;
    }
    case VFS_OP_CLOSE: {
        int r = vfs_fd_close(&fdtab, (int)q->fd);
        if (r < 0) reply_err(-r);
        else       reply_ok (0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_READ: {
        file_t *f = vfs_fd_get(&fdtab, (int)q->fd);
        if (!f) { reply_err(EBADF); break; }
        if (q->arg0 == 0 || q->arg0 > VFS_RPC_DATA_MAX) {
            reply_err(EINVAL); break;
        }
        u8 buf[VFS_RPC_DATA_MAX];
        i64 n = vfs_read(f, buf, (size_t)q->arg0);
        if (n < 0) reply_err((int)-n);
        else       reply_ok (n, 0, buf, (u32)n);
        break;
    }
    case VFS_OP_WRITE: {
        file_t *f = vfs_fd_get(&fdtab, (int)q->fd);
        if (!f) { reply_err(EBADF); break; }
        if (q->data_len == 0 || q->data_len > VFS_RPC_DATA_MAX) {
            reply_err(EINVAL); break;
        }
        i64 n = vfs_write(f, q->data, q->data_len);
        if (n < 0) reply_err((int)-n);
        else       reply_ok (n, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_SEEK: {
        file_t *f = vfs_fd_get(&fdtab, (int)q->fd);
        if (!f) { reply_err(EBADF); break; }
        i64 r = vfs_seek(f, (i64)q->arg0, (int)q->arg1);
        if (r < 0) reply_err((int)-r);
        else       reply_ok (r, (u64)r, (void *)0, 0);
        break;
    }
    case VFS_OP_MKDIR: {
        int r = vfs_mkdir(q->path, (u32)q->arg0);
        if (r < 0) reply_err(-r);
        else       reply_ok (0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_UNLINK: {
        int r = vfs_unlink(q->path);
        if (r < 0) reply_err(-r);
        else       reply_ok (0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_RMDIR: {
        int r = vfs_rmdir(q->path);
        if (r < 0) reply_err(-r);
        else       reply_ok (0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_READDIR: {
        file_t *f = vfs_fd_get(&fdtab, (int)q->fd);
        if (!f) { reply_err(EBADF); break; }
        if (f->vn->type != VFS_TYPE_DIR || !f->vn->ops || !f->vn->ops->readdir) {
            reply_err(ENOTDIR); break;
        }
        char name[VFS_RPC_DATA_MAX];
        int r = f->vn->ops->readdir(f->vn, (u32)q->arg0, name,
                                    sizeof(name));
        if (r < 0) reply_err(-r);
        else       reply_ok (0, 0, name, (u32)strlen(name) + 1);
        break;
    }
    case VFS_OP_SHUTDOWN:
        reply_ok(0, 0, (void *)0, 0);
        _exit(0);
    default:
        reply_err(ENOSYS);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    vfs_init();
    vnode_t *root = ramfs_create_root();
    if (!root) { printf("exy-vfs: cannot create ramfs root\n"); return 1; }
    vfs_mount_root(root);
    vfs_fd_init(&fdtab);

    exyde_handle_t ch_req = exyde_get_bootstrap();
    if (ch_req == EXYDE_HANDLE_INVALID) {
        printf("exy-vfs: no bootstrap channel\n");
        return 1;
    }

    static u8 attach_buf[VFS_RPC_MSG_SIZE];
    int a = exyde_ipc_recv_cap(ch_req, attach_buf, VFS_RPC_MSG_SIZE,
                               &g_resp);
    if (a != (int)VFS_RPC_MSG_SIZE || g_resp == EXYDE_HANDLE_INVALID) {
        printf("exy-vfs: attach failed (n=%d, resp=0x%x)\n",
               a, (unsigned)g_resp);
        return 1;
    }
    struct vfs_req *aq = (struct vfs_req *)attach_buf;
    if (aq->op != VFS_OP_ATTACH) {
        printf("exy-vfs: bad attach op=%u\n", (unsigned)aq->op);
        return 1;
    }
    printf("exy-vfs: ready\n");

    static u8 req_buf[VFS_RPC_MSG_SIZE];
    for (;;) {
        int n = exyde_ipc_recv(ch_req, req_buf, VFS_RPC_MSG_SIZE);
        if (n != (int)VFS_RPC_MSG_SIZE) {
            printf("exy-vfs: short recv (n=%d)\n", n);
            return 1;
        }
        struct vfs_req *q = (struct vfs_req *)req_buf;

        int need_path = (q->op == VFS_OP_OPEN ||
                         q->op == VFS_OP_MKDIR ||
                         q->op == VFS_OP_UNLINK ||
                         q->op == VFS_OP_RMDIR);
        if (need_path && check_path(q) < 0) {
            reply_err(EINVAL);
            continue;
        }
        handle(q);
    }
}
