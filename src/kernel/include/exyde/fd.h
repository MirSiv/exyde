#ifndef EXYDE_FD_H
#define EXYDE_FD_H

#include <exyde/types.h>
#include <exyde/vfs.h>

/* Per-process file-descriptor table.  Indices are the fd numbers
 * handed to userspace.  Lowest-free allocation, matching the Unix
 * convention (0/1/2 reserved by init in Phase 10). */
#define FD_MAX  64

typedef struct fd_table {
    file_t *entries[FD_MAX];
} fd_table_t;

void fd_table_init(fd_table_t *t);
void fd_table_destroy(fd_table_t *t);

/* Install `f` at the lowest free slot.  Returns the fd (>= 0) or
 * -EMFILE if the table is full. */
int  fd_alloc(fd_table_t *t, file_t *f);

/* Return the file at fd, or NULL if fd is invalid / closed. */
file_t *fd_get(fd_table_t *t, int fd);

/* Close fd.  Returns 0 on success, -EBADF if fd is not open. */
int  fd_close(fd_table_t *t, int fd);

#endif /* EXYDE_FD_H */
