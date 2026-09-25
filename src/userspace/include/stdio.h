#ifndef EXYDE_USERSPACE_STDIO_H
#define EXYDE_USERSPACE_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Minimal write-backed stdio for Phase 11.x.
 *
 * printf/vprintf write directly to fd 1 through write(2); snprintf/
 * vsnprintf format into a caller-provided buffer.  Both share one
 * formatting core (see libc/stdio.c).
 *
 * Supported conversions:
 *   %d %i %u %o %x %X %c %s %p %%
 * Supported flags:
 *   '-' (left), '0' (zero-pad), '+' (sign), ' ' (space), '#' (0x / 0 prefix)
 * Supported width:
 *   decimal, or '*' (consumes an int argument)
 * Supported precision:
 *   '.' decimal, or '.*' (consumes an int argument)
 * Supported length modifiers:
 *   hh, h, l, ll, z, t, j
 *
 * Not implemented (deliberately):
 *   - floating point (%f %e %g)
 *   - positional arguments (%1$d)
 *   - locale grouping
 */

#define EOF (-1)

int  putchar(int c);
int  puts(const char *s);

int  printf(const char *fmt, ...)
         __attribute__((format(printf, 1, 2)));
int  vprintf(const char *fmt, va_list ap)
         __attribute__((format(printf, 1, 0)));

int  snprintf(char *buf, size_t n, const char *fmt, ...)
         __attribute__((format(printf, 3, 4)));
int  vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
         __attribute__((format(printf, 3, 0)));

int  sprintf(char *buf, const char *fmt, ...)
         __attribute__((format(printf, 2, 3)));
int  vsprintf(char *buf, const char *fmt, va_list ap)
         __attribute__((format(printf, 2, 0)));

/* Print "<s>: <strerror(errno)>" to stderr (fd 2), followed by '\n'.
 * If `s` is NULL or empty, only the message is printed.  errno is not
 * modified. */
void perror(const char *s);

#endif /* EXYDE_USERSPACE_STDIO_H */
