/* exy-vfs -- userspace VFS server, Phase 11.5.3.
 * Multiplexed onto multiple clients in Phase 12.3a.
 *
 * Bootstrap channel (ch_req) is shared.  Each client sends an
 * ATTACH on ch_req carrying its private reply channel (ch_resp) as
 * a capability.  The server assigns a client_id, stores
 * {id, ch_resp, fdtab}, and replies on ch_resp with the id in aux.
 * Subsequent requests carry that client_id; the server routes the
 * reply to the right ch_resp and uses that client's fd table.
 *
 * SHUTDOWN detaches one client.  The server exits when the last
 * client has detached. */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <exyde/micro.h>
#include <exyde/vfs_rpc.h>
#include "vfs_internal.h"

#define VFS_MAX_CLIENTS 8

struct vfs_client {
    int              in_use;
    u32              id;
    exyde_handle_t   ch_resp;
    vfs_fd_table_t   fdtab;
};

static struct vfs_client g_clients[VFS_MAX_CLIENTS];
static u32                g_next_client_id = 1;

static u8 reply_buf[VFS_RPC_MSG_SIZE];

static struct vfs_client *client_find(u32 id) {
    if (id == 0) return (struct vfs_client *)0;
    for (int i = 0; i < VFS_MAX_CLIENTS; ++i) {
        if (g_clients[i].in_use && g_clients[i].id == id)
            return &g_clients[i];
    }
    return (struct vfs_client *)0;
}

static int clients_any_alive(void) {
    for (int i = 0; i < VFS_MAX_CLIENTS; ++i)
        if (g_clients[i].in_use) return 1;
    return 0;
}

static struct vfs_client *client_attach(exyde_handle_t ch_resp) {
    for (int i = 0; i < VFS_MAX_CLIENTS; ++i) {
        if (!g_clients[i].in_use) {
            g_clients[i].in_use = 1;
            g_clients[i].id      = g_next_client_id++;
            g_clients[i].ch_resp = ch_resp;
            vfs_fd_init(&g_clients[i].fdtab);
            return &g_clients[i];
        }
    }
    return (struct vfs_client *)0;
}

static void client_detach(struct vfs_client *c) {
    for (int i = 0; i < VFS_FD_MAX; ++i) {
        if (c->fdtab.files[i]) {
            vfs_close(c->fdtab.files[i]);
            c->fdtab.files[i] = (file_t *)0;
        }
    }
    exyde_handle_close(c->ch_resp);
    c->ch_resp = EXYDE_HANDLE_INVALID;
    c->in_use  = 0;
}

static void reply_ok(struct vfs_client *c, i64 ret, u64 aux,
                     const void *data, u32 data_len) {
    struct vfs_rsp *r = (struct vfs_rsp *)reply_buf;
    memset(r, 0, sizeof *r);
    r->ret      = ret;
    r->aux      = aux;
    r->data_len = data_len;
    if (data_len && data) memcpy(r->data, data, data_len);
    exyde_ipc_send(c->ch_resp, reply_buf, VFS_RPC_MSG_SIZE);
}

static void reply_err(struct vfs_client *c, int err) {
    struct vfs_rsp *r = (struct vfs_rsp *)reply_buf;
    memset(r, 0, sizeof *r);
    r->ret = -(i64)err;
    exyde_ipc_send(c->ch_resp, reply_buf, VFS_RPC_MSG_SIZE);
}

static int check_path(const struct vfs_req *q) {
    for (size_t i = 0; i < VFS_RPC_PATH_MAX; ++i) {
        if (q->path[i] == '\0') return 0;
    }
    return -EINVAL;
}

static int op_open(struct vfs_client *c, const struct vfs_req *q) {
    file_t *f = (file_t *)0;
    int r = vfs_open(q->path, (u32)q->arg0, (u32)q->arg1, &f);
    if (r < 0) return r;
    int fd = vfs_fd_alloc(&c->fdtab, f);
    if (fd < 0) { vfs_close(f); return fd; }
    return fd;
}

