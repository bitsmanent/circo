/* See LICENSE file for copyright and license details.
 *
 * circo is an IRC client...
 *
 * The messages handlers are organized in an array which is accessed whenever a
 * new message has been fetched. This allows message dispatching in O(1) time.
 *
 * Keys are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
*/

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "printfc.h"

#include "arg.h"
char *argv0;

/* macros */
#define LENGTH(X)       (sizeof X / sizeof X[0])
#define ISCHANPFX(P)    ((P) == '#' || (P) == '&')
#define ISCHAN(B)       ISCHANPFX((B)->name[0])

/* UTF-8 utils */
#define UTF8BYTES(X)    ( ((X) & 0xF0) == 0xF0 ? 4 \
			: ((X) & 0xE0) == 0xE0 ? 3 \
			: ((X) & 0xC0) == 0xC0 ? 2 \
			: 1)
#define UTF8CBYTE(X)    (((X) & 0xC0) == 0x80)

/* drawing flags */
#define REDRAW_BAR      1<<1
#define REDRAW_BUFFER   1<<2
#define REDRAW_CMDLN    1<<3
#define REDRAW_ALL      (REDRAW_BAR|REDRAW_BUFFER|REDRAW_CMDLN)

/* VT100 escape sequences */
#define CLEAR           "\33[2J"
#define CLEARLN         "\33[2K"
#define CLEARRIGHT      "\33[0K"
#define CURPOS          "\33[%d;%dH"
#define CURSON          "\33[?25h"
#define CURSOFF         "\33[?25l"

#if defined CTRL && defined _AIX
  #undef CTRL
#endif
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

/* enums */
enum { KeyFirst = -999, KeyUp, KeyDown, KeyRight, KeyLeft, KeyHome, KeyEnd, KeyDel, KeyPgUp, KeyPgDw, KeyBackspace, KeyLast };
enum { LineToOffset, OffsetToLine, TotalLines }; /* bufinfo() flags */

enum {
	NickNormal,
	NickMention,
	IRCMessage,
	ColorLast
} Color;

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Nick Nick;
struct Nick {
	char name[16];
	int len;
	Nick *next;
};

typedef struct Buffer Buffer;
struct Buffer {
	char *data;
	char name[64];
	char *hist;
	char cmdbuf[256];
	int size, len, kicked;
	int line, nlines, lnoff;
	int cmdlen, cmdoff, cmdpos;
	int histsz, histlnoff;
	int need_redraw;
	int notify;
	int totnames;
	Nick *names;
	Buffer *next;
};

typedef struct {
	char *name;
	void (*func)(char *, char *);
} Command;

typedef struct {
	char *name;
	void (*func)(char *, char *, char *);
} Message;

