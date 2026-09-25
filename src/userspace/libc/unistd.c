#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <exyde/abi.h>
#include <exyde/vfs_client.h>
#include <exyde/console_client.h>
#include "internal/syscall.h"
#include "internal/vfs_fdtab.h"

static long posix_ret(long r) {
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

/* fd 0/1/2 are console.  Writes go to the console server if one is
 * wired up, otherwise through SYS_KPUTS (kernel fallback).  Reads of
 * fd 0 need the console server; there is no kernel input path any
 * more.  close/lseek on 0/1/2 are POSIX-style no-op / ESPIPE.
 *
 * fd >= 3 route through libvfs to exy-vfs, using the local fd table
 * to translate local fd -> server fd.  open() always goes through
 * libvfs; a caller that has not called vfs_client_init() gets ENOSYS
 * from libvfs. */

ssize_t read(int fd, void *buf, size_t count) {
    if (fd >= 0 && fd < 3) {
        /* Only stdin is readable.  If a console server is up, ask it;
         * otherwise there is no source of input in Phase 11.5.6. */
        if (fd == STDIN_FILENO && console_client_ready()) {
            return console_client_read(buf, count);
        }
        errno = EBADF;
        return -1;
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    return vfs_client_read((int)sf, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (fd >= 0 && fd < 3) {
        /* fd 1/2 are console.  Route to the console server if one
         * is up; otherwise fall back to the kernel console via
         * SYS_KPUTS.  fd 0 is read-only and handled by read(). */
        if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
            if (console_client_ready()) {
                return console_client_write(buf, count);
            }
            long r = __exyde_syscall(SYS_KPUTS, (long)buf, (long)count, 0, 0, 0);
            return (ssize_t)posix_ret(r);
        }
        errno = EBADF;
        return -1;
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    return vfs_client_write((int)sf, buf, count);
}

int open(const char *path, int flags, ...) {
    int sf = vfs_client_open(path, (uint32_t)flags, 0);
    if (sf < 0) return -1;
    return vfs_fdtab_alloc((uint32_t)sf);
}

int close(int fd) {
    if (fd >= 0 && fd < 3) {
        /* POSIX: closing stdin/stdout/stderr is a no-op success.
         * There is no kernel fd to release any more. */
        return 0;
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    if (vfs_client_close((int)sf) != 0) return -1;
    return vfs_fdtab_free(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    if (fd >= 0 && fd < 3) {
        /* stdin/stdout/stderr are character devices; not seekable. */
        errno = ESPIPE;
        return -1;
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    return vfs_client_seek((int)sf, offset, whence);
}

pid_t getpid(void) {
    long r = __exyde_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
    return (pid_t)posix_ret(r);
}

void _exit(int status) {
    __exyde_syscall(SYS_EXIT, status, 0, 0, 0, 0);
    for (;;) __asm__ volatile ("hlt");
}
