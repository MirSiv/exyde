#ifndef EXYDE_USERSPACE_STDLIB_H
#define EXYDE_USERSPACE_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);

void  exit(int status) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));

#endif /* EXYDE_USERSPACE_STDLIB_H */
