#ifndef TEACHING_CONSOLE_H
#define TEACHING_CONSOLE_H
#include <stdint.h>
void console_init(void);
void console_putc(char c);
void console_write(const char *text);
void console_uint(uint32_t value);
/* Nonblocking: COM1 serial first, then PS/2 keyboard. Returns -1 if empty. */
int console_read(void);
#endif
