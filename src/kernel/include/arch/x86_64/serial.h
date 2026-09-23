#ifndef EXYDE_ARCH_X86_64_SERIAL_H
#define EXYDE_ARCH_X86_64_SERIAL_H

#include <exyde/types.h>

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char *s);
void serial_write_hex(u64 value);

#endif /* EXYDE_ARCH_X86_64_SERIAL_H */
