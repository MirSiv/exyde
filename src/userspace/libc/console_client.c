#include <exyde/console_client.h>
#include <exyde/console_rpc.h>
#include <string.h>
#include <errno.h>

static exyde_handle_t rpc_req  = EXYDE_HANDLE_INVALID;
static exyde_handle_t rpc_resp = EXYDE_HANDLE_INVALID;

int console_client_init(exyde_handle_t ch_req, exyde_handle_t ch_resp) {
    if (ch_req  == EXYDE_HANDLE_INVALID ||
        ch_resp == EXYDE_HANDLE_INVALID) {
        errno = EINVAL;
        return -1;
    }
    rpc_req  = ch_req;
    rpc_resp = ch_resp;
    return 0;
}

int console_client_ready(void) {
    return (rpc_req != EXYDE_HANDLE_INVALID &&
            rpc_resp != EXYDE_HANDLE_INVALID) ? 1 : 0;
}

static int rpc(const struct console_req *q, struct console_rsp *r_out) {
    static uint8_t tx[CONSOLE_RPC_MSG_SIZE];
    static uint8_t rx[CONSOLE_RPC_MSG_SIZE];

    memset(tx, 0, sizeof tx);
    memcpy(tx, q, sizeof *q);

    if (exyde_ipc_send(rpc_req, tx, CONSOLE_RPC_MSG_SIZE) != 0)
        return -1;
    int n = exyde_ipc_recv(rpc_resp, rx, CONSOLE_RPC_MSG_SIZE);
    if (n != (int)CONSOLE_RPC_MSG_SIZE) { errno = EIO; return -1; }

    struct console_rsp *r = (struct console_rsp *)rx;
    if (r->ret < 0) { errno = (int)-r->ret; return -1; }
    if (r_out) *r_out = *r;
    return 0;
}

long console_client_write(const void *buf, size_t len) {
    if (!console_client_ready()) { errno = ENOSYS; return -1; }

    long total = 0;
    const uint8_t *p = (const uint8_t *)buf;
    while ((size_t)total < len) {
        size_t chunk = len - (size_t)total;
        if (chunk > CONSOLE_RPC_DATA_MAX) chunk = CONSOLE_RPC_DATA_MAX;

        struct console_req q;
        memset(&q, 0, sizeof q);
        q.op  = CONSOLE_OP_WRITE;
        q.len = (uint32_t)chunk;
        memcpy(q.data, p + total, chunk);

        struct console_rsp r;
        if (rpc(&q, &r) != 0) return -1;
        if (r.ret <= 0) { errno = EIO; return -1; }
        total += (long)r.ret;
    }
    return total;
}

long console_client_read(void *buf, size_t len) {
    if (!console_client_ready()) { errno = ENOSYS; return -1; }
    if (len == 0) return 0;

    size_t want = len;
    if (want > CONSOLE_RPC_DATA_MAX) want = CONSOLE_RPC_DATA_MAX;

    struct console_req q;
    memset(&q, 0, sizeof q);
    q.op  = CONSOLE_OP_READ;
    q.len = (uint32_t)want;

    struct console_rsp r;
    if (rpc(&q, &r) != 0) return -1;
    if (r.len > want) { errno = EIO; return -1; }
    memcpy(buf, r.data, r.len);
    return (long)r.len;
}

int console_client_shutdown(void) {
    if (!console_client_ready()) { errno = ENOSYS; return -1; }
    struct console_req q;
    memset(&q, 0, sizeof q);
    q.op = CONSOLE_OP_SHUTDOWN;
    return rpc(&q, (struct console_rsp *)0);
}
