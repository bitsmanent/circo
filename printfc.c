/* See LICENSE file for copyright and license details.
 *
 * Bring colors to printf().
 *
 * Format: CSV of values.
 * Values: fg, bg, att1, att2, attN, ...
 * See: https://en.wikipedia.org/wiki/ANSI_escape_code
 *
 * At least one value must be provided.
 * Any value may be omitted (e.g. %.1C omit fg and set bg=1)
 * Any value may be replaced with * (value is specified by a variable)
 * %C expect an array of values (last one must be -1)
 *
 * Important: this family of function must be used veri careful. The compiler
 * known nothing about %C and also recognized format strings are built at
 * runtime thus the compiler has no hint to test for correctness.
 *
 * Examples:
 *
 * %fg.bg.att1.attN
 * %..0: RESET (no fg, no bg, att 0)
 * %fg
 * %.bg
 * %.bg.att
 * %..att
 * %C (int[]){fg, bg, att1, attN, ..., -1}
 * %*C fg
 * %.*C bg
 * %..*C att
 * %..*.*C att1, att2
 *
 * To understand everything else, start reading doprintfc().
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "printfc.h"

#define COLFG "\33[38;5;%dm"
#define COLBG "\33[48;5;%dm"
#define ATTR  "\33[%dm"

int doprintfc(char *str, size_t size, size_t *slen, char *fmt, va_list ap);
int doprintfc_parse(int *n, char *s, va_list ap);

int
doprintfc(char *str, size_t size, size_t *slen, char *fmt, va_list ap) {
	char *p, *e;
	char f[16]; /* everyone should be happy */
	int len = 0, nc = 0, n;
	int *colors;

	/* both or none */
	if((!str && size) || (!size && str))
		return -1;

/* TODO: find a better way */
#define STRORNULL (len < size ? &str[len] : NULL)
#define SIZORNULL (len < size ? size - len : 0)

	for(p = fmt; *p; ++p) {
		if(*p != '%') {
			if(SIZORNULL)
				str[len] = *p;
			++len;
			++nc;
			continue;
		}
		if(!*++p)
			break;
		if(*p == '%')  {
			if(SIZORNULL)
				str[len] = *p;
			++len;
			++nc;
			continue;
		}

		/* jump to the alpha char */
		for(e = p; *e && !isalpha(*e); ++e);

		/* handle multi-char specifiers like %ld %lld %hhd ... */
		if(*e == 'l' || *e == 'h') {
			++e;
			if(*e == 'l' || *e == 'h')
				++e;
		}

		switch(*e) {
		case 'C':
			if(p == e) {
				colors = va_arg(ap, int *);

				/*
				printf("C0=%d", colors[0]);
				if(colors[1] == -1) printf(" C1=%d", colors[1]);
				for(n = 2; colors[n] != -1; ++n)
					printf(" A%d=%d", n - 2, colors[n]);
				printf("\n");
				*/

				len += snprintf(STRORNULL, SIZORNULL, COLFG, colors[0]);
				if(colors[1] == -1)
					break;
				len += snprintf(STRORNULL, SIZORNULL, COLBG, colors[1]);
				for(n = 2; colors[n] != -1; ++n)
					len += snprintf(STRORNULL, SIZORNULL, ATTR, colors[n]);
				break;
			}

			/* foreground */
			p += doprintfc_parse(&n, p, ap);
			if(n)
				len += snprintf(STRORNULL, SIZORNULL, COLFG, n);

			/* background */
			if(*p == '.') {
				++p;
				p += doprintfc_parse(&n, p, ap);
				if(n)
					len += snprintf(STRORNULL, SIZORNULL, COLBG, n);
			}

			/* attributes */
			while(*p == '.') {
				++p;
				p += doprintfc_parse(&n, p, ap);
				len += snprintf(STRORNULL, SIZORNULL, ATTR, n);
			}
			break;
		default:
			/* TODO: explain what's happening here */
			strncpy(f, p - 1, e - p + 2);
			f[e - p + 2] = '\0';
			p = e;

			n = vsnprintf(STRORNULL, SIZORNULL, f, ap);
			if(n < 0)
				return n;
			len += n;
			nc += n;
			break;
		}
	}
	va_end(ap);

	if(SIZORNULL)
		str[len] = '\0';
	if(slen)
		*slen = len;
	return nc;
}

int
doprintfc_parse(int *n, char *s, va_list ap) {
	char *p = s;

	if(*p == '*') {
		*n = va_arg(ap, int);
		++p;
	}
	else {
		*n = 0;
		while(*p && isdigit(*p)) {
			*n = *n * 10 + *p - '0';
			++p;
		}
	}
	return p - s;
}

int
printfc(char *fmt, ...) {
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vprintfc(fmt, ap);
	va_end(ap);
	return len;
}

int
snprintfc(char *str, size_t size, char *fmt, ...) {
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = doprintfc(str, size, NULL, fmt, ap);
	va_end(ap);
	return len;
}

int
vprintfc(char *fmt, va_list ap) {
	va_list apcopy;
	char *buf;
	size_t size;
	int len;

	va_copy(apcopy, ap);
	len = doprintfc(NULL, 0, &size, fmt, ap);
	if(size < 0)
		return -1;
	if(!(buf = malloc(size+1))) {
		va_end(apcopy);
		return -1;
	}
	doprintfc(buf, size+1, NULL, fmt, apcopy);
	printf("%s", buf);
	va_end(apcopy);
	free(buf);
	return len;
}

int
vsnprintfc(char *str, size_t size, char *fmt, va_list ap) {
	return doprintfc(str, size, NULL, fmt, ap);
}
