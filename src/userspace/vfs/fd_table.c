#include "vfs_internal.h"
#include <errno.h>
#include <exyde/abi.h>

void vfs_fd_init(vfs_fd_table_t *t) {
    for (int i = 0; i < VFS_FD_MAX; ++i) t->files[i] = (file_t *)0;
}

int vfs_fd_alloc(vfs_fd_table_t *t, file_t *f) {
    for (int i = 0; i < VFS_FD_MAX; ++i) {
        if (!t->files[i]) { t->files[i] = f; return i; }
    }
    return -EMFILE;
}

file_t *vfs_fd_get(vfs_fd_table_t *t, int fd) {
    if (fd < 0 || fd >= VFS_FD_MAX) return (file_t *)0;
    return t->files[fd];
}

int vfs_fd_close(vfs_fd_table_t *t, int fd) {
    if (fd < 0 || fd >= VFS_FD_MAX) return -EBADF;
    if (!t->files[fd]) return -EBADF;
    vfs_close(t->files[fd]);
    t->files[fd] = (file_t *)0;
    return 0;
}
