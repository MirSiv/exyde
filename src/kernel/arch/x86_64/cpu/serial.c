#include <arch/x86_64/io.h>
#include <arch/x86_64/serial.h>

#define COM1 0x3F8u

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB */
    outb(COM1 + 0, 0x03); /* divisor low: 38400 baud */
    outb(COM1 + 1, 0x00); /* divisor high */
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO enable, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static bool serial_tx_ready(void) {
    return (inb(COM1 + 5) & 0x20u) != 0u;
}

void serial_write_char(char c) {
    while (!serial_tx_ready()) {
        /* spin */
    }
    outb(COM1, (u8)c);
}

void serial_write(const char *s) {
    for (; *s != '\0'; ++s) {
        if (*s == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*s);
    }
}

void serial_write_hex(u64 value) {
    static const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_write_char(digits[(value >> (unsigned)shift) & 0xFu]);
    }
}
