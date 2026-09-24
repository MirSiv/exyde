#ifndef EXYDE_USERSPACE_STDIO_H
#define EXYDE_USERSPACE_STDIO_H

#include <stddef.h>

/* Minimal write-backed stdio skeleton for Phase 11.1.1.
 *
 * Everything goes straight to fd 1 via write(2) -- no buffering, no
 * FILE objects, no stdin.  A real stdio with FILE / buffers / stdin
 * arrives once the VFS and a TTY are ready (Phase 13/15).
 *
 * printf supports:
 *   %s %c %d %i %u %x %X %p %%
 *   %ld %li %lu %lx %lX %lp
 * Width, precision and length modifiers other than 'l' are not
 * implemented yet -- they will land in 11.2. */

#define EOF (-1)

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* EXYDE_USERSPACE_STDIO_H */
