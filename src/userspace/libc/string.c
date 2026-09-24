#include <string.h>
#include <stdlib.h>

/* --- memory -------------------------------------------------------- */

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

/* --- length / compare ---------------------------------------------- */

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0)  return 0;
    }
    return 0;
}

/* --- copy / concat ------------------------------------------------- */

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0') { }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst + strlen(dst);
    while ((*d++ = *src++) != '\0') { }
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    size_t i = 0;
    for (; i < n && src[i]; ++i) d[i] = src[i];
    d[i] = '\0';
    return dst;
}

/* --- search -------------------------------------------------------- */

char *strchr(const char *s, int c) {
    char ch = (char)c;
    for (;; ++s) {
        if (*s == ch) return (char *)s;
        if (*s == '\0') return NULL;
    }
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    for (;; ++s) {
        if (*s == ch) last = s;
        if (*s == '\0') break;
    }
    /* Special case: strrchr("...", '\0') is valid and returns pointer
     * to the NUL terminator; the loop above handles it because we
     * check *s before breaking. */
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { ++h; ++n; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (s[n]) {
        int found = 0;
        for (const char *a = accept; *a; ++a) {
            if (*a == s[n]) { found = 1; break; }
        }
        if (!found) break;
        ++n;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (s[n]) {
        for (const char *r = reject; *r; ++r) {
            if (*r == s[n]) return n;
        }
        ++n;
    }
    return n;
}

char *strtok(char *s, const char *delim) {
    static char *save = NULL;
    if (s) save = s;
    if (!save) return NULL;

    /* Skip leading delimiters. */
    while (*save) {
        int is_delim = 0;
        for (const char *d = delim; *d; ++d) {
            if (*d == *save) { is_delim = 1; break; }
        }
        if (!is_delim) break;
        ++save;
    }
    if (!*save) { save = NULL; return NULL; }

    char *start = save;
    while (*save) {
        for (const char *d = delim; *d; ++d) {
            if (*d == *save) {
                *save = '\0';
                ++save;
                return start;
            }
        }
        ++save;
    }
    /* Reached end without a delimiter: this is the last token. */
    save = NULL;
    return start;
}

/* --- duplicate ----------------------------------------------------- */

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}
