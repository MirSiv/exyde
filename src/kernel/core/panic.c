#include <exyde/panic.h>
#include <exyde/console.h>

/* ---- kprintf ------------------------------------------------------- */

static void put_u64_dec(u64 v) {
    char buf[21];
    int n = 0;
    if (v == 0) { console_write_char('0'); return; }
    while (v) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n--) console_write_char(buf[n]);
}

static void put_i64_dec(i64 v) {
    if (v < 0) {
        console_write_char('-');
        /* avoid UB on INT64_MIN */
        put_u64_dec((u64)(-(v + 1)) + 1);
    } else {
        put_u64_dec((u64)v);
    }
}

static void put_u64_hex(u64 v) {
    static const char d[] = "0123456789abcdef";
    char buf[16];
    int n = 0;
    if (v == 0) { console_write_char('0'); return; }
    while (v) { buf[n++] = d[v & 0xF]; v >>= 4; }
    while (n--) console_write_char(buf[n]);
}

void kprintf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') { console_write_char(*p); continue; }
        ++p;
        switch (*p) {
            case 's': {
                const char *s = __builtin_va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) console_write_char(*s++);
                break;
            }
            case 'c':
                console_write_char((char)__builtin_va_arg(ap, int));
                break;
            case 'd':
                put_i64_dec(__builtin_va_arg(ap, i64));
                break;
            case 'u':
                put_u64_dec(__builtin_va_arg(ap, u64));
                break;
            case 'x':
                put_u64_hex(__builtin_va_arg(ap, u64));
                break;
            case 'p':
                console_write("0x");
                put_u64_hex((u64)(uintptr_t)__builtin_va_arg(ap, void *));
                break;
            case '%':
                console_write_char('%');
                break;
            case '\0':
                __builtin_va_end(ap);
                return;
            default:
                console_write_char('%');
                console_write_char(*p);
                break;
        }
    }
    __builtin_va_end(ap);
}

/* ---- panic --------------------------------------------------------- */

void panic(const char *msg) {
    console_write("\n*** KERNEL PANIC ***\n");
    console_write(msg);
    console_write("\nSystem halted.\n");
    for (;;) __asm__ volatile("cli; hlt");
}

void panicf(const char *fmt, ...) {
    console_write("\n*** KERNEL PANIC ***\n");
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    /* kprintf already does the formatting; re-implement the small
     * subset inline so we don't need a v-kprintf variant yet. */
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') { console_write_char(*p); continue; }
        ++p;
        switch (*p) {
            case 's': {
                const char *s = __builtin_va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) console_write_char(*s++);
                break;
            }
            case 'c':
                console_write_char((char)__builtin_va_arg(ap, int));
                break;
            case 'd':
                put_i64_dec(__builtin_va_arg(ap, i64));
                break;
            case 'u':
                put_u64_dec(__builtin_va_arg(ap, u64));
                break;
            case 'x':
                put_u64_hex(__builtin_va_arg(ap, u64));
                break;
            case 'p':
                console_write("0x");
                put_u64_hex((u64)(uintptr_t)__builtin_va_arg(ap, void *));
                break;
            case '%':
                console_write_char('%');
                break;
            case '\0':
                __builtin_va_end(ap);
                console_write("\nSystem halted.\n");
                for (;;) __asm__ volatile("cli; hlt");
            default:
                console_write_char('%');
                console_write_char(*p);
                break;
        }
    }
    __builtin_va_end(ap);
    console_write("\nSystem halted.\n");
    for (;;) __asm__ volatile("cli; hlt");
}
