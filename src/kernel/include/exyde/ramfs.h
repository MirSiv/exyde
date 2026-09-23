#ifndef EXYDE_RAMFS_H
#define EXYDE_RAMFS_H

#include <exyde/vfs.h>

/* Create an empty RAMFS root directory vnode.  Returns NULL on
 * allocation failure.  The returned vnode carries a single
 * reference owned by the caller. */
vnode_t *ramfs_create_root(void);

#endif /* EXYDE_RAMFS_H */
