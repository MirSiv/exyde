/* init.elf -- service manager.
 *
 * Phase 11.5.2.  Runs as the first user process (spawned by the
 * kernel bootstrap path in kmain.c::userspace_selftest).  Its job:
 *
 *   1. Spawn "test" -- the Phase 11 regression suite -- with a
 *      bootstrap channel, wait for it, and abort with a non-zero
 *      exit code if it failed.
 *   2. Spawn "echo" -- the first real service -- with another
 *      bootstrap channel, exercise an IPC round-trip, ask it to
 *      quit, and wait for it.
 *   3. Exit 0.
 *
 * This is deliberately thin.  Everything that was init's test suite
 * moved to test.elf. */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <exyde/micro.h>
#include <exyde/console_client.h>
#include <exyde/console_rpc.h>
#include <exyde/vfs_client.h>
#include <exyde/vfs_rpc.h>
#include <stdint.h>

static exyde_handle_t g_console_child = EXYDE_HANDLE_INVALID;

static int start_console(void) {
    exyde_handle_t ch_req  = exyde_ipc_create(CONSOLE_RPC_MSG_SIZE, 4);
    exyde_handle_t ch_resp = exyde_ipc_create(CONSOLE_RPC_MSG_SIZE, 4);
    if (ch_req == EXYDE_HANDLE_INVALID || ch_resp == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL console channel errno=%d\n", errno);
        return 1;
    }

    exyde_handle_t child = exyde_spawn("console", ch_req);
    if (child == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL spawn console errno=%d\n", errno);
        exyde_handle_close(ch_req);
        exyde_handle_close(ch_resp);
        return 1;
    }
    g_console_child = child;

    static uint8_t attach_buf[CONSOLE_RPC_MSG_SIZE];
    memset(attach_buf, 0, sizeof attach_buf);
    struct console_req *aq = (struct console_req *)attach_buf;
    aq->op = CONSOLE_OP_ATTACH;
    if (exyde_ipc_send_cap(ch_req, attach_buf, CONSOLE_RPC_MSG_SIZE,
                           ch_resp, EXYDE_IPC_CAP_DUPLICATE) != 0) {
        printf("init: FAIL console attach errno=%d\n", errno);
        exyde_handle_close(ch_req);
        exyde_handle_close(ch_resp);
        return 1;
    }

    if (console_client_init(ch_req, ch_resp) != 0) {
        printf("init: FAIL console_client_init errno=%d\n", errno);
        exyde_handle_close(ch_req);
        exyde_handle_close(ch_resp);
        return 1;
    }

    /* The console server keeps running until we tell it to shut
     * down.  We do not wait for it here -- it is a service, not a
     * test.  The child handle stays open for the process lifetime. */
    printf("init: console up\n");
    return 0;
}

static int run_regression_suite(void) {
    exyde_handle_t ch = exyde_ipc_create(16, 2);
    if (ch == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL cannot create bootstrap channel\n");
        return 1;
    }

    exyde_handle_t child = exyde_spawn("test", ch);
    if (child == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL spawn test errno=%d\n", errno);
        exyde_handle_close(ch);
        return 1;
    }

    int code = exyde_wait(child);
    exyde_handle_close(ch);
    exyde_handle_close(child);

    if (code != 0) {
        printf("init: FAIL test.elf exit code %d\n", code);
        return 1;
    }
    printf("init: test suite OK\n");
    return 0;
}

static int run_echo_service(void) {
    exyde_handle_t ch = exyde_ipc_create(16, 2);
    if (ch == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL cannot create echo channel\n");
        return 1;
    }

    exyde_handle_t child = exyde_spawn("echo", ch);
    if (child == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL spawn echo errno=%d\n", errno);
        exyde_handle_close(ch);
        return 1;
    }

    /* Round-trip: send "ping", expect "ping" back.  The channel is a
     * single FIFO queue, so at most one message is in flight at a
     * time and capacity 2 is more than enough. */
    char tx[16] = "ping";
    char rx[16];
    if (exyde_ipc_send(ch, tx, 16) != 0) {
        printf("init: FAIL echo send errno=%d\n", errno);
        goto fail;
    }
    if (exyde_ipc_recv(ch, rx, 16) != 16) {
        printf("init: FAIL echo recv errno=%d\n", errno);
        goto fail;
    }
    if (strcmp(rx, "ping") != 0) {
        printf("init: FAIL echo mismatch [%s]\n", rx);
        goto fail;
    }

    /* Ask echo to quit. */
    char q[16] = "quit";
    if (exyde_ipc_send(ch, q, 16) != 0) {
        printf("init: FAIL echo quit send errno=%d\n", errno);
        goto fail;
    }

    int code = exyde_wait(child);
    exyde_handle_close(ch);
    exyde_handle_close(child);

    if (code != 0) {
        printf("init: FAIL echo.elf exit code %d\n", code);
        return 1;
    }
    printf("init: echo service OK\n");
    return 0;

fail:
    exyde_handle_close(ch);
    exyde_handle_close(child);
    return 1;
}

