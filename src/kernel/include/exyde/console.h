#ifndef EXYDE_CONSOLE_H
#define EXYDE_CONSOLE_H

#include <exyde/types.h>

void console_init(void);
void console_write(const char *s);
void console_write_char(char c);
void console_write_hex(u64 v);

#endif /* EXYDE_CONSOLE_H */
