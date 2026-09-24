#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>

/* Minimal write-backed stdio.  Every byte goes through write(1) one at
 * a time for now -- fine for init and early bring-up.  Buffering and a
 * proper FILE layer come with the TTY in Phase 13/15. */

int putchar(int c) {
    char b = (char)c;
    if (write(STDOUT_FILENO, &b, 1) != 1) return EOF;
    return (int)(unsigned char)b;
}

int puts(const char *s) {
    while (*s) {
        if (putchar((unsigned char)*s++) == EOF) return EOF;
    }
    if (putchar('\n') == EOF) return EOF;
    return 0;
}

static int print_str(const char *s) {
    int n = 0;
    while (*s) {
        if (putchar((unsigned char)*s++) == EOF) return -1;
        ++n;
    }
    return n;
}

/* Unsigned integer in arbitrary base (10 or 16).  Emits most-significant
 * digit first.  `upper` selects A-F vs a-f; ignored for base 10. */
static int print_uint(unsigned long v, unsigned base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[32];
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v) {
            buf[i++] = digits[v % base];
            v /= base;
        }
    }
    int n = 0;
    while (i > 0) {
        if (putchar(buf[--i]) == EOF) return -1;
        ++n;
    }
    return n;
}

static int print_int(long v) {
    if (v < 0) {
        if (putchar('-') == EOF) return -1;
        /* Avoid UB on LONG_MIN: negate via unsigned. */
        unsigned long u = (unsigned long)(-(v + 1)) + 1UL;
        int r = print_uint(u, 10, 0);
        return r < 0 ? -1 : r + 1;
    }
    return print_uint((unsigned long)v, 10, 0);
}

static int print_ptr(const void *p) {
    int n = 0;
    if (putchar('0') == EOF) return -1;
    ++n;
    if (putchar('x') == EOF) return -1;
    ++n;
    int r = print_uint((unsigned long)(uintptr_t)p, 16, 0);
    return r < 0 ? -1 : n + r;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int n = 0;
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            if (putchar((unsigned char)*p) == EOF) { n = -1; goto out; }
            ++n;
            continue;
        }

        ++p;
        switch (*p) {
        case '%':
            if (putchar('%') == EOF) { n = -1; goto out; }
            ++n;
            break;

        case 'c': {
            int c = va_arg(ap, int);
            if (putchar(c) == EOF) { n = -1; goto out; }
            ++n;
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            int r = print_str(s ? s : "(null)");
            if (r < 0) { n = -1; goto out; } n += r; break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            int r = print_int(v);
            if (r < 0) { n = -1; goto out; } n += r; break;
        }
        case 'u': {
            unsigned v = va_arg(ap, unsigned);
            int r = print_uint(v, 10, 0);
            if (r < 0) { n = -1; goto out; } n += r; break;
        }
        case 'x': {
            unsigned v = va_arg(ap, unsigned);
            int r = print_uint(v, 16, 0);
            if (r < 0) { n = -1; goto out; } n += r; break;
        }
        case 'X': {
            unsigned v = va_arg(ap, unsigned);
            int r = print_uint(v, 16, 1);
            if (r < 0) { n = -1; goto out; } n += r; break;
        }
        case 'p': {
            const void *v = va_arg(ap, const void *);
            int r = print_ptr(v);
            if (r < 0) { n = -1; goto out; } n += r; break;
        }

        case 'l':
            ++p;
            switch (*p) {
            case 'd':
            case 'i': {
                long v = va_arg(ap, long);
                int r = print_int(v);
                if (r < 0) { n = -1; goto out; } n += r; break;
            }
            case 'u': {
                unsigned long v = va_arg(ap, unsigned long);
                int r = print_uint(v, 10, 0);
                if (r < 0) { n = -1; goto out; } n += r; break;
            }
            case 'x': {
                unsigned long v = va_arg(ap, unsigned long);
                int r = print_uint(v, 16, 0);
                if (r < 0) { n = -1; goto out; } n += r; break;
            }
            case 'X': {
                unsigned long v = va_arg(ap, unsigned long);
                int r = print_uint(v, 16, 1);
                if (r < 0) { n = -1; goto out; } n += r; break;
            }
            case 'p': {
                const void *v = va_arg(ap, const void *);
                int r = print_ptr(v);
                if (r < 0) { n = -1; goto out; } n += r; break;
            }
            default:
                /* Unknown: emit literally. */
                if (putchar('%') == EOF) { n = -1; goto out; }
                ++n;
                if (putchar('l') == EOF) { n = -1; goto out; }
                ++n;
                if (*p && putchar((unsigned char)*p) == EOF) { n = -1; goto out; }
                if (*p) ++n;
                break;
            }
            break;

        default:
            /* Unknown specifier: emit '%' + char literally. */
            if (putchar('%') == EOF) { n = -1; goto out; }
            ++n;
            if (*p && putchar((unsigned char)*p) == EOF) { n = -1; goto out; }
            if (*p) ++n;
            break;
        }
    }

out:
    va_end(ap);
    return n;
}
