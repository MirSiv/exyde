#ifndef EXYDE_USERSPACE_STRING_H
#define EXYDE_USERSPACE_STRING_H

#include <stddef.h>

/* Memory */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);

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
char  *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* Tokenizer (not thread-safe, as in the C standard) */
char  *strtok(char *s, const char *delim);

/* Duplicate (uses malloc from stdlib.h) */
char  *strdup(const char *s);

/* errno -> short human-readable string.  Never returns NULL; unknown
 * codes yield "Unknown error <n>" in a thread-local-ish static buffer. */
char  *strerror(int errnum);

/* TODO (not in scope for Phase 11.3.x):
 *   - strcoll / strxfrm: require a locale; there is no locale
 *     infrastructure yet.  Add when/if that exists.
 *   - memrchr: GNU extension, not POSIX.  Add only if a caller needs it.
 *   - strtok_r: reentrant tokenizer; lands with threads.
 *   - strerror_r: reentrant variant; same reason.
 */

#endif /* EXYDE_USERSPACE_STRING_H */
