#ifndef _LEFOB_STRING_H
#define _LEFOB_STRING_H

/* Declarations only. The implementations live in ch32fun.c. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void  *memcpy(void *dest, const void *src, size_t n);
void  *memmove(void *dest, const void *src, size_t n);
void  *memset(void *dest, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *src, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
int    strcmp(const char *l, const char *r);
int    strncmp(const char *l, const char *r, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);

#ifdef __cplusplus
}
#endif

#endif /* _LEFOB_STRING_H */
