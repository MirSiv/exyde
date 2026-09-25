#ifndef EXYDE_USERSPACE_UNISTD_H
#define EXYDE_USERSPACE_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     open(const char *path, int flags, ...);
int     close(int fd);
off_t   lseek(int fd, off_t offset, int whence);
pid_t   getpid(void);


/* POSIX declares environ in <unistd.h>; also declared in <stdlib.h>. */
extern char **environ;

void    _exit(int status) __attribute__((noreturn));

#endif /* EXYDE_USERSPACE_UNISTD_H */
