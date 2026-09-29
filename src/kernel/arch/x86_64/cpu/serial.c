#include <arch/x86_64/io.h>
#include <arch/x86_64/serial.h>

#define COM1 0x3F8u

/* Line Status Register bits we care about. */
#define LSR_DATA_READY   0x01u   /* RX FIFO has at least one byte  */
#define LSR_TX_EMPTY     0x20u   /* TX holding register is empty   */

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
    return (inb(COM1 + 5) & LSR_TX_EMPTY) != 0u;
}

static bool serial_rx_ready(void) {
    return (inb(COM1 + 5) & LSR_DATA_READY) != 0u;
}

void serial_write_char(char c) {
    while (!serial_tx_ready()) {
        /* spin */
    }
    outb(COM1, (u8)c);
}

/* Non-blocking: returns -1 rather than spinning when the RX FIFO is
 * empty.  The console server is responsible for waiting (poll +
 * SYS_YIELD); the kernel has no serial IRQ handler to block on. */
int serial_read_char(void) {
    if (!serial_rx_ready()) return -1;
    return (int)(u8)inb(COM1);
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
