#ifndef EXYDE_PANIC_H
#define EXYDE_PANIC_H

#include <exyde/types.h>

/* printf-like kernel console output.  Supports:
 *   %s  char*
 *   %c  char
 *   %d  i64 / int
 *   %u  u64 / unsigned
 *   %x  u64, lowercase hex, no leading zeros
 *   %p  pointer, as 0x + lowercase hex
 *   %%  literal '%'
 * Any other conversion is printed verbatim (% + char).
 * No width/precision/flags.  Safe to call from any CPL0 context. */
void exy_printf(const char *fmt, ...);

/* Fatal kernel failure.  Prints the message on the early console and
 * halts the CPU forever.  Does not return. */
void panic(const char *msg) __attribute__((noreturn));

void panicf(const char *fmt, ...) __attribute__((noreturn));

/* If `cond` is false, print file:line + expression, then panic. */
#define BUG_ON(cond) \
    do { \
        if (cond) panicf("BUG: %s:%d: %s", __FILE__, __LINE__, #cond); \
    } while (0)

/* Same, but non-fatal: warn once on the console and continue. */
#define WARN_ON(cond) \
    do { \
        if (cond) exy_printf("WARN: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } while (0)

#endif /* EXYDE_PANIC_H */
