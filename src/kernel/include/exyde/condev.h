#ifndef EXYDE_CONDEV_H
#define EXYDE_CONDEV_H

#include <exyde/vfs.h>

/* Initialise the singleton console vnode.  Idempotent. */
void condev_init(void);

/* Return the singleton (or NULL if not initialised).  Does NOT bump
 * the refcount. */
vnode_t *condev_get(void);

/* Return the singleton with an extra reference (caller must
 * vnode_unref() eventually).  Returns NULL if not initialised. */
vnode_t *condev_ref(void);

#endif /* EXYDE_CONDEV_H */
