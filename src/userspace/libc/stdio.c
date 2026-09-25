#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>

/* write(1)-backed stdio.
 *
 * Phase 11.3.0: the formatter was extracted into format_core(), which
 * writes characters through a tiny `struct out` sink.  printf/vprintf
 * use an fd sink (one byte per write(2), no buffering).  snprintf/
 * vsnprintf/sprintf/vsprintf use a bounded buffer sink.
 *
 * Still missing (deliberately):
 *   - floating point (%f %e %g): needs a soft-float formatter, and
 *     the userspace is built -mno-sse anyway.
 *   - positional arguments (%1$d).
 *   - locale-aware grouping (').
 * Those, if ever needed, land in a later phase. */

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

/* --- output sinks ---------------------------------------------------- */

/* A formatting session writes characters through `put`.  `count` tracks
 * every character the formatter *intended* to emit (that is the value
 * snprintf must return); `error` records a sink failure. */
struct out {
    int (*put)(void *ctx, int c);
    void *ctx;
    int count;
    int error;
};

static int out_char(struct out *o, int c) {
    if (o->error) return -1;
    if (o->put(o->ctx, c) < 0) {
        o->error = 1;
        return -1;
    }
    ++o->count;
    return 0;
}

static int out_repeat(struct out *o, int c, int n) {
    for (int i = 0; i < n; ++i) {
        if (out_char(o, c) < 0) return -1;
    }
    return 0;
}

/* fd sink: one byte per write(2), matching the pre-11.3.0 printf. */
static int fd_sink_put(void *ctx, int c) {
    int fd = *(int *)ctx;
    char b = (char)c;
    return (write(fd, &b, 1) == 1) ? 0 : -1;
}

/* Buffer sink: writes at most cap-1 bytes, then the caller NUL-
 * terminates.  pos always advances, so the caller can compute the
 * "would-have-written" count even after truncation. */
struct buf_sink {
    char  *buf;
    size_t cap;
    size_t pos;
};

static int buf_sink_put(void *ctx, int c) {
    struct buf_sink *bs = ctx;
    if (bs->pos + 1 < bs->cap) bs->buf[bs->pos] = (char)c;
    ++bs->pos;
    return 0;
}

/* --- integer-to-digits ---------------------------------------------- */

/* Unsigned integer -> digits, most-significant first.  Returns count. */
static int u64_to_base(unsigned long long v, unsigned base, int upper,
                       char *out) {
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v) {
            tmp[n++] = dig[v % base];
            v /= base;
        }
    }
    for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    return n;
}

/* --- flags / length codes ------------------------------------------- */

#define F_LEFT   (1u << 0)
#define F_ZERO   (1u << 1)
#define F_PLUS   (1u << 2)
#define F_SPACE  (1u << 3)
#define F_HASH   (1u << 4)

#define L_NONE 0
#define L_HH   1
#define L_H    2
#define L_L    3
#define L_LL   4
#define L_Z    5
#define L_T    6
#define L_J    7

/* --- formatting core ------------------------------------------------- */

/* Consumes `ap`.  Never returns an error itself -- failures are
 * recorded in o->error.  For an fd sink that means a write failure; for
 * a buffer sink, overflow is silent and only `count` grows. */
