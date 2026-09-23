#include <unistd.h>
#include <errno.h>
#include <exyde/abi.h>
#include "internal/syscall.h"

static long posix_ret(long r) {
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

ssize_t read(int fd, void *buf, size_t count) {
    long r = __exyde_syscall(SYS_READ, fd, (long)buf, (long)count, 0, 0);
    return (ssize_t)posix_ret(r);
}

ssize_t write(int fd, const void *buf, size_t count) {
    long r = __exyde_syscall(SYS_WRITE, fd, (long)buf, (long)count, 0, 0);
    return (ssize_t)posix_ret(r);
}

int open(const char *path, int flags, ...) {
    long r = __exyde_syscall(SYS_OPEN, (long)path, flags, 0, 0, 0);
    return (int)posix_ret(r);
}

int close(int fd) {
    long r = __exyde_syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
    return (int)posix_ret(r);
}

off_t lseek(int fd, off_t offset, int whence) {
    long r = __exyde_syscall(SYS_LSEEK, fd, offset, whence, 0, 0);
    return (off_t)posix_ret(r);
}

pid_t getpid(void) {
    long r = __exyde_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
    return (pid_t)posix_ret(r);
}

void _exit(int status) {
    __exyde_syscall(SYS_EXIT, status, 0, 0, 0, 0);
    for (;;) __asm__ volatile ("hlt");
}
