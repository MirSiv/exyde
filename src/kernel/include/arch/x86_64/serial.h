#ifndef EXYDE_ARCH_X86_64_SERIAL_H
#define EXYDE_ARCH_X86_64_SERIAL_H

#include <exyde/types.h>

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char *s);
void serial_write_hex(u64 value);

/* Non-blocking read from the UART RX FIFO.  Returns 0..255 if a byte
 * is currently available, or -1 if the RX FIFO is empty.  Polling
 * only: the kernel does not install a serial IRQ handler in Phase 12,
 * so there is nothing to wake a blocked reader. */
int serial_read_char(void);

#endif /* EXYDE_ARCH_X86_64_SERIAL_H */
