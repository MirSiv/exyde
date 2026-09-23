#include <exyde/syscall.h>
#include <exyde/errno.h>
#include <exyde/handle.h>
#include <exyde/console.h>
#include <exyde/thread.h>
#include <exyde/sched.h>
#include <exyde/process.h>
#include <exyde/fd.h>
#include <exyde/vfs.h>
#include <exyde/panic.h>

/* ---- helpers -------------------------------------------------------- */

static process_t *current_process(void) {
    thread_t *t = thread_current();
    if (!t || !t->process) return (process_t *)0;
    return (process_t *)t->process;
}

/* ---- old ABI (unchanged) -------------------------------------------- */

static sysret_t sys_ping(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    return (sysret_t)(0xDEADBEEFCAFE0000ULL ^ a0);
}

static sysret_t sys_exit(u64 code, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)code; (void)a1; (void)a2; (void)a3; (void)a4;
    thread_exit();
}

static sysret_t sys_handle_create(u64 kind, u64 rights, u64 object,
                                  u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    handle_t h = handle_create(&p->handles,
                               (u32)kind, (u32)rights,
                               (void *)(uintptr_t)object);
    if (h == HANDLE_INVALID) return SYSRET_ERR(EMFILE);
    return (sysret_t)h;
}

static sysret_t sys_handle_close(u64 h, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (!handle_close(&p->handles, (handle_t)h)) return SYSRET_ERR(EBADF);
    return 0;
}

static sysret_t sys_handle_query(u64 h, u64 required, u64 a2, u64 a3, u64 a4) {
    (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (!handle_lookup(&p->handles, (handle_t)h, (u32)required,
                       (void **)0, (u32 *)0)) {
        return SYSRET_ERR(EPERM);
    }
    return 1;
}

/* ---- new ABI (Phase 10) --------------------------------------------- */

static sysret_t sys_write(u64 fd, u64 buf_u, u64 count,
                          u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    if (count == 0) return 0;
    if (buf_u < USER_VA_BASE) return SYSRET_ERR(EFAULT);

    i64 r = vfs_write(f, (const void *)(uintptr_t)buf_u, (size_t)count);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return (sysret_t)r;
}

static sysret_t sys_read(u64 fd, u64 buf_u, u64 count,
                         u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);

    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    if (count == 0) return 0;
    if (buf_u < USER_VA_BASE) return SYSRET_ERR(EFAULT);

    i64 r = vfs_read(f, (void *)(uintptr_t)buf_u, (size_t)count);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return (sysret_t)r;
}

static sysret_t sys_open(u64 path_u, u64 flags, u64 mode,
                         u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    if (path_u < USER_VA_BASE) return SYSRET_ERR(EFAULT);

    const char *path = (const char *)(uintptr_t)path_u;
    file_t *f = (file_t *)0;
    int r = vfs_open(path, (u32)flags, (u32)mode, &f);
    if (r < 0) return SYSRET_ERR((u64)-r);

    int fd = fd_alloc(&p->fds, f);
    if (fd < 0) {
        vfs_close(f);
        return SYSRET_ERR(EMFILE);
    }
    return (sysret_t)fd;
}

static sysret_t sys_close(u64 fd, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    int r = fd_close(&p->fds, (int)fd);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return 0;
}

static sysret_t sys_lseek(u64 fd, u64 off, u64 whence,
                          u64 a3, u64 a4) {
    (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    file_t *f = fd_get(&p->fds, (int)fd);
    if (!f) return SYSRET_ERR(EBADF);
    i64 r = vfs_seek(f, (i64)off, (int)whence);
    if (r < 0) return SYSRET_ERR((u64)-r);
    return (sysret_t)r;
}

static sysret_t sys_getpid(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    process_t *p = current_process();
    if (!p) return SYSRET_ERR(EPERM);
    return (sysret_t)p->pid;
}

/* ---- dispatch ------------------------------------------------------- */

typedef sysret_t (*syscall_fn_t)(u64, u64, u64, u64, u64);

static const syscall_fn_t syscall_table[SYSCALL_MAX] = {
    [SYS_PING]          = sys_ping,
    [SYS_WRITE]         = sys_write,
    [SYS_EXIT]          = sys_exit,
    [SYS_HANDLE_CREATE] = sys_handle_create,
    [SYS_HANDLE_CLOSE]  = sys_handle_close,
    [SYS_HANDLE_QUERY]  = sys_handle_query,
    [SYS_READ]          = sys_read,
    [SYS_OPEN]          = sys_open,
    [SYS_CLOSE]         = sys_close,
    [SYS_LSEEK]         = sys_lseek,
    [SYS_GETPID]        = sys_getpid,
};

sysret_t syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    if (nr >= SYSCALL_MAX || syscall_table[nr] == (syscall_fn_t)0) {
        return SYSRET_ERR(ENOSYS);
    }
    return syscall_table[nr](a0, a1, a2, a3, a4);
}
