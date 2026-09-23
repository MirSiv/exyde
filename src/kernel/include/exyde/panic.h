#ifndef EXYDE_PANIC_H
#define EXYDE_PANIC_H

/* Fatal kernel failure.  Prints the message on the early console and
 * halts the CPU forever.  Does not return.
 *
 * Intentionally minimal: no varargs, no formatting.  A printf-like
 * interface will be added later; until then, pass a plain string. */
void panic(const char *msg) __attribute__((noreturn));

#endif /* EXYDE_PANIC_H */
