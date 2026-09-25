#include <exyde/vfs_client.h>
#include <exyde/vfs_rpc.h>
#include <string.h>
#include <errno.h>

/* Two-channel RPC: requests out on ch_req, replies in on ch_resp.
 * A client never reads from ch_req and the server never reads from
 * ch_resp, so neither side can pull its own message back. */
static exyde_handle_t rpc_req  = EXYDE_HANDLE_INVALID;
static exyde_handle_t rpc_resp = EXYDE_HANDLE_INVALID;

int vfs_client_init(exyde_handle_t ch_req, exyde_handle_t ch_resp) {
    if (ch_req  == EXYDE_HANDLE_INVALID ||
        ch_resp == EXYDE_HANDLE_INVALID) {
        errno = EINVAL; return -1;
    }
    rpc_req  = ch_req;
    rpc_resp = ch_resp;
    return 0;
}

static int ensure_init(void) {
    if (rpc_req == EXYDE_HANDLE_INVALID || rpc_resp == EXYDE_HANDLE_INVALID) {
        errno = ENOSYS;
        return -1;
    }
    return 0;
}

static int rpc(const struct vfs_req *q, struct vfs_rsp *r_out) {
    static uint8_t tx[VFS_RPC_MSG_SIZE];
    static uint8_t rx[VFS_RPC_MSG_SIZE];

    memset(tx, 0, sizeof tx);
    memcpy(tx, q, sizeof *q);

    if (exyde_ipc_send(rpc_req, tx, VFS_RPC_MSG_SIZE) != 0) {
        return -1;
    }
    int n = exyde_ipc_recv(rpc_resp, rx, VFS_RPC_MSG_SIZE);
    if (n != (int)VFS_RPC_MSG_SIZE) {
        errno = EIO;
        return -1;
    }
    struct vfs_rsp *r = (struct vfs_rsp *)rx;
    if (r->ret < 0) {
        errno = (int)-r->ret;
        return -1;
    }
    if (r_out) *r_out = *r;
    return 0;
}

static void set_path(struct vfs_req *q, const char *path) {
    size_t n = strlen(path);
    if (n >= VFS_RPC_PATH_MAX) n = VFS_RPC_PATH_MAX - 1;
    memcpy(q->path, path, n);
    q->path[n] = '\0';
}

int vfs_client_open(const char *path, uint32_t flags, uint32_t mode) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_OPEN; q.arg0 = flags; q.arg1 = mode;
    set_path(&q, path);
    struct vfs_rsp r;
    if (rpc(&q, &r) != 0) return -1;
    return (int)r.aux;
}

int vfs_client_close(int fd) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_CLOSE; q.fd = (uint32_t)fd;
    return rpc(&q, (struct vfs_rsp *)0);
}

long vfs_client_read(int fd, void *buf, size_t len) {
    if (ensure_init() != 0) return -1;

    if (len == 0 || len > VFS_RPC_DATA_MAX) { errno = EINVAL; return -1; }
    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_READ; q.fd = (uint32_t)fd; q.arg0 = len;
    struct vfs_rsp r;
    if (rpc(&q, &r) != 0) return -1;
    if (r.data_len > len) { errno = EIO; return -1; }
    memcpy(buf, r.data, r.data_len);
    return (long)r.data_len;
}

long vfs_client_write(int fd, const void *buf, size_t len) {
    if (ensure_init() != 0) return -1;

    if (len == 0 || len > VFS_RPC_DATA_MAX) { errno = EINVAL; return -1; }
    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_WRITE; q.fd = (uint32_t)fd; q.data_len = (uint32_t)len;
    memcpy(q.data, buf, len);
    struct vfs_rsp r;
    if (rpc(&q, &r) != 0) return -1;
    return (long)r.ret;
}

long vfs_client_seek(int fd, long off, int whence) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_SEEK; q.fd = (uint32_t)fd;
    q.arg0 = (uint64_t)off; q.arg1 = (uint64_t)whence;
    struct vfs_rsp r;
    if (rpc(&q, &r) != 0) return -1;
    return (long)r.aux;
}

int vfs_client_mkdir(const char *path, uint32_t mode) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_MKDIR; q.arg0 = mode; set_path(&q, path);
    return rpc(&q, (struct vfs_rsp *)0);
}

int vfs_client_unlink(const char *path) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_UNLINK; set_path(&q, path);
    return rpc(&q, (struct vfs_rsp *)0);
}

int vfs_client_rmdir(const char *path) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_RMDIR; set_path(&q, path);
    return rpc(&q, (struct vfs_rsp *)0);
}

int vfs_client_readdir(int fd, uint32_t index, char *name, size_t n) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_READDIR; q.fd = (uint32_t)fd; q.arg0 = index;
    struct vfs_rsp r;
    if (rpc(&q, &r) != 0) return -1;
    if (r.data_len == 0 || r.data_len > n) { errno = EIO; return -1; }
    memcpy(name, r.data, r.data_len);
    return 0;
}

int vfs_client_shutdown(void) {
    if (ensure_init() != 0) return -1;

    struct vfs_req q; memset(&q, 0, sizeof q);
    q.op = VFS_OP_SHUTDOWN;
    return rpc(&q, (struct vfs_rsp *)0);
}
