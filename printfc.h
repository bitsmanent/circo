/* See LICENSE file for copyright and license details. */

#include <stdarg.h>

int printfc(char *fmt, ...);
int snprintfc(char *str, size_t size, char *fmt, ...);
int vprintfc(char *fmt, va_list ap);
int vsnprintfc(char *str, size_t size, char *fmt, va_list ap);
