#include <exyde/console.h>
#include <arch/x86_64/serial.h>

/* x86_64 early console backend: COM1 serial.
 * No buffering, no locks, no interrupts.
 *
 * Color is emitted as ANSI SGR escapes.  The serial layer only
 * translates '\n' to "\r\n"; ESC bytes pass through untouched. */

void console_init(void) {
    serial_init();
}

void console_putc(char c) {
    serial_write_char(c);
}

void console_write(const char *s) {
    serial_write(s);
}

void console_write_hex(u64 value) {
    serial_write_hex(value);
}

void console_write_char(char c) {
    char buf[2] = { c, '\0' };
    console_write(buf);
}

static const char *const sgr_codes[] = {
    "\x1b[0m",   /* RESET          */
    "\x1b[31m",  /* RED            */
    "\x1b[32m",  /* GREEN          */
    "\x1b[33m",  /* YELLOW         */
    "\x1b[34m",  /* BLUE           */
    "\x1b[35m",  /* MAGENTA        */
    "\x1b[36m",  /* CYAN           */
    "\x1b[37m",  /* WHITE          */
    "\x1b[91m",  /* BRIGHT_RED     */
    "\x1b[92m",  /* BRIGHT_GREEN   */
    "\x1b[93m",  /* BRIGHT_YELLOW  */
    "\x1b[97m",  /* BRIGHT_WHITE   */
};

void console_set_color(console_color_t c) {
    unsigned idx = (unsigned)c;
    if (idx >= sizeof(sgr_codes) / sizeof(sgr_codes[0])) return;
    console_write(sgr_codes[idx]);
}

void console_reset_color(void) {
    console_write("\x1b[0m");
}

void console_ok(const char *s) {
    console_set_color(CONSOLE_COLOR_GREEN);
    console_write(s);
    console_reset_color();
}

void console_notice(const char *s) {
    console_set_color(CONSOLE_COLOR_CYAN);
    console_write(s);
    console_reset_color();
}

void console_banner(const char *s) {
    console_set_color(CONSOLE_COLOR_BRIGHT_WHITE);
    console_write(s);
    console_reset_color();
}
