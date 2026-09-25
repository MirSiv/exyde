/* console -- userspace console server, Phase 11.5.5.
 *
 * Spawned by init with a bootstrap channel (ch_req).  First message
 * must be CONSOLE_OP_ATTACH carrying the reply channel (ch_resp) as
 * a capability.
 *
 * Writes go through the kernel console via the transitional
 * SYS_WRITE (fd 1).  A future Phase 13 will drive the hardware
 * directly once userspace has MMIO/PIO capabilities. */

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
    case CONSOLE_OP_READ:
        /* No keyboard driver yet.  Return 0 (EOF) immediately. */
        reply(0);
        break;
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
