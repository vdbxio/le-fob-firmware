#ifndef _LEFOB_STDIO_H
#define _LEFOB_STDIO_H

/* Declarations only. The implementations live in ch32fun.c. */

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int snprintf(char *buffer, unsigned int buffer_len, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int puts(const char *s);
int putchar(int c);

#ifdef __cplusplus
}
#endif

#endif /* _LEFOB_STDIO_H */
