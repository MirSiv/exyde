#include "internal/vfs_fdtab.h"
#include <errno.h>

typedef struct {
    uint32_t server_fd;
    int      in_use;
} vfs_fd_entry_t;

static vfs_fd_entry_t tab[VFS_FD_LOCAL_MAX];

void vfs_fdtab_init(void) {
    for (int i = 0; i < VFS_FD_LOCAL_MAX; ++i) {
        tab[i].server_fd = 0;
        tab[i].in_use    = 0;
    }
}

int vfs_fdtab_alloc(uint32_t server_fd) {
    for (int i = 3; i < VFS_FD_LOCAL_MAX; ++i) {
        if (!tab[i].in_use) {
            tab[i].server_fd = server_fd;
            tab[i].in_use    = 1;
            return i;
        }
    }
    errno = EMFILE;
    return -1;
}

int vfs_fdtab_get(int local_fd, uint32_t *out_server_fd) {
    if (local_fd < 3 || local_fd >= VFS_FD_LOCAL_MAX ||
        !tab[local_fd].in_use) {
        errno = EBADF;
        return -1;
    }
    if (out_server_fd) *out_server_fd = tab[local_fd].server_fd;
    return 0;
}

int vfs_fdtab_free(int local_fd) {
    if (local_fd < 3 || local_fd >= VFS_FD_LOCAL_MAX ||
        !tab[local_fd].in_use) {
        errno = EBADF;
        return -1;
    }
    tab[local_fd].in_use    = 0;
    tab[local_fd].server_fd = 0;
    return 0;
}