static void format_core(struct out *o, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p; ) {
        if (*p != '%') {
            if (out_char(o, (unsigned char)*p++) < 0) return;
            continue;
        }
        ++p;

        /* flags */
        unsigned flags = 0;
        for (;; ++p) {
            if      (*p == '-') flags |= F_LEFT;
            else if (*p == '0') flags |= F_ZERO;
            else if (*p == '+') flags |= F_PLUS;
            else if (*p == ' ') flags |= F_SPACE;
            else if (*p == '#') flags |= F_HASH;
            else break;
        }

        /* width */
        int width = -1;
        if (*p == '*') {
            width = va_arg(ap, int);
            ++p;
            if (width < 0) { flags |= F_LEFT; width = -width; }
        } else {
            while (*p >= '0' && *p <= '9') {
                if (width < 0) width = 0;
                width = width * 10 + (*p - '0');
                ++p;
            }
        }

        /* precision */
        int prec = -1;
        if (*p == '.') {
            ++p;
            prec = 0;
            if (*p == '*') {
                prec = va_arg(ap, int);
                ++p;
                if (prec < 0) prec = -1;
            } else {
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    ++p;
                }
            }
        }

        /* length */
        int len = L_NONE;
        if (*p == 'h') {
            ++p;
            if (*p == 'h') { len = L_HH; ++p; } else len = L_H;
        } else if (*p == 'l') {
            ++p;
            if (*p == 'l') { len = L_LL; ++p; } else len = L_L;
        } else if (*p == 'z') { len = L_Z; ++p; }
        else if   (*p == 't') { len = L_T; ++p; }
        else if   (*p == 'j') { len = L_J; ++p; }

        /* conversion */
        char conv = *p;
        if (!conv) break;
        ++p;

        /* --- %%, %c, %s: handled directly, no numeric buffer ---------- */
        if (conv == '%') {
            if (out_char(o, '%') < 0) return;
            continue;
        }

        if (conv == 'c') {
            int c = va_arg(ap, int);
            int pad = (width > 1) ? width - 1 : 0;
            if (flags & F_LEFT) {
                if (out_char(o, c) < 0) return;
                if (out_repeat(o, ' ', pad) < 0) return;
            } else {
                if (out_repeat(o, ' ', pad) < 0) return;
                if (out_char(o, c) < 0) return;
            }
            continue;
        }

        if (conv == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = 0;
            while (s[slen]) ++slen;
            if (prec >= 0 && slen > prec) slen = prec;
            int pad = (width > slen) ? width - slen : 0;
            if (flags & F_LEFT) {
                for (int i = 0; i < slen; ++i)
                    if (out_char(o, (unsigned char)s[i]) < 0) return;
                if (out_repeat(o, ' ', pad) < 0) return;
            } else {
                if (out_repeat(o, ' ', pad) < 0) return;
                for (int i = 0; i < slen; ++i)
                    if (out_char(o, (unsigned char)s[i]) < 0) return;
            }
            continue;
        }

        /* --- numeric conversions: build body then pad ----------------- */
        char body[96];
        int blen = 0;
        int headlen = 0;     /* length of sign + 0x prefix */
        int zero_ok = 1;     /* '0' flag honored? */

        if (conv == 'p') {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            body[blen++] = '0';
            body[blen++] = 'x';
            headlen = 2;
            char digits[32];
            int dlen = u64_to_base((unsigned long long)v, 16, 0, digits);
            for (int i = 0; i < dlen; ++i) body[blen++] = digits[i];
        } else if (conv == 'd' || conv == 'i' || conv == 'u' ||
                   conv == 'x' || conv == 'X' || conv == 'o') {

            int is_signed = (conv == 'd' || conv == 'i');
            unsigned long long uval = 0;
            int negative = 0;

            if (is_signed) {
                long long sv = 0;
                switch (len) {
                case L_HH: case L_H: case L_NONE:
                    sv = (long long)va_arg(ap, int); break;
                case L_L:
                    sv = (long long)va_arg(ap, long); break;
                case L_LL:
                    sv = va_arg(ap, long long); break;
                case L_Z:
                    sv = (long long)(ptrdiff_t)va_arg(ap, size_t); break;
                case L_T:
                    sv = (long long)va_arg(ap, ptrdiff_t); break;
                case L_J:
                    sv = (long long)va_arg(ap, intmax_t); break;
                }
                if (sv < 0) {
                    negative = 1;
                    /* Avoid UB on LLONG_MIN. */
                    uval = (unsigned long long)(-(sv + 1)) + 1ULL;
                } else {
                    uval = (unsigned long long)sv;
                }
            } else {
                switch (len) {
                case L_HH: case L_H: case L_NONE:
                    uval = (unsigned long long)va_arg(ap, unsigned int); break;
                case L_L:
                    uval = (unsigned long long)va_arg(ap, unsigned long); break;
                case L_LL:
                    uval = va_arg(ap, unsigned long long); break;
                case L_Z:
                    uval = (unsigned long long)va_arg(ap, size_t); break;
                case L_T:
                    uval = (unsigned long long)va_arg(ap, ptrdiff_t); break;
                case L_J:
                    uval = (unsigned long long)va_arg(ap, uintmax_t); break;
                }
            }

            int base  = (conv == 'o') ? 8
                      : (conv == 'u' || conv == 'd' || conv == 'i') ? 10
                      : 16;
            int upper = (conv == 'X');

            if (negative)                body[blen++] = '-';
            else if ((flags & F_PLUS) && is_signed)  body[blen++] = '+';
            else if ((flags & F_SPACE) && is_signed) body[blen++] = ' ';

            if ((flags & F_HASH) && uval != 0) {
                if (base == 16) {
                    body[blen++] = '0';
                    body[blen++] = upper ? 'X' : 'x';
                } else if (base == 8) {
                    body[blen++] = '0';
                }
            }
            headlen = blen;

            char digits[32];
            int dlen = u64_to_base(uval, base, upper, digits);

            if (prec >= 0) {
                zero_ok = 0;
                if (prec == 0 && uval == 0) {
                    dlen = 0;
                } else if (dlen < prec) {
                    for (int i = 0; i < prec - dlen; ++i) body[blen++] = '0';
                }
            }
            for (int i = 0; i < dlen; ++i) body[blen++] = digits[i];
        } else {
            /* Unknown conversion: emit '%' + char literally. */
            body[blen++] = '%';
            body[blen++] = conv;
        }

        int pad = (width > blen) ? width - blen : 0;
        if (flags & F_LEFT) {
            for (int i = 0; i < blen; ++i)
                if (out_char(o, (unsigned char)body[i]) < 0) return;
            if (out_repeat(o, ' ', pad) < 0) return;
        } else if ((flags & F_ZERO) && zero_ok) {
            for (int i = 0; i < headlen; ++i)
                if (out_char(o, (unsigned char)body[i]) < 0) return;
            if (out_repeat(o, '0', pad) < 0) return;
            for (int i = headlen; i < blen; ++i)
                if (out_char(o, (unsigned char)body[i]) < 0) return;
        } else {
            if (out_repeat(o, ' ', pad) < 0) return;
            for (int i = 0; i < blen; ++i)
                if (out_char(o, (unsigned char)body[i]) < 0) return;
        }
    }
}

