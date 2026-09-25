#include <string.h>
#include <stdlib.h>
#include <errno.h>

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

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char ch = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == ch) return (void *)(p + i);
    }
    return NULL;
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

char *strpbrk(const char *s, const char *accept) {
    for (; *s; ++s) {
        for (const char *a = accept; *a; ++a) {
            if (*a == *s) return (char *)s;
        }
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

/* errno -> short human-readable string.  Never NULL.  Unknown codes
 * get "Unknown error <n>" written into a static buffer; the string is
 * overwritten by the next unknown-code call, which matches the classic
 * non-reentrant strerror() behaviour that programs already expect. */
char *strerror(int errnum) {
    static char unknown[32];

    switch (errnum) {
    case E_OK:         return "Success";
    case EPERM:        return "Operation not permitted";
    case ENOENT:       return "No such file or directory";
    case ESRCH:        return "No such process";
    case EINTR:        return "Interrupted system call";
    case EIO:          return "Input/output error";
    case ENXIO:        return "No such device or address";
    case E2BIG:        return "Argument list too long";
    case ENOEXEC:      return "Exec format error";
    case EBADF:        return "Bad file descriptor";
    case ECHILD:       return "No child processes";
    case EAGAIN:       return "Resource temporarily unavailable";
    case ENOMEM:       return "Cannot allocate memory";
    case EACCES:       return "Permission denied";
    case EFAULT:       return "Bad address";
    case EBUSY:        return "Device or resource busy";
    case EEXIST:       return "File exists";
    case ENODEV:       return "No such device";
    case ENOTDIR:      return "Not a directory";
    case EISDIR:       return "Is a directory";
    case EINVAL:       return "Invalid argument";
    case ENFILE:       return "Too many open files in system";
    case EMFILE:       return "Too many open files";
    case ENOSPC:       return "No space left on device";
    case ESPIPE:       return "Illegal seek";
    case EROFS:        return "Read-only file system";
    case EPIPE:        return "Broken pipe";
    case ERANGE:       return "Numerical result out of range";
    case ENAMETOOLONG: return "File name too long";
    case ENOSYS:       return "Function not implemented";
    case ENOTEMPTY:    return "Directory not empty";
    default: break;
    }

    /* Hand-rolled itoa for the unknown-code buffer.  We could call
     * snprintf, but stdio.c is not linked in every consumer of
     * string.h. */
    const char *pre = "Unknown error ";
    int n = 0;
    while (pre[n]) { unknown[n] = pre[n]; ++n; }

    char digits[12];
    int dn = 0;
    unsigned int u;
    int neg = errnum < 0;
    if (neg) u = (unsigned int)(-(errnum + 1)) + 1u;
    else     u = (unsigned int)errnum;
    if (u == 0) {
        digits[dn++] = '0';
    } else {
        while (u) { digits[dn++] = (char)('0' + (u % 10)); u /= 10; }
    }
    if (neg) unknown[n++] = '-';
    while (dn > 0) unknown[n++] = digits[--dn];
    unknown[n] = '\0';
    return unknown;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return NULL;
    memcpy(p, s, n);
    return p;
}
