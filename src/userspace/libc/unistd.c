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

/* fd 0/1/2 route to the kernel console via the transitional
 * SYS_READ / SYS_WRITE.  fd >= 3 route through libvfs to exy-vfs,
 * using the local fd table to translate local fd -> server fd.
 *
 * open() always goes through libvfs; there is no longer a POSIX
 * open that touches the kernel VFS.  A caller that has not called
 * vfs_client_init() gets ENOSYS from libvfs. */

ssize_t read(int fd, void *buf, size_t count) {
    if (fd >= 0 && fd < 3) {
        if (console_client_ready()) {
            if (fd == STDIN_FILENO) return console_client_read(buf, count);
            /* stdout/stderr are write-only; fall through to kernel
             * which will return the appropriate error. */
        }
        long r = __exyde_syscall(SYS_READ, fd, (long)buf, (long)count, 0, 0);
        return (ssize_t)posix_ret(r);
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    return vfs_client_read((int)sf, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (fd >= 0 && fd < 3) {
        if (console_client_ready() &&
            (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
            return console_client_write(buf, count);
        }
        long r = __exyde_syscall(SYS_WRITE, fd, (long)buf, (long)count, 0, 0);
        return (ssize_t)posix_ret(r);
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
        long r = __exyde_syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
        return (int)posix_ret(r);
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    if (vfs_client_close((int)sf) != 0) return -1;
    return vfs_fdtab_free(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    if (fd >= 0 && fd < 3) {
        long r = __exyde_syscall(SYS_LSEEK, fd, offset, whence, 0, 0);
        return (off_t)posix_ret(r);
    }
    uint32_t sf;
    if (vfs_fdtab_get(fd, &sf) != 0) return -1;
    return vfs_client_seek((int)sf, offset, whence);
}

pid_t getpid(void) {
    long r = __exyde_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
    return (pid_t)posix_ret(r);
}

void *sbrk(long increment) {
    long cur = __exyde_syscall(SYS_BRK, 0, 0, 0, 0, 0);
    if (cur < 0) { errno = (int)-cur; return (void *)-1; }
    if (increment == 0) return (void *)(uintptr_t)cur;
    long want = cur + increment;
    if (want < cur) { errno = ENOMEM; return (void *)-1; }
    long got = __exyde_syscall(SYS_BRK, (unsigned long)want, 0, 0, 0, 0);
    if (got < 0) { errno = (int)-got; return (void *)-1; }
    if (got != want) { errno = ENOMEM; return (void *)-1; }
    return (void *)(uintptr_t)cur;
}

void _exit(int status) {
    __exyde_syscall(SYS_EXIT, status, 0, 0, 0, 0);
    for (;;) __asm__ volatile ("hlt");
}
