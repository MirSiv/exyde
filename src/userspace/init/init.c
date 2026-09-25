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

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    printf("init: service manager starting\n");

    if (run_regression_suite() != 0) return 1;
    if (run_echo_service()     != 0) return 1;

    printf("init: all services OK\n");
    return 0;
}