typedef struct {
	const int key;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

/* function declarations */
void attach(Buffer *b);
int bprintf(Buffer *b, char *fmt, ...);
int bufinfo(char *buf, int len, int val, int act);
void cleanup(void);
void cmd_close(char *cmd, char *s);
void cmd_exit(char *cmd, char *s);
void cmd_msg(char *cmd, char *s);
void cmd_quit(char *cmd, char *s);
void cmd_rejoinall(char *cmd, char *s);
void cmd_server(char *cmd, char *s);
void cmd_topic(char *cmd, char *s);
void cmdln_chldel(const Arg *arg);
void cmdln_chrdel(const Arg *arg);
void cmdln_clear(const Arg *arg);
void cmdln_complete(const Arg *arg);
void cmdln_cursor(const Arg *arg);
void cmdln_submit(const Arg *arg);
void cmdln_wdel(const Arg *arg);
void detach(Buffer *b);
int dial(char *host, char *port);
void die(const char *fmt, ...);
void draw(void);
void drawbar(void);
void drawbuf(void);
void drawcmdln(void);
void *ecalloc(size_t nmemb, size_t size);
Buffer *getbuf(char *name);
void focus(Buffer *b);
void focusnext(const Arg *arg);
void focusprev(const Arg *arg);
void freebuf(Buffer *b);
void freenames(Nick **names);
char *gcsfitcols(char *s, int maxw);
int gcswidth(char *s, int len);
int getkey(void);
void hangsup(void);
void history(const Arg *arg);
void histpush(char *buf, int len);
void logw(char *txt);
int mvprintf(int x, int y, char *fmt, ...);
Buffer *newbuf(char *name);
Nick *nickadd(Buffer *b, char *name);
void nickdel(Buffer *b, char *name);
Nick *nickget(Buffer *b, char *name);
void nicklist(Buffer *b, char *list);
void nickmv(char *old, char *new);
void parsecmd(char *cmd);
void parsesrv(void);
void privmsg(char *to, char *txt);
void quit(char *msg);
int readchar(void);
void recv_busynick(char *u, char *u2, char *u3);
void recv_join(char *who, char *chan, char *txt);
void recv_kick(char *who, char *chan, char *txt);
void recv_luserme(char *a, char *b, char *c);
void recv_mode(char *u, char *val, char *u2);
void recv_motd(char *u, char *u2, char *txt);
void recv_names(char *usr, char *par, char *txt);
void recv_namesend(char *host, char *par, char *names);
void recv_nick(char *who, char *u, char *txt);
void recv_notice(char *who, char *u, char *txt);
void recv_part(char *who, char *chan, char *txt);
void recv_ping(char *u, char *u2, char *txt);
void recv_privmsg(char *from, char *to, char *txt);
void recv_quit(char *who, char *u, char *txt);
void recv_topic(char *who, char *chan, char *txt);
void recv_topicrpl(char *usr, char *par, char *txt);
void resize(int x, int y);
void scroll(const Arg *arg);
void setup(void);
void sigchld(int unused);
void sigwinch(int unused);
char *skip(char *s, char c);
void sout(char *fmt, ...);
void spawn(const char **cmd);
void strip_ctrlseqs(char *s);
void trim(char *s);
void usage(void);
void usrin(void);
int utf8len(char *s, size_t sz);
char *wordleft(char *str, int offset, int *size);

/* variables */
FILE *srv, *logp;
Buffer *buffers, *status, *sel;
char bufin[4096];
char bufout[4096];
struct termios origti;
time_t trespond;
int running = 1;
int rows, cols;

Message messages[] = {
	{ "JOIN",    recv_join },
	{ "KICK",    recv_kick },
	{ "MODE",    recv_mode },
	{ "NICK",    recv_nick },
	{ "NOTICE",  recv_notice },
	{ "PART",    recv_part },
	{ "PING",    recv_ping },
	{ "PRIVMSG", recv_privmsg },
	{ "QUIT",    recv_quit },
	{ "TOPIC",   recv_topic },
	{ "255",     recv_luserme },
	{ "331",     recv_topicrpl }, /* no topic set */
	{ "332",     recv_topicrpl },
	{ "353",     recv_names },
	{ "366",     recv_namesend },
	{ "372",     recv_motd },
	{ "433",     recv_busynick },
	{ "437",     recv_busynick },

	/* ignored */
	{ "PONG",    NULL },
	{ "470",     NULL }, /* channel forward */

};

/* configuration, allows nested code to access above variables */
#include "config.h"

/* function implementations */
void
attach(Buffer *b) {
	b->next = buffers;
	buffers = b;
}

int
bprintf(Buffer *b, char *fmt, ...) {
	va_list ap;
	time_t tm;
	char buf[512];
	int len = 0;

	tm = time(NULL);
        len = strftime(buf, sizeof buf, TIMESTAMP_FORMAT, localtime(&tm));
	va_start(ap, fmt);
	vsnprintfc(&buf[len], sizeof buf - len - 1, fmt, ap);
	len += strlen(&buf[len]);
	va_end(ap);
	if(!b->size || b->len + len >= b->size)
		if(!(b->data = realloc(b->data, b->size += len + 256)))
			die("realloc()\n");
	memcpy(&b->data[b->len], buf, len);
	b->len += len;
	b->nlines = bufinfo(b->data, b->len, 0, TotalLines);
	sel->need_redraw |= REDRAW_BUFFER;

	/* It's easy to just logw() in bprintf() so we can log
	 * anything in a single place. Though this force us to:
	 * - log data in the same format as the actual UI, and
	 * - strip control sequences (colors) from the buffer.
	 * Apart from this all should be fine. */
	strip_ctrlseqs(buf);
	logw(buf);

	return len;
}

int
bufinfo(char *buf, int len, int val, int act) {
	int x, y, i;

	for(i = 0, x = y = 1; i < len; ++i) {
		switch(act) {
		case LineToOffset:
			if(val == y)
				return i;
			break;
		case OffsetToLine:
			if(val == i)
				return y;
			break;
		}
		if(x == cols || buf[i] == '\n') {
			if(buf[i] != '\n' && i < len - 1 && buf[i + 1] == '\n')
				++i;
			x = 1;
			++y;
		}
		else
			++x;
	}
	return (act == TotalLines ? y : 0) - 1;
}

void
cleanup(void) {
	Buffer *b;

	while((b = buffers)) {
		buffers = buffers->next;
		freebuf(b);
	}
	tcsetattr(0, TCSANOW, &origti);
}

void
cmd_close(char *cmd, char *s) {
	Buffer *b;

	b = *s ? getbuf(s) : sel;
	if(!b) {
		bprintf(status, "/%s: %s: unknown buffer.\n", cmd, s);
		return;
	}
	if(b == status) {
		bprintf(status, "/%s: cannot close the status.\n", cmd);
		return;
	}
	if(srv && ISCHAN(b) && !b->kicked)
		sout("PART :%s", b->name); /* Note: you may be not in that channel */
	if(b == sel) {
		sel = sel->next ? sel->next : buffers;
		sel->need_redraw |= REDRAW_ALL;
	}
	detach(b);
	freebuf(b);
}

void
cmd_exit(char *cmd, char *msg) {
	quit(*msg ? msg : QUIT_MESSAGE);
	running = 0;
}

void
cmd_msg(char *cmd, char *s) {
	char *to, *txt;

	if(!srv) {
		bprintf(sel, "/%s: not connected.\n", cmd);
		return;
	}
	trim(s);
	to = s;
	txt = skip(to, ' ');
	if(!(*to && *txt)) {
		bprintf(sel, "Usage: /%s <channel or user> <text>\n", cmd);
		return;
	}
	privmsg(to, txt);
}

void
cmd_quit(char *cmd, char *msg) {
	Buffer *b;

	if(!srv) {
		bprintf(sel, "/%s: not connected.\n", cmd);
		return;
	}
	if(!*msg)
		msg = QUIT_MESSAGE;
	quit(msg);
	for(b = buffers; b; b = b->next)
		bprintf(b, "Quit (%s)\n", msg);
}

void
cmd_rejoinall(char *cmd, char *s) {
	Buffer *b;

	if(!srv) {
		bprintf(sel, "/%s: not connected.\n", cmd);
		return;
	}
	for(b = buffers; b; b = b->next)
		if(ISCHAN(b))
			sout("JOIN %s", b->name);
}

void
cmd_server(char *cmd, char *s) {
	char *t;
	int fd;

	t = skip(s, ' ');
	if(!*t)
		t = port;
	else
		strncpy(port, t, sizeof port);
	t = s;
	if(!*t)
		t = host;
	else
		strncpy(host, t, sizeof host);
	if(srv)
		quit(QUIT_MESSAGE);
	bprintf(status, "Connecting to %s:%s...\n", host, port);
	if((fd = dial(host, port)) < 0) { /* Note: dial() locks. */
		bprintf(status, "Cannot connect to %s on port %s.\n", host, port);
		return;
	}
	srv = fdopen(fd, "r+");
	setbuf(srv, NULL);
	sout("NICK %s", nick);
	sout("USER %s localhost %s :%s", nick, host, nick);
	sel->need_redraw |= REDRAW_BAR;
}

void
cmd_topic(char *cmd, char *s) {
	char *chan, *txt;

	if(!srv) {
		bprintf(sel, "/%s: not connected.\n", cmd);
		return;
	}
	if(!*s) {
		if(ISCHAN(sel))
			sout("TOPIC %s", sel->name);
		else
			bprintf(sel, "/%s: %s in not a channel.\n", cmd, sel->name);
		return;
	}
	if(ISCHANPFX(*s)) {
		chan = s;
		txt = skip(s, ' ');
		if(!*txt) {
			sout("TOPIC %s", chan);
			return;
		}
	}
	else {
		if(sel == status) {
			bprintf(sel, "Usage: /%s [channel] [text]\n", cmd);
			return;
		}
		chan = sel->name;
		txt = s;
	}
	sout("TOPIC %s :%s", chan, txt);
}

void
cmdln_chldel(const Arg *arg) {
	int nb;

	if(!sel->cmdoff)
		return;
	for(nb = 1; UTF8CBYTE(sel->cmdbuf[sel->cmdoff - nb]); ++nb);
	if(sel->cmdoff < sel->cmdlen)
		memmove(&sel->cmdbuf[sel->cmdoff - nb], &sel->cmdbuf[sel->cmdoff],
			sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= nb;
	sel->cmdoff -= nb;
	sel->cmdbuf[sel->cmdlen] = '\0';
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_chrdel(const Arg *arg) {
	int nb;

	if(!sel->cmdlen)
		return;
	if(sel->cmdoff == sel->cmdlen) {
		for(nb = 1; UTF8CBYTE(sel->cmdbuf[sel->cmdoff - nb]); ++nb);
		sel->cmdoff -= nb;
		sel->need_redraw |= REDRAW_CMDLN;
		return;
	}
	nb = UTF8BYTES(sel->cmdbuf[sel->cmdoff]);
	memmove(&sel->cmdbuf[sel->cmdoff], &sel->cmdbuf[sel->cmdoff + nb],
		sel->cmdlen - sel->cmdoff);

	sel->cmdlen -= nb;
	sel->cmdbuf[sel->cmdlen] = '\0';

	if(sel->cmdoff && sel->cmdoff == sel->cmdlen) {
		for(nb = 1; UTF8CBYTE(sel->cmdbuf[sel->cmdoff - nb]); ++nb);
		sel->cmdoff -= nb;
	}

	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_clear(const Arg *arg) {
	if(!sel->cmdoff)
		return;
	/* preserve text on the right */
	memmove(sel->cmdbuf, &sel->cmdbuf[sel->cmdoff], sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= sel->cmdoff;
	sel->cmdbuf[sel->cmdlen] = '\0';
	sel->cmdoff = 0;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_complete(const Arg *arg) {
	char word[64];
	char *match = NULL, *ws, *we, *epos;
	int wlen, mlen, newlen, i;
	Nick *n;

	if(!sel->cmdlen)
		return;
	ws = wordleft(sel->cmdbuf, sel->cmdoff, &wlen);
	if(!ws || wlen > sizeof word)
		return;
	memcpy(word, ws, wlen);
	word[wlen] = '\0';

	/* actual search */
	if(word[0] == '/') {
		/* search in commands */
		for(i = 0; i < LENGTH(commands) && !match; ++i)
			if(!strncasecmp(commands[i].name, &word[1], wlen - 1))
				match = commands[i].name;
		if(!match)
			return;
		mlen = strlen(match);
		/* preserve the slash */
		++ws;
		--wlen;
	}
	else if(ISCHANPFX(word[0])) {
		/* search buffer name */
		if(strncasecmp(sel->name, word, wlen))
			return;
		match = sel->name;
		mlen = strlen(match);
	}
	else {
		/* match a nick in current buffer */
		for(n = sel->names; n && !match; n = n->next)
			if(!strncasecmp(n->name, word, wlen))
				break;
		if(!n)
			return;
		match = n->name;
		mlen = n->len;
	}

	we = ws + wlen;
	epos = &sel->cmdbuf[sel->cmdoff] > we ? &sel->cmdbuf[sel->cmdoff] : we;

	/* check if match exceed buffer size */
	newlen = sel->cmdlen - (epos - ws) + mlen;
	if(newlen > sizeof sel->cmdbuf - 1)
		return;

	memmove(ws+mlen, epos, sel->cmdlen - (epos - sel->cmdbuf));
	memcpy(ws, match, mlen);

	sel->cmdlen = newlen;
	sel->cmdbuf[sel->cmdlen] = '\0';
	sel->cmdoff = ws - sel->cmdbuf + mlen;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_cursor(const Arg *arg) {
	int i = arg->i, nb;

	if(!i) {
		sel->cmdoff = 0;
		sel->need_redraw |= REDRAW_CMDLN;
		return;
	}
	if(i < 0) {
		nb = 1;
		while(++i <= 0)
			while(UTF8CBYTE(sel->cmdbuf[sel->cmdoff - nb]))
				++nb;
		nb = -nb;
	}
	else {
		nb = 0;
		while(--i >= 0)
			nb += UTF8BYTES(sel->cmdbuf[sel->cmdoff + nb]);
	}

	sel->cmdoff += nb;
	if(sel->cmdoff < 0)
		sel->cmdoff = 0;
	else if(sel->cmdoff > sel->cmdlen)
		sel->cmdoff = sel->cmdlen;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_submit(const Arg *arg) {
	char *buf;

	if(sel->cmdbuf[0] == '\0')
		return;
	buf = ecalloc(1, sel->cmdlen);
	memcpy(buf, sel->cmdbuf, sel->cmdlen);

	logw(buf);
	logw("\n");

	histpush(sel->cmdbuf, sel->cmdlen);

	if(buf[0] == '/') {
		sel->cmdlen = sel->cmdoff = sel->histlnoff = 0;
		sel->cmdbuf[sel->cmdlen] = '\0';
		sel->need_redraw |= REDRAW_CMDLN;

		/* Note: network latency may cause visual glitches. */
		parsecmd(buf);
	}
	else {
		if(sel == status)
			bprintf(sel, "Cannot send text here.\n");
		else if(!srv)
			bprintf(sel, "Not connected.\n");
		else
			privmsg(sel->name, sel->cmdbuf);

		sel->cmdlen = sel->cmdoff = sel->histlnoff = 0;
		sel->cmdbuf[sel->cmdlen] = '\0';
		sel->need_redraw |= REDRAW_CMDLN;
	}
	free(buf);
}

void
cmdln_wdel(const Arg *arg) {
	int i;

	if(!sel->cmdoff)
		return;
	i = sel->cmdoff - 1;
	while(i && sel->cmdbuf[i] == ' ')
		--i;
	if(i && isalnum(sel->cmdbuf[i])) {
		while(--i && isalnum(sel->cmdbuf[i]));
		if(i)
			++i;
	}
	memmove(&sel->cmdbuf[i], &sel->cmdbuf[sel->cmdoff], sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= sel->cmdoff - i;
	sel->cmdbuf[sel->cmdlen] = '\0';
	sel->cmdoff = i;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
detach(Buffer *b) {
	Buffer **tb;

	for (tb = &buffers; *tb && *tb != b; tb = &(*tb)->next);
	*tb = b->next;
}

int
dial(char *host, char *port) {
	static struct addrinfo hints;
	struct addrinfo *res, *r;
	int srvfd;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if(getaddrinfo(host, port, &hints, &res) != 0)
		return -1;
	for(r = res; r; r = r->ai_next) {
		if((srvfd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1)
			continue;
		if(connect(srvfd, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(srvfd);
	}
	freeaddrinfo(res);
	if(!r)
		return -1;
	return srvfd;
}

void
die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(0);
}


void
draw(void) {
	printf(CURSOFF);
	if(sel->need_redraw & REDRAW_BAR)
		drawbar();
	if(sel->need_redraw & REDRAW_BUFFER)
		drawbuf();
	if(sel->need_redraw & REDRAW_CMDLN)
		drawcmdln();
	printf(CURPOS CURSON, rows, sel->cmdpos);
}

void
drawbar(void) {
	Buffer *b;
	char buf[512], tmp[256];
	int len, w, tmpw, tmplen;

	if(!(cols && rows))
		return;

	if(ISCHAN(sel)) {
		len = snprintf(tmp, sizeof tmp, "%d users in %s",
			sel->totnames, sel->name);
	}
	else {
		len = snprintf(tmp, sizeof tmp, "%s@%s",
			*nick ? nick : "[nick unset]",
			srv ? host : "[offline]");
	}
	if(sel->line)
		snprintf(&tmp[len], sizeof tmp - len, " [scrolled]");

	len = gcsfitcols(tmp, cols) - tmp;
	if(len >= sizeof buf)
		return;
	snprintf(buf, sizeof buf, "%.*s", len, tmp);
	w = gcswidth(buf, len - 1); /* -1 for null byte */

	for(b = buffers; b; b = b->next) {
		if(!b->notify)
			continue;

		snprintfc(tmp, sizeof tmp, " %C", colors[NickMention]);
		tmplen = strlen(tmp);
		snprintf(&tmp[tmplen], sizeof tmp - tmplen, "%s(%d)", b->name, b->notify);
		tmplen += gcsfitcols(&tmp[tmplen], cols) - &tmp[tmplen];
		snprintfc(&tmp[tmplen], sizeof tmp - tmplen, "%..0C");
		tmplen += strlen(&tmp[tmplen]);

		tmpw = gcswidth(tmp, tmplen - 1);
		if(len + tmplen >= sizeof buf)
			break;
		snprintf(&buf[len], sizeof buf - len, "%s", tmp);
		len += tmplen;
		w += tmpw;
	}

	mvprintf(1, 1, "%s%s", buf, w < cols ? CLEARRIGHT : "");
}

void
drawbuf(void) {
	int x, y, c, i, nb, nx;

	if(!(cols && rows && sel->len))
		return;
	x = rows - 2;
	y = sel->nlines - x;
	i = sel->line
		? sel->lnoff
		: bufinfo(sel->data, sel->len, 1 + (sel->nlines > x ? y : 0), LineToOffset);
	x = 1;
	y = 2;

	for(; i < sel->len; ++i) {
		/* control sequences (for colors) */
		if(sel->data[i] == 0x1B) {
			while(!isalpha(sel->data[i]))
				putchar(sel->data[i++]);
			putchar(sel->data[i]);
			continue;
		}

		c = sel->data[i];
		if(x <= cols) {
			if(c != '\n') {
				nb = UTF8BYTES(c);
				nx = x + gcswidth(&sel->data[i], 1);
				if(nx - 1 <= cols) {
					mvprintf(x, y, "%.*s", nb, &sel->data[i]);
					x = nx;
					i += nb - 1;
					continue;
				}
			}
			mvprintf(x, y, "%s", CLEARRIGHT);
		}

		x = 1;
		if(++y == rows)
			break;
		if(c != '\n') {
			nb = UTF8BYTES(c);
			mvprintf(x, y, "%.*s", nb, &sel->data[i]);
			x += gcswidth(&sel->data[i], 1);
			i += nb - 1;
		}
		if(x > cols && i < sel->len - 1 && sel->data[i + 1] == '\n')
			++i;
	}
	if(y < rows) {
		mvprintf(x, y, "%s", CLEARRIGHT);
		while(++y < rows)
			mvprintf(1, y, "%s", CLEARLN);
	}
}

void
drawcmdln(void) {
	char prompt[64], *buf, *p;
	int s, w; /* size and width */
	int x = 1, colw = cols;

	sel->cmdpos = 1;

	/* prompt */
	s = snprintf(prompt, sizeof prompt, "[%s] ", sel->name);
	w = gcswidth(prompt, colw - 1);
	if(w > 0) {
		s = gcsfitcols(prompt, colw - 1) - prompt;
		mvprintf(x, rows, "%.*s", s, prompt);
		x += w;
		colw -= w;
		sel->cmdpos += w;
	}

	/* buffer */
	for(p = buf = sel->cmdbuf; p < &sel->cmdbuf[sel->cmdoff]; p = gcsfitcols(p, colw)) {
		if(p != sel->cmdbuf && p == buf)
			break;
		buf = p;
	}
	w = gcswidth(buf, colw);

	/* leave room for the cursor */
	if(w >= colw && p == &sel->cmdbuf[sel->cmdoff]) {
		buf = p;
		w = gcswidth(buf, colw);
	}

	s = w ? gcsfitcols(buf, colw) - buf : 0;
	mvprintf(x, rows, "%.*s%s", s, buf, w < colw ? CLEARRIGHT : "");

	/* cursor position */
	for(p = buf; p < &sel->cmdbuf[sel->cmdoff]; p += UTF8BYTES(*p))
		sel->cmdpos += gcswidth(p, 1);
}

void *
ecalloc(size_t nmemb, size_t size) {
	void *p;

	if(!(p = calloc(nmemb, size)))
		die("Cannot allocate memory.\n");
	return p;
}

void
focus(Buffer *b) {
	if(b->notify)
		b->notify = 0;
	sel = b;
	sel->need_redraw |= REDRAW_ALL;
}

void
focusnext(const Arg *arg) {
	Buffer *nb;

	nb = sel->next ? sel->next : buffers;
	if(nb == sel)
		return;
	focus(nb);
}

void 
focusprev(const Arg *arg) {
	Buffer *b, *nb;

	nb = sel;
	if(sel == buffers) {
		for(b = buffers; b; b = b->next)
			nb = b;
	}
	else {
		for(b = buffers; b && b->next; b = b->next)
			if(b->next == sel)
				nb = b;
	}
	if(nb == sel)
		return;
	focus(nb);
}

void
freebuf(Buffer *b) {
	freenames(&b->names);
	free(b->hist);
	free(b->data);
	free(b);
}

void
freenames(Nick **names) {
	Nick *n;

	while((n = *names)) {
		*names = (*names)->next;
		free(n);
	}
}

char *
gcsfitcols(char *s, int maxw) {
	int w = 0;

	while(*s) {
		w += gcswidth(s, 1);
		if(w > maxw)
			break;
		s += UTF8BYTES(*s);
	}
	return s;
}

int
gcswidth(char *s, int len) {
	wchar_t *wcs = malloc(len * sizeof(wchar_t) + 1);
	int n;

	if(!wcs)
		return -1;
	n = mbstowcs(wcs, s, len);
	return n == -1 ? -1 : wcswidth(wcs, n);
}

Buffer *
getbuf(char *name) {
	Buffer *b;

	for(b = buffers; b; b = b->next)
		if(!strcmp(b->name, name))
			return b;
	return NULL;
}

/* XXX quick'n dirty implementation */
int
getkey(void) {
	int key = readchar(), c;

	if(key != '\x1b' || readchar() != '[') {
		switch(key) {
		case 127: key = KeyBackspace; break;
		}
		return key;
	}
	switch((c = readchar())) {
	case 'A': key = KeyUp; break;
	case 'B': key = KeyDown; break;
	case 'C': key = KeyRight; break;
	case 'D': key = KeyLeft; break;
	case 'H': key = KeyHome; break;
	case 'F': key = KeyEnd; break;
	case '1': key = KeyHome; break;
	case '3': key = KeyDel; break;
	case '4': key = KeyEnd; break;
	case '5': key = KeyPgUp; break;
	case '6': key = KeyPgDw; break;
	case '7': key = KeyHome; break;
	case '8': key = KeyEnd; break;
	}
	readchar();
	return key;
}

void
hangsup(void) {
	if(!srv)
		return;
	fclose(srv);
	srv = NULL;
}

void
history(const Arg *arg) {
	int nl, n, i;

	if(!sel->histsz)
		return;
	for(i = nl = 0; i < sel->histsz; i += strlen(&sel->hist[i]) + 2, ++nl);
	if(!sel->histlnoff) {
		if(sel->cmdlen)
			histpush(sel->cmdbuf, sel->cmdlen);
		sel->histlnoff = nl+1;
	}
	n = sel->histlnoff + arg->i;
	if(n < 1)
		n = 1;
	else if(n > nl)
		n = 0;
	sel->histlnoff = n;
	if(sel->histlnoff) {
		for(i = 0; i < sel->histsz && --n; i += strlen(&sel->hist[i]) + 2);
		sel->cmdlen = strlen(&sel->hist[i]);
		memcpy(sel->cmdbuf, &sel->hist[i], sel->cmdlen);
	}
	else {
		sel->cmdlen = 0;
	}
	sel->cmdoff = 0;
	sel->cmdbuf[sel->cmdlen] = '\0';
	sel->need_redraw |= REDRAW_CMDLN;
}

void
histpush(char *buf, int len) {
	int nl, i;

	/* do not clone unchanged lines */
	if(sel->histlnoff) {
		for(i = 0, nl = 1; i < sel->histsz && nl != sel->histlnoff; i += strlen(&sel->hist[i]) + 2, ++nl);
		if(!memcmp(&sel->hist[i], buf, len))
			return;
	}
	if(sel->histsz)
		++sel->histsz;
	i = sel->histsz;
	if(!(sel->hist = realloc(sel->hist, (sel->histsz += len + 1))))
		die("Cannot realloc\n");
	memcpy(&sel->hist[i], buf, len);
	sel->hist[i + len] = '\0';
}

void
logw(char *txt) {
	if(!logp)
		return;
	fprintf(logp, "%s", txt);
	fflush(logp);
}

int
mvprintf(int x, int y, char *fmt, ...) {
	va_list ap;
	int len;

	printf(CURPOS, y, x);
	va_start(ap, fmt);
	len = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return len;
}

Buffer *
newbuf(char *name) {
	Buffer *b;

	b = ecalloc(1, sizeof(Buffer));
	strncpy(b->name, name, sizeof b->name);
	attach(b);
	return b;
}

Nick *
nickadd(Buffer *b, char *name) {
	Nick *n;

	n = ecalloc(1, sizeof(Nick));
	strncpy(n->name, name, sizeof n->name - 1);
	n->len = strlen(n->name);

	/* attach */
	n->next = b->names;
	b->names = n;
	++b->totnames;
	if(b == sel)
		sel->need_redraw |= REDRAW_BAR;
	return n;
}

void
nickdel(Buffer *b, char *name) {
	Nick *n, **tn;

	if(!b)
		return;
	if(!(n = nickget(b, name)))
		return;
	/* detach */
	for(tn = &b->names; *tn && *tn != n; tn = &(*tn)->next);
	*tn = n->next;
	free(n);
	--b->totnames;
	if(b == sel)
		sel->need_redraw |= REDRAW_BAR;
}

Nick *
nickget(Buffer *b, char *name) {
	Nick *n;

	for(n = b->names; n; n = n->next)
		if(!strcmp(n->name, name))
			return n;
	return NULL;
}

void
nicklist(Buffer *b, char *list) {
	char *p, *np;

	freenames(&b->names);
	b->totnames = 0;
	for(p = list, np = skip(list, ' '); *p; p = np, np = skip(np, ' ')) {
		/* skip nick flags */
		switch(*p) {
		case '+':
		case '@':
		case '~':
		case '%':
		case '&':
			++p;
			break;
		}
		nickadd(b, p);
	}
}

void
nickmv(char *old, char *new) {
	Buffer *b;
	Nick *n;

	for(b = buffers; b; b = b->next) {
		n = nickget(b, old);
		if(!n)
			continue;
		strncpy(n->name, new, sizeof n->name - 1);
		n->len = strlen(n->name);
	}
}

void
parsecmd(char *cmd) {
	char *p, *tp;
	int i, len;

	p = &cmd[1]; /* skip the slash */
	if(!*p)
		return;
	tp = p + 1;
	tp = skip(p, ' ');
	len = strlen(p);
	for(i = 0; i < LENGTH(commands); ++i) {
		if(!strncmp(commands[i].name, p, len)) {
			commands[i].func(p, tp);
			return;
		}
	}
	if(srv)
		sout("%s %s", p, tp); /* raw */
	else
		bprintf(sel, "/%s: not connected.\n", p);
}

void
parsesrv(void) {
	char *cmd, *usr, *par, *txt;

	//bprintf(sel, "DEBUG | > | %s\n", bufin);
	cmd = bufin;
	usr = host;
	if(!cmd || !*cmd)
		return;
	if(cmd[0] == ':') {
		usr = cmd + 1;
		cmd = skip(usr, ' ');
		if(cmd[0] == '\0')
			return;
		skip(usr, '!');
	}
	skip(cmd, '\r');
	par = skip(cmd, ' ');
	txt = skip(par, ':');
	trim(txt);
	trim(par);
	for(int i = 0; i < LENGTH(messages); ++i) {
		if(!strcmp(messages[i].name, cmd)) {
			if(messages[i].func)
				messages[i].func(usr, par, txt);
			return;
		}
	}
	par = skip(par, ' ');
	bprintf(sel, "%s %s\n", par, txt);
}

void
privmsg(char *to, char *txt) {
	Buffer *b = getbuf(to);
	
	if(!b)
		b = isalpha(*to) ? newbuf(to) : sel;
	bprintf(b, "%s: %s\n", nick, txt);
	sout("PRIVMSG %s :%s", to, txt);
}

void
quit(char *msg) {
	if(!srv)
		return;
	sout("QUIT :%s", msg);
	hangsup();
}

int
readchar(void) {
	char buf[1] = {0};
	return (read(0, buf, 1) < 1 ? EOF : buf[0]);
}

void
recv_busynick(char *u, char *u2, char *u3) {
	char *n = skip(u2, ' ');

	bprintf(status, "%s is busy, choose a different /nick\n", n);
	sel->need_redraw |= REDRAW_BAR;
}

void
recv_join(char *who, char *chan, char *txt) {
	Buffer *b;

	if(!*chan)
		chan = txt;
	b = getbuf(chan);

	if(!strcmp(who, nick)) {
		if(!b)
			b = newbuf(chan);
		else
			b->kicked = 0; /* b may only be non-NULL due to this */
		sel = b;
		sel->need_redraw = REDRAW_ALL;
	}
	nickadd(b, who);
	bprintf(b, "%CJOIN%..0C %s\n", colors[IRCMessage], who);
}

void
recv_kick(char *oper, char *chan, char *who) {
	Buffer *b;

	who = skip(chan, ' ');
	b = getbuf(chan);
	if(!b)
		return;
	if(!strcmp(who, nick)) {
		b->kicked = 1;
		freenames(&b->names); /* we don't need this anymore */
		bprintf(b, "You got kicked from %s\n", chan);
	}
	else {
		bprintf(b, "%CKICK%..0C %s (%s)\n", colors[IRCMessage], who, oper);
		nickdel(b, who);
	}
}

void
recv_luserme(char *host, char *mynick, char *info) {
	bprintf(sel, "DEBUG LUSER: host=%s nick=%s info=%s\n", host, mynick, info);
	strcpy(nick, mynick);
	sel->need_redraw |= REDRAW_BAR;
}

void
recv_mode(char *u, char *val, char *u2) {
	if(*nick)
		return;
	strcpy(nick, val);
	sel->need_redraw |= REDRAW_BAR;
}

void
recv_motd(char *u, char *u2, char *txt) {
	bprintf(status, "%s\n", txt);
}

void
recv_names(char *host, char *par, char *names) {
	char *chan = skip(skip(par, ' '), ' '); /* skip user and symbol */
	Buffer *b = getbuf(chan);

	if(!b)
		b = status;
	bprintf(sel, "NAMES in %s: %s\n", chan, names);
	nicklist(b, names); /* keep as last since names is altered by skip() */
}

void
recv_namesend(char *host, char *par, char *names) {
	char *chan = skip(par, ' ');
	Buffer *b = getbuf(chan);

	if(!b)
		b = status;
	if(!b->names) {
		bprintf(sel, "No names in %s.\n", chan);
		return;
	}
	/* we don't actually need these on the status */
	if(b == status)
		freenames(&b->names);
}

void
recv_nick(char *who, char *u, char *upd) {
	Buffer *b;

	if(!strcmp(who, nick)) {
		strcpy(nick, upd);
		sel->need_redraw |= REDRAW_BAR;
		bprintf(sel, "You're now known as %s\n", upd);
	}
	for(b = buffers; b; b = b->next) {
		if(!nickget(b, who))
			continue;
		bprintf(b, "%CNICK%..0C %s: %s\n", colors[IRCMessage], who, upd);
	}
	nickmv(who, upd);
}

void
recv_notice(char *who, char *u, char *txt) {
	bprintf(sel, "%CNOTICE%..0C %s: %s\n", colors[IRCMessage], who, txt);
}

void
recv_part(char *who, char *chan, char *txt) {
	Buffer *b = getbuf(chan);

	/* cmd_close() destroys the buffer before PART is received */
	if(!b)
		return;
	if(!strcmp(who, nick)) {
		if(b == sel) {
			sel = sel->next ? sel->next : buffers;
			sel->need_redraw |= REDRAW_ALL;
		}
		detach(b);
		freebuf(b);
	}
	else {
		bprintf(b, "%CPART%..0C %s (%s)\n", colors[IRCMessage], who, txt);
		nickdel(b, who);
	}
}

void
recv_ping(char *u, char *u2, char *txt) {
	sout("PONG %s", txt);
}

void
recv_privmsg(char *from, char *to, char *txt) {
	Buffer *b;
	int mention, query;

	query = !strcmp(nick, to);
	mention = strstr(txt, nick) != NULL;

	if(query)
		to = from;
	b = getbuf(to);
	if(!b)
		b = newbuf(to);

	if(b != sel && (mention || query)) {
		++b->notify;
		sel->need_redraw |= REDRAW_BAR;
		if(NOTIFY_SCRIPT)
			spawn((const char *[]){ NOTIFY_SCRIPT, from, to, txt, NULL });
	}
	bprintf(b, "%C%s%..0C: %s\n", mention ? colors[NickMention] : colors[NickNormal], from, txt);
}

void
recv_quit(char *who, char *u, char *txt) {
	Buffer *b;

	for(b = buffers; b; b = b->next) {
		if(!nickget(b, who))
			continue;
		bprintf(b, "%CQUIT%..0C %s (%s)\n", colors[IRCMessage], who, txt);
		nickdel(b, who);
	}
}

void
recv_topic(char *who, char *chan, char *txt) {
	bprintf(getbuf(chan), "%s changed topic to %s\n", who, txt);
}

void
recv_topicrpl(char *usr, char *par, char *txt) {
	char *chan = skip(par, ' ');
	bprintf(sel, "Topic on %s is %s\n", chan, txt);
}

void
resize(int x, int y) {
	Buffer *b;

	rows = x;
	cols = y;
	for(b = buffers; b; b = b->next) {
		if(b->line && b->lnoff)
			b->lnoff = bufinfo(b->data, b->len, b->line, LineToOffset);
		b->nlines = bufinfo(b->data, b->len, 0, TotalLines);
	}
}

void
run(void) {
	Buffer *b;
	struct timeval tv;
	fd_set rd;
	int n, nfds;

	while(running) {
		FD_ZERO(&rd);
		FD_SET(0, &rd);
		tv.tv_sec = 120;
		tv.tv_usec = 0;
		nfds = 0;
		if(srv) {
			FD_SET(fileno(srv), &rd);
			nfds = fileno(srv);
		}
		n = select(nfds + 1, &rd, 0, 0, &tv);
		if(n < 0) {
			if(errno == EINTR)
				continue;
			die("select()\n");
		}
		if(n == 0) {
			if(srv) {
				if(time(NULL) - trespond >= 300) {
					hangsup();
					for(b = buffers; b; b = b->next)
						bprintf(b, "Connection timeout.\n");
				}
				else
					sout("PING %s", host);
			}
		}
		else {
			if(srv && FD_ISSET(fileno(srv), &rd)) {
				if(fgets(bufin, sizeof bufin, srv) == NULL) {
					hangsup();
					for(b = buffers; b; b = b->next)
						bprintf(b, "Remote host closed connection.\n");
				}
				else {
					trespond = time(NULL);
					parsesrv();
				}
			}
			if(FD_ISSET(0, &rd))
				usrin();
		}
		if(sel->need_redraw) {
			draw();
			sel->need_redraw = 0;
		}
	}
}

void
scroll(const Arg *arg) {
	int bufh = rows - 2;

	if(sel->nlines <= bufh)
		return;
	if(arg->i == 0) {
		sel->line = 0;
		sel->lnoff = 0;
		sel->need_redraw |= (REDRAW_BUFFER | REDRAW_BAR);
		return;
	}
	if(!sel->line)
		sel->line = sel->nlines - bufh + 1;
	sel->line += arg->i;
	if(sel->line < 1)
		sel->line = 1;
	else if(sel->line > sel->nlines - bufh)
		sel->line = 0;
	sel->lnoff = bufinfo(sel->data, sel->len, sel->line, LineToOffset);
	sel->need_redraw |= (REDRAW_BUFFER | REDRAW_BAR);
}

void
setup(void) {
	struct termios ti;
	struct sigaction sa;
	struct winsize ws;

	/* clean up any zombies immediately */
	sigchld(0);

	setlocale(LC_CTYPE, "");
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigwinch;
	sigaction(SIGWINCH, &sa, NULL);
	tcgetattr(0, &origti);
	cfmakeraw(&ti);
	ti.c_iflag |= ICRNL;
	ti.c_cc[VMIN] = 0;
	ti.c_cc[VTIME] = 0;
	tcsetattr(0, TCSAFLUSH, &ti);
	ioctl(0, TIOCGWINSZ, &ws);
	resize(ws.ws_row, ws.ws_col);
}

void
sigchld(int unused) {
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void
sigwinch(int unused) {
	struct winsize ws;

	ioctl(0, TIOCGWINSZ, &ws);
	resize(ws.ws_row, ws.ws_col);
	sel->need_redraw = REDRAW_ALL;
	printf(CLEAR);
	draw();
}

char *
skip(char *s, char c) {
	while(*s != c && *s != '\0')
		s++;
	if(*s != '\0')
		*s++ = '\0';
	return s;
}

void
sout(char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, sizeof bufout, fmt, ap);
	va_end(ap);
	fprintf(srv, "%s\r\n", bufout);
}

void
spawn(const char **cmd) {
	if(fork() == 0) {
		setsid();
		execvp(cmd[0], (char **)cmd);
		fprintf(stderr, "%s: execvp %s", argv0, cmd[0]);
		perror(" failed");
		exit(0);
	}
}

void
strip_ctrlseqs(char *s) {
	int i = 0;
	char *c;

	for(c = s; *c; ++c) {
		if(*c == 0x1B) {
			while(*++c && !isalpha(*c));
			continue;
		}
		s[i++] = *c;
	}
	s[i] = '\0';
}

void
trim(char *s) {
	char *e;

	e = s + strlen(s) - 1;
	while(isspace(*e) && e > s)
		e--;
	*(e + 1) = '\0';
}

void
usage(void) {
	die("Usage: %s [-v] [-hpnl <arg>]\n", argv0);
}

void
usrin(void) {
	char graph[4];
	int key, nb, i;

	key = getkey();
	for(i = 0; i < LENGTH(keys); ++i) {
		if(keys[i].key == key) {
			keys[i].func(&keys[i].arg);
			return;
		}
	}

	if(key >= KeyFirst && key <= KeyLast)
		return;
	if(iscntrl(key)) {
		while((key = readchar()) != EOF);
		return;
	}

	nb = UTF8BYTES(key);
	graph[0] = key;
	for(i = 1; i < nb; ++i) {
		key = readchar();
		if(key == EOF) {
			/* TODO: preserve the state and return */
			while((key = readchar()) == EOF);
		}
		graph[i] = key;
	}

	/* prevent overflow */
	if(sel->cmdlen + nb >= sizeof sel->cmdbuf)
		return;

	/* move nb bytes to the right */
	memmove(&sel->cmdbuf[sel->cmdoff+nb],
		&sel->cmdbuf[sel->cmdoff],
		sel->cmdlen - sel->cmdoff);

	/* insert nb bytes at current offset */
	memcpy(&sel->cmdbuf[sel->cmdoff], graph, nb);

	sel->cmdlen += nb;
	sel->cmdbuf[sel->cmdlen] = '\0';
	sel->cmdoff += nb;

	sel->need_redraw |= REDRAW_CMDLN;
}

char *
wordleft(char *str, int offset, int *size) {
	char *s = &str[offset], *e;

	while(s != str && (*s == ' ' || *s == '\0'))
		--s;
	if(!*s || *s == ' ')
		return NULL;
	while(s != str && *(s - 1) != ' ')
		--s;
	if(size) {
		for(e = s + 1; *e != '\0' && *e != ' '; ++e);
		*size = e - s;
	}
	return s;
}

int
main(int argc, char *argv[]) {
	const char *user = getenv("USER");

	ARGBEGIN {
	case 'h': strncpy(host, EARGF(usage()), sizeof host); break;
	case 'p': strncpy(port, EARGF(usage()), sizeof port); break;
	case 'n': strncpy(nick, EARGF(usage()), sizeof nick); break;
	case 'l': strncpy(logfile, EARGF(usage()), sizeof logfile); break;
	case 'v': die("circo-"VERSION"\n");
	default: usage();
	} ARGEND;

	if(!*nick)
		strncpy(nick, user ? user : "circo", sizeof nick);
	setup();
	if(*logfile)
		logp = fopen(logfile, "a");
	sel = status = newbuf("status");
	setbuf(stdout, NULL);
	sel->need_redraw = REDRAW_ALL;
	printf(CLEAR);
	draw();
	run();
	mvprintf(1, rows, "\n");
	cleanup();
	return 0;
}
