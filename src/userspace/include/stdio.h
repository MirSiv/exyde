#ifndef EXYDE_USERSPACE_STDIO_H
#define EXYDE_USERSPACE_STDIO_H

#include <stddef.h>

/* Minimal write-backed stdio for Phase 11.x.
 *
 * Everything goes straight to fd 1 via write(2) -- no buffering, no
 * FILE objects, no stdin.  A real stdio with FILE / buffers / stdin
 * arrives once the VFS and a TTY are ready (Phase 13/15).
 *
 * printf supports:
 *   conversions : %d %i %u %o %x %X %c %s %p %%
 *   flags       : '-' (left), '0' (zero-pad), '+' (sign), ' ' (space),
 *                 '#' (0x / 0 prefix)
 *   width       : decimal, or '*' (consumes an int argument)
 *   precision   : '.' decimal, or '.*' (consumes an int argument)
 *   length      : hh, h, l, ll, z, t, j
 *
 * Not implemented (deliberately, needs more than a write loop):
 *   - floating point (%f %e %g)
 *   - positional arguments (%1$d)
 *   - locale grouping
 */

#define EOF (-1)

int  putchar(int c);
int  puts(const char *s);
int  printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Print "<s>: <strerror(errno)>" to stderr (fd 2), followed by '\n'.
 * If `s` is NULL or empty, only the message is printed.  errno is not
 * modified. */
void perror(const char *s);

#endif /* EXYDE_USERSPACE_STDIO_H */