static int run_vfs_test(void) {
    exyde_handle_t ch_req  = exyde_ipc_create(VFS_RPC_MSG_SIZE, 4);
    exyde_handle_t ch_resp = exyde_ipc_create(VFS_RPC_MSG_SIZE, 4);
    if (ch_req == EXYDE_HANDLE_INVALID || ch_resp == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL vfs channel errno=%d\n", errno);
        return 1;
    }

    exyde_handle_t child = exyde_spawn("exy-vfs", ch_req);
    if (child == EXYDE_HANDLE_INVALID) {
        printf("init: FAIL spawn exy-vfs errno=%d\n", errno);
        exyde_handle_close(ch_req);
        exyde_handle_close(ch_resp);
        return 1;
    }

    /* ATTACH: hand ch_resp to the server. */
    {
        static uint8_t attach_buf[VFS_RPC_MSG_SIZE];
        memset(attach_buf, 0, sizeof attach_buf);
        struct vfs_req *aq = (struct vfs_req *)attach_buf;
        aq->op = VFS_OP_ATTACH;
        if (exyde_ipc_send_cap(ch_req, attach_buf, VFS_RPC_MSG_SIZE,
                               ch_resp, EXYDE_IPC_CAP_DUPLICATE) != 0) {
            printf("init: FAIL attach errno=%d\n", errno);
            goto fail;
        }
    }

    if (vfs_client_init(ch_req, ch_resp) != 0) {
        printf("init: FAIL vfs_client_init errno=%d\n", errno);
        goto fail;
    }

    if (vfs_client_mkdir("/tmp", 0755) != 0) {
        printf("init: FAIL mkdir errno=%d\n", errno);
        goto fail;
    }

    /* POSIX path: open/write/read/close.  Internally these go
     * through libvfs and the local fd table. */
    int fd = open("/tmp/hello", 0x0102 /* O_CREAT|O_WRONLY */, 0644);
    if (fd < 0) {
        printf("init: FAIL open(w) errno=%d\n", errno);
        goto fail;
    }
    if (fd < 3) {
        printf("init: FAIL open returned reserved fd %d\n", fd);
        close(fd);
        goto fail;
    }

    const char *msg = "hello exyde vfs";
    ssize_t w = write(fd, msg, 16);
    if (w != 16) {
        printf("init: FAIL write ret=%ld errno=%d\n", (long)w, errno);
        close(fd);
        goto fail;
    }
    if (close(fd) != 0) {
        printf("init: FAIL close(w) errno=%d\n", errno);
        goto fail;
    }

    fd = open("/tmp/hello", 0x0001 /* O_RDONLY */);
    if (fd < 0) {
        printf("init: FAIL open(r) errno=%d\n", errno);
        goto fail;
    }
    char buf[32];
    ssize_t n = read(fd, buf, 16);
    if (n != 16) {
        printf("init: FAIL read ret=%ld errno=%d\n", (long)n, errno);
        close(fd);
        goto fail;
    }
    buf[16] = 0;
    if (strcmp(buf, "hello exyde vfs") != 0) {
        printf("init: FAIL content [%s]\n", buf);
        close(fd);
        goto fail;
    }
    if (close(fd) != 0) {
        printf("init: FAIL close(r) errno=%d\n", errno);
        goto fail;
    }

    fd = vfs_client_open("/tmp", 0x0001, 0);
    if (fd < 0) {
        printf("init: FAIL open(dir) errno=%d\n", errno);
        goto fail;
    }
    char name[64];
    if (vfs_client_readdir(fd, 0, name, sizeof name) != 0) {
        printf("init: FAIL readdir errno=%d\n", errno);
        vfs_client_close(fd);
        goto fail;
    }
    if (strcmp(name, "hello") != 0) {
        printf("init: FAIL readdir name [%s]\n", name);
        vfs_client_close(fd);
        goto fail;
    }
    errno = 0;
    if (vfs_client_readdir(fd, 1, name, sizeof name) != -1 || errno != 2) {
        printf("init: FAIL readdir past-end errno=%d\n", errno);
        vfs_client_close(fd);
        goto fail;
    }
    vfs_client_close(fd);

    if (vfs_client_unlink("/tmp/hello") != 0) {
        printf("init: FAIL unlink errno=%d\n", errno);
        goto fail;
    }
    if (vfs_client_rmdir("/tmp") != 0) {
        printf("init: FAIL rmdir errno=%d\n", errno);
        goto fail;
    }

    vfs_client_shutdown();
    int code = exyde_wait(child);
    exyde_handle_close(ch_req);
    exyde_handle_close(ch_resp);
    exyde_handle_close(child);

    if (code != 0) {
        printf("init: FAIL exy-vfs exit code %d\n", code);
        return 1;
    }
    printf("init: vfs service OK\n");
    return 0;

fail:
    exyde_handle_close(ch_req);
    exyde_handle_close(ch_resp);
    exyde_handle_close(child);
    return 1;
}
int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    printf("init: service manager starting\n");

    if (start_console()        != 0) return 1;
    if (run_regression_suite() != 0) return 1;
    if (run_echo_service()     != 0) return 1;
    if (run_vfs_test()         != 0) return 1;

    printf("init: all services OK\n");

    /* Shut down the console server last, and wait for it to actually
     * exit, so that when init's process teardown closes the request
     * channel nothing is still blocked on it.  A channel destroyed
     * with waiters triggers a defensive panic in channel_destroy;
     * keeping the shutdown orderly avoids that path entirely. */
    if (g_console_child != EXYDE_HANDLE_INVALID) {
        console_client_shutdown();
        (void)exyde_wait(g_console_child);
        (void)exyde_handle_close(g_console_child);
    }
    return 0;
}
