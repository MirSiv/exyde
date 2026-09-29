#ifndef EXYDE_USERSPACE_INITRD_H
#define EXYDE_USERSPACE_INITRD_H

#include <stddef.h>

/* cpio newc initrd reader.
 *
 * The kernel maps the raw archive read-only at INITRD_VA and passes
 * its base and size to init through the environment as
 * EXYDE_INITRD_BASE / EXYDE_INITRD_SIZE (both hex, "0x...").
 *
 * This is a userspace concept: the kernel never parses the archive.
 * init (and only init) calls initrd_init() once at startup, then
 * initrd_find() for every program it wants to spawn.
 *
 * Only regular files are recognised.  Directories, symlinks and
 * device nodes may appear in the archive; the parser walks over
 * them.  Hard links are not supported. */

/* Read EXYDE_INITRD_BASE/SIZE from the environment, validate the
 * leading magic.  Returns 0 on success, -1 with errno set:
 *   ENOENT  one of the env vars is missing
 *   EINVAL  bad hex, bad magic, or archive claims more bytes than
 *           the kernel mapped. */
int initrd_init(void);

/* Look up `path` (e.g. "bin/echo") in the archive.  On success
 * stores a pointer to the file body and its length.  On failure
 * returns -1 with errno = ENOENT (not found / not initialised) or
 * EINVAL (archive malformed). */
int initrd_find(const char *path, const void **out_data, size_t *out_size);

#endif /* EXYDE_USERSPACE_INITRD_H */
