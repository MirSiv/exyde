#ifndef EXYDE_USERSPACE_STDLIB_H
#define EXYDE_USERSPACE_STDLIB_H

#include <stddef.h>

/* Memory */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* Numeric conversion */
long  strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
int   atoi(const char *s);
long  atol(const char *s);

/* Integer arithmetic */
int   abs(int j);
long  labs(long j);

/* Sorting / searching */
void  qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* Process control */
void  exit(int status) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));

#endif /* EXYDE_USERSPACE_STDLIB_H */
