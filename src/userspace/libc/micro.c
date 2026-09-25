#include <exyde/micro.h>
#include <exyde/abi.h>
#include <errno.h>
#include <stdint.h>
#include "internal/syscall.h"

/* Translate a raw syscall return (>= 0 success / -errno error) into
 * the POSIX convention (-1 with errno set). */
static long chk(long r) {
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

/* ---- IPC -------------------------------------------------------- */

exyde_handle_t exyde_ipc_create(unsigned int msg_size,
                                unsigned int capacity) {
    long r = __exyde_syscall(SYS_IPC_CREATE, msg_size, capacity, 0, 0, 0);
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return EXYDE_HANDLE_INVALID;
    }
    return (exyde_handle_t)r;
}

int exyde_ipc_send(exyde_handle_t h, const void *buf, unsigned int len) {
    long r = __exyde_syscall(SYS_IPC_SEND, h, (long)(uintptr_t)buf, len, 0, 0);
    return (int)chk(r);
}

int exyde_ipc_try_send(exyde_handle_t h, const void *buf, unsigned int len) {
    long r = __exyde_syscall(SYS_IPC_TRY_SEND, h, (long)(uintptr_t)buf, len, 0, 0);
    return (int)chk(r);
}

int exyde_ipc_recv(exyde_handle_t h, void *buf, unsigned int max_len) {
    long r = __exyde_syscall(SYS_IPC_RECV, h, (long)(uintptr_t)buf, max_len, 0, 0);
    return (int)chk(r);
}

int exyde_ipc_try_recv(exyde_handle_t h, void *buf, unsigned int max_len) {
    long r = __exyde_syscall(SYS_IPC_TRY_RECV, h, (long)(uintptr_t)buf, max_len, 0, 0);
    return (int)chk(r);
}

/* ---- IPC with capability transfer ------------------------------- */

int exyde_ipc_send_cap(exyde_handle_t ch,
                       const void *buf, unsigned int len,
                       exyde_handle_t cap, unsigned int action) {
    long r = __exyde_syscall(SYS_IPC_SEND_CAP, ch,
                             (long)(uintptr_t)buf, len, cap, action);
    return (int)chk(r);
}

int exyde_ipc_recv_cap(exyde_handle_t ch,
                       void *buf, unsigned int max_len,
                       exyde_handle_t *out_cap) {
    long r = __exyde_syscall(SYS_IPC_RECV_CAP, ch,
                             (long)(uintptr_t)buf, max_len,
                             (long)(uintptr_t)out_cap, 0);
    return (int)chk(r);
}

int exyde_ipc_try_recv_cap(exyde_handle_t ch,
                           void *buf, unsigned int max_len,
                           exyde_handle_t *out_cap) {
    long r = __exyde_syscall(SYS_IPC_TRY_RECV_CAP, ch,
                             (long)(uintptr_t)buf, max_len,
                             (long)(uintptr_t)out_cap, 0);
    return (int)chk(r);
}

int exyde_handle_close(exyde_handle_t h) {
    long r = __exyde_syscall(SYS_HANDLE_CLOSE, h, 0, 0, 0, 0);
    return (int)chk(r);
}

/* ---- process management ----------------------------------------- */

exyde_handle_t exyde_spawn(const char *name, exyde_handle_t cap) {
    long r = __exyde_syscall(SYS_SPAWN,
                             (long)(uintptr_t)name,
                             0,   /* argv (unused, 11.5.3) */
                             0,   /* argc must be 0 for now */
                             cap, 0);
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return EXYDE_HANDLE_INVALID;
    }
    return (exyde_handle_t)r;
}

int exyde_wait(exyde_handle_t proc) {
    long r = __exyde_syscall(SYS_WAIT, proc, 0, 0, 0, 0);
    /* exit codes are i32 and may be negative on purpose; only errno-
     * style -1..-4095 are treated as syscall failures.  Since the
     * syscall returns the code directly, we cannot distinguish a
     * "genuine -1 exit code" from an error without a separate
     * out-parameter -- acceptable for 11.5.2. */
    return (int)chk(r);
}

exyde_handle_t exyde_get_bootstrap(void) {
    long r = __exyde_syscall(SYS_GET_BOOTSTRAP, 0, 0, 0, 0, 0);
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return EXYDE_HANDLE_INVALID;
    }
    return (exyde_handle_t)r;
}

/* ---- memory ----------------------------------------------------- */

void *exyde_map(void *hint, unsigned int npages, unsigned int flags) {
    long r = __exyde_syscall(SYS_MAP, (long)(uintptr_t)hint, npages, flags, 0, 0);
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return (void *)0;
    }
    return (void *)(uintptr_t)r;
}

int exyde_unmap(void *vaddr, unsigned int npages) {
    long r = __exyde_syscall(SYS_UNMAP, (long)(uintptr_t)vaddr, npages, 0, 0, 0);
    return (int)chk(r);
}

int exyde_yield(void) {
    long r = __exyde_syscall(SYS_YIELD, 0, 0, 0, 0, 0);
    return (int)chk(r);
}