static void handle(struct vfs_client *c, const struct vfs_req *q) {
    switch (q->op) {
    case VFS_OP_OPEN: {
        int r = op_open(c, q);
        if (r < 0) reply_err(c, -r);
        else       reply_ok (c, 0, (u64)r, (void *)0, 0);
        break;
    }
    case VFS_OP_CLOSE: {
        int r = vfs_fd_close(&c->fdtab, (int)q->fd);
        if (r < 0) reply_err(c, -r);
        else       reply_ok (c, 0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_READ: {
        file_t *f = vfs_fd_get(&c->fdtab, (int)q->fd);
        if (!f) { reply_err(c, EBADF); break; }
        if (q->arg0 == 0 || q->arg0 > VFS_RPC_DATA_MAX) {
            reply_err(c, EINVAL); break;
        }
        u8 buf[VFS_RPC_DATA_MAX];
        i64 n = vfs_read(f, buf, (size_t)q->arg0);
        if (n < 0) reply_err(c, (int)-n);
        else       reply_ok (c, n, 0, buf, (u32)n);
        break;
    }
    case VFS_OP_WRITE: {
        file_t *f = vfs_fd_get(&c->fdtab, (int)q->fd);
        if (!f) { reply_err(c, EBADF); break; }
        if (q->data_len == 0 || q->data_len > VFS_RPC_DATA_MAX) {
            reply_err(c, EINVAL); break;
        }
        i64 n = vfs_write(f, q->data, q->data_len);
        if (n < 0) reply_err(c, (int)-n);
        else       reply_ok (c, n, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_SEEK: {
        file_t *f = vfs_fd_get(&c->fdtab, (int)q->fd);
        if (!f) { reply_err(c, EBADF); break; }
        i64 r = vfs_seek(f, (i64)q->arg0, (int)q->arg1);
        if (r < 0) reply_err(c, (int)-r);
        else       reply_ok (c, r, (u64)r, (void *)0, 0);
        break;
    }
    case VFS_OP_MKDIR: {
        int r = vfs_mkdir(q->path, (u32)q->arg0);
        if (r < 0) reply_err(c, -r);
        else       reply_ok (c, 0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_UNLINK: {
        int r = vfs_unlink(q->path);
        if (r < 0) reply_err(c, -r);
        else       reply_ok (c, 0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_RMDIR: {
        int r = vfs_rmdir(q->path);
        if (r < 0) reply_err(c, -r);
        else       reply_ok (c, 0, 0, (void *)0, 0);
        break;
    }
    case VFS_OP_READDIR: {
        file_t *f = vfs_fd_get(&c->fdtab, (int)q->fd);
        if (!f) { reply_err(c, EBADF); break; }
        if (f->vn->type != VFS_TYPE_DIR || !f->vn->ops || !f->vn->ops->readdir) {
            reply_err(c, ENOTDIR); break;
        }
        char name[VFS_RPC_DATA_MAX];
        int r = f->vn->ops->readdir(f->vn, (u32)q->arg0, name,
                                    sizeof(name));
        if (r < 0) reply_err(c, -r);
        else       reply_ok (c, 0, 0, name, (u32)strlen(name) + 1);
        break;
    }
    default:
        reply_err(c, ENOSYS);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    vfs_init();
    vnode_t *root = ramfs_create_root();
    if (!root) { printf("exy-vfs: cannot create ramfs root\n"); return 1; }
    vfs_mount_root(root);

    exyde_handle_t ch_req = exyde_get_bootstrap();
    if (ch_req == EXYDE_HANDLE_INVALID) {
        printf("exy-vfs: no bootstrap channel\n");
        return 1;
    }

    printf("exy-vfs: ready\n");

    static u8 req_buf[VFS_RPC_MSG_SIZE];
    for (;;) {
        exyde_handle_t new_resp = EXYDE_HANDLE_INVALID;
        int n = exyde_ipc_recv_cap(ch_req, req_buf, VFS_RPC_MSG_SIZE,
                                   &new_resp);
        if (n != (int)VFS_RPC_MSG_SIZE) {
            printf("exy-vfs: short recv (n=%d)\n", n);
            return 1;
        }
        struct vfs_req *q = (struct vfs_req *)req_buf;

        if (q->op == VFS_OP_ATTACH) {
            if (new_resp == EXYDE_HANDLE_INVALID) {
                /* Bad ATTACH: nothing to reply on. */
                continue;
            }
            struct vfs_client *c = client_attach(new_resp);
            if (!c) {
                struct vfs_rsp *re = (struct vfs_rsp *)reply_buf;
                memset(re, 0, sizeof *re);
                re->ret = -(i64)EMFILE;
                exyde_ipc_send(new_resp, reply_buf, VFS_RPC_MSG_SIZE);
                exyde_handle_close(new_resp);
                continue;
            }
            {
                struct vfs_rsp *r = (struct vfs_rsp *)reply_buf;
                memset(r, 0, sizeof *r);
                r->ret = 0;
                r->aux = (u64)c->id;
                exyde_ipc_send(c->ch_resp, reply_buf, VFS_RPC_MSG_SIZE);
            }
            continue;
        }

        /* Stray cap on a non-ATTACH message is a protocol error. */
        if (new_resp != EXYDE_HANDLE_INVALID) {
            exyde_handle_close(new_resp);
        }

        struct vfs_client *c = client_find(q->client_id);
        if (!c) continue;

        if (q->op == VFS_OP_SHUTDOWN) {
            reply_ok(c, 0, 0, (void *)0, 0);
            client_detach(c);
            if (!clients_any_alive()) _exit(0);
            continue;
        }

        int need_path = (q->op == VFS_OP_OPEN ||
                         q->op == VFS_OP_MKDIR ||
                         q->op == VFS_OP_UNLINK ||
                         q->op == VFS_OP_RMDIR);
        if (need_path && check_path(q) < 0) {
            reply_err(c, EINVAL);
            continue;
        }
        handle(c, q);
    }
}
