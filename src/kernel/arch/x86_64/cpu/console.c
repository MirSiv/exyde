#include <exyde/console.h>
#include <arch/x86_64/serial.h>

/* x86_64 early console backend: COM1 serial.
 * No buffering, no locks, no interrupts. */

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