/* --- public API ------------------------------------------------------ */

int vprintf(const char *fmt, va_list ap) {
    int fd = STDOUT_FILENO;
    struct out o = { fd_sink_put, &fd, 0, 0 };
    format_core(&o, fmt, ap);
    return o.error ? -1 : o.count;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    struct buf_sink bs = { buf, n, 0 };
    struct out o = { buf_sink_put, &bs, 0, 0 };
    format_core(&o, fmt, ap);

    /* NUL-terminate at min(pos, n-1) when the caller gave us room. */
    if (n > 0) {
        size_t t = (bs.pos < n) ? bs.pos : (n - 1);
        buf[t] = 0;
    }
    return o.count;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

/* --- perror ---------------------------------------------------------- */

/* Print "<s>: <strerror(errno)>" to stderr, followed by '\n'.
 * If `s` is NULL or empty, only the message is printed.  errno is
 * captured on entry and not modified. */
void perror(const char *s) {
    int saved = errno;
    const char *msg = strerror(saved);

    if (s && *s) {
        size_t n = strlen(s);
        if (write(STDERR_FILENO, s, n) != (ssize_t)n) return;
        if (write(STDERR_FILENO, ": ", 2) != 2) return;
    }
    size_t m = strlen(msg);
    if (write(STDERR_FILENO, msg, m) != (ssize_t)m) return;
    (void)write(STDERR_FILENO, "\n", 1);
}
