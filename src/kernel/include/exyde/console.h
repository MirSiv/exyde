#ifndef EXYDE_CONSOLE_H
#define EXYDE_CONSOLE_H

#include <exyde/types.h>

/* ANSI SGR colors for the serial console.  Order must match the
 * table in console.c. */
typedef enum {
    CONSOLE_COLOR_RESET = 0,
    CONSOLE_COLOR_RED,
    CONSOLE_COLOR_GREEN,
    CONSOLE_COLOR_YELLOW,
    CONSOLE_COLOR_BLUE,
    CONSOLE_COLOR_MAGENTA,
    CONSOLE_COLOR_CYAN,
    CONSOLE_COLOR_WHITE,
    CONSOLE_COLOR_BRIGHT_RED,
    CONSOLE_COLOR_BRIGHT_GREEN,
    CONSOLE_COLOR_BRIGHT_YELLOW,
    CONSOLE_COLOR_BRIGHT_WHITE,
} console_color_t;

void console_init(void);
void console_write(const char *s);
void console_write_char(char c);
void console_write_hex(u64 v);

void console_set_color(console_color_t c);
void console_reset_color(void);

/* Convenience wrappers.  Each prints `s` inside a fixed SGR color
 * and then resets.  Callers include the newline themselves if they
 * want one, so these are safe to nest. */
void console_ok(const char *s);       /* green        - test passed */
void console_notice(const char *s);   /* cyan         - informational */
void console_banner(const char *s);   /* bright white - project markers */

#endif /* EXYDE_CONSOLE_H */
