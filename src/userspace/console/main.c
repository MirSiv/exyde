/* console -- userspace console server, Phase 11.5.5.
 *
 * Spawned by init with a bootstrap channel (ch_req).  First message
 * must be CONSOLE_OP_ATTACH carrying the reply channel (ch_resp) as
 * a capability.
 *
 * Writes go through the kernel console via SYS_EXY_PUTS (fd 1 in
 * this process falls back to it because we never call
 * console_client_init).  Reads poll the kernel console via fd 0
 * (SYS_EXY_GETS) with SYS_YIELD between attempts.  Phase 13 will
 * drive the hardware directly once userspace has PIO/MMIO
 * capabilities. */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <exyde/micro.h>
#include <exyde/console_rpc.h>

static exyde_handle_t g_resp = EXYDE_HANDLE_INVALID;
static uint8_t reply_buf[CONSOLE_RPC_MSG_SIZE];

/* Direct kernel write, bypassing libc's POSIX layer -- the console
 * server must not route its own output back through itself. */
static void kwrite(const void *buf, size_t len) {
    write(1, buf, len);
}

static void kputs(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    kwrite(s, n);
}

static void reply(int ret) {
    struct console_rsp *r = (struct console_rsp *)reply_buf;
    memset(r, 0, sizeof *r);
    r->ret = (int64_t)ret;
    exyde_ipc_send(g_resp, reply_buf, CONSOLE_RPC_MSG_SIZE);
}

static void reply_bytes(const void *buf, uint32_t len) {
    struct console_rsp *r = (struct console_rsp *)reply_buf;
    memset(r, 0, sizeof *r);
    r->ret = (int64_t)len;
    r->len = len;
    if (len) memcpy(r->data, buf, len);
    exyde_ipc_send(g_resp, reply_buf, CONSOLE_RPC_MSG_SIZE);
}

static void handle(const struct console_req *q) {
    switch (q->op) {
    case CONSOLE_OP_WRITE:
        if (q->len == 0 || q->len > CONSOLE_RPC_DATA_MAX) {
            reply(-(int)EINVAL);
            break;
        }
        kwrite(q->data, q->len);
        reply((int)q->len);
        break;

    case CONSOLE_OP_READ: {
        uint32_t want = q->len;
        if (want > CONSOLE_RPC_DATA_MAX) want = CONSOLE_RPC_DATA_MAX;
        if (want == 0) { reply(0); break; }

        /* read(0) here falls back to SYS_EXY_GETS because this
         * process never calls console_client_init.  It is
         * non-blocking, so we poll with a yield until at least one
         * byte is available.  Blocking here is deliberate: the
         * console server is the single component responsible for
         * waiting on hardware, so clients get POSIX-style blocking
         * read(). */
        static uint8_t tmp[CONSOLE_RPC_DATA_MAX];
        size_t got = 0;
        for (;;) {
            ssize_t r = read(0, tmp, want);
            if (r > 0) { got = (size_t)r; break; }
            if (r == 0) { reply(0); return; }  /* EOF */
            if (errno != EAGAIN) { reply(-(int)errno); return; }
            exyde_yield();
        }
        reply_bytes(tmp, (uint32_t)got);
        break;
    }

    case CONSOLE_OP_SHUTDOWN:
        reply(0);
        _exit(0);

    default:
        reply(-(int)ENOSYS);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    exyde_handle_t ch_req = exyde_get_bootstrap();
    if (ch_req == EXYDE_HANDLE_INVALID) {
        kputs("console: no bootstrap channel\n");
        return 1;
    }

    static uint8_t attach_buf[CONSOLE_RPC_MSG_SIZE];
    int a = exyde_ipc_recv_cap(ch_req, attach_buf, CONSOLE_RPC_MSG_SIZE,
                               &g_resp);
    if (a != (int)CONSOLE_RPC_MSG_SIZE || g_resp == EXYDE_HANDLE_INVALID) {
        kputs("console: attach failed\n");
        return 1;
    }
    struct console_req *aq = (struct console_req *)attach_buf;
    if (aq->op != CONSOLE_OP_ATTACH) {
        kputs("console: bad attach op\n");
        return 1;
    }

    kputs("console: ready\n");

    static uint8_t req_buf[CONSOLE_RPC_MSG_SIZE];
    for (;;) {
        int n = exyde_ipc_recv(ch_req, req_buf, CONSOLE_RPC_MSG_SIZE);
        if (n != (int)CONSOLE_RPC_MSG_SIZE) {
            kputs("console: short recv\n");
            return 1;
        }
        handle((const struct console_req *)req_buf);
    }
}
