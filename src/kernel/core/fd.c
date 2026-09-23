#include <exyde/fd.h>
#include <exyde/errno.h>

void fd_table_init(fd_table_t *t) {
    for (u32 i = 0; i < FD_MAX; ++i) t->entries[i] = (file_t *)0;
}

void fd_table_destroy(fd_table_t *t) {
    for (u32 i = 0; i < FD_MAX; ++i) {
        if (t->entries[i]) {
            vfs_close(t->entries[i]);
            t->entries[i] = (file_t *)0;
        }
    }
}

int fd_alloc(fd_table_t *t, file_t *f) {
    if (!t || !f) return -EINVAL;
    for (u32 i = 0; i < FD_MAX; ++i) {
        if (!t->entries[i]) {
            t->entries[i] = f;
            return (int)i;
        }
    }
    return -EMFILE;
}

file_t *fd_get(fd_table_t *t, int fd) {
    if (!t || fd < 0 || (u32)fd >= FD_MAX) return (file_t *)0;
    return t->entries[fd];
}

int fd_close(fd_table_t *t, int fd) {
    if (!t || fd < 0 || (u32)fd >= FD_MAX) return -EBADF;
    file_t *f = t->entries[fd];
    if (!f) return -EBADF;
    t->entries[fd] = (file_t *)0;
    vfs_close(f);
    return 0;
}
