#ifndef EXYDE_USERSPACE_STRING_H
#define EXYDE_USERSPACE_STRING_H

#include <stddef.h>

/* Memory */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

/* Length / compare */
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);

/* Copy / concat */
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);

/* Search */
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* Tokenizer (not thread-safe, as in the C standard) */
char  *strtok(char *s, const char *delim);

/* Duplicate (uses malloc from stdlib.h) */
char  *strdup(const char *s);

#endif /* EXYDE_USERSPACE_STRING_H */
