/* claudio's IRC oasis */

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
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "arg.h"
char *argv0;

/* macros */
#define LENGTH(X)       (sizeof X / sizeof X[0])
#define BUFSZ           512

/* VT100 escape sequences */
#define CLEAR           "\33[2J"
#define CLEARLN         "\33[2K"
#define CLEARRIGHT      "\33[0K"
#define CURPOS          "\33[%d;%dH"
#define CURSON          "\33[?25h"
#define CURSOFF         "\33[?25l"
#define TTLSET          "\33]0;%s\007"

#if defined CTRL && defined _AIX
  #undef CTRL
#endif
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

/* enums */
enum { KeyUp = -50, KeyDown, KeyRight, KeyLeft, KeyHome, KeyEnd, KeyDel, KeyPgUp, KeyPgDw, KeyBackspace };
enum { LineToOffset, OffsetToLine, TotalLines }; /* bufinfo() flags */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Buffer Buffer;
struct Buffer {
	char *data;
	char name[256];
	char cmd[BUFSZ];
	int size;
	int len;
	int line;
	int nlines;
	int lnoff;
	int cmdlen;
	int cmdoff;
	int need_redraw;
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
int bufinfo(char *buf, int len, int val, int act);
void cleanup(void);
void cmd_close(char *cmd, char *s);
void cmd_msg(char *cmd, char *s);
void cmd_quit(char *cmd, char *s);
void cmd_server(char *cmd, char *s);
void cmd_topic(char *cmd, char *s);
void cmdln_chldel(const Arg *arg);
void cmdln_chrdel(const Arg *arg);
void cmdln_clear(const Arg *arg);
void cmdln_cursor(const Arg *arg);
void cmdln_wdel(const Arg *arg);
void detach(Buffer *b);
int dial(char *host, char *port);
void die(const char *errstr, ...);
void draw(void);
void drawbar(void);
void drawbuf(void);
void drawcmdln(void);
void *ecalloc(size_t nmemb, size_t size);
Buffer *getbuf(char *name);
void focusnext(const Arg *arg);
void focusprev(const Arg *arg);
int getkey(void);
void logw(char *txt);
int mvprintf(int x, int y, char *fmt, ...);
Buffer *newbuf(char *name);
void parsecmd(void);
void parsesrv(void);
int printb(Buffer *b, char *fmt, ...);
void privmsg(char *to, char *txt);
void recv_busynick(char *u, char *u2, char *u3);
void recv_join(char *who, char *chan, char *txt);
void recv_mode(char *u, char *val, char *u2);
void recv_motd(char *u, char *u2, char *txt);
void recv_nick(char *who, char *u, char *txt);
void recv_notice(char *who, char *u, char *txt);
void recv_part(char *who, char *chan, char *txt);
void recv_ping(char *u, char *u2, char *txt);
void recv_privmsg(char *from, char *to, char *txt);
void recv_quit(char *who, char *u, char *txt);
void recv_topic(char *who, char *chan, char *txt);
void recv_topicrpl(char *usr, char *par, char *txt);
void recv_users(char *usr, char *par, char *txt);
void resize(int x, int y);
void scroll(const Arg *arg);
void setup(void);
void sigwinch(int unused);
char *skip(char *s, char c);
void sout(char *fmt, ...);
void trim(char *s);
void usage(void);
void usrin(void);

/* variables */
FILE *srv, *logp;
Buffer *buffers, *status, *sel;
struct termios origti;
int running = 1;
int rows, cols;

Message messages[] = {
	{ "JOIN",    recv_join },
	{ "MODE",    recv_mode },
	{ "NICK",    recv_nick },
	{ "NOTICE",  recv_notice },
	{ "PART",    recv_part },
	{ "PING",    recv_ping },
	{ "PRIVMSG", recv_privmsg },
	{ "QUIT",    recv_quit },
	{ "TOPIC",   recv_topic },
	{ "331",     recv_topicrpl }, /* no topic set */
	{ "332",     recv_topicrpl },
	{ "353",     recv_users },
	{ "372",     recv_motd },
	{ "437",     recv_busynick },

	/* ignored */
	{ "PONG",    NULL },
	{ "470",     NULL }, /* channel forward */

};

char *host = "irc.freenode.org";
char *port = "6667";
char logfile[64] = "/tmp/circo.log";
char nick[32];

/* configuration, allows nested code to access above variables */
#include "config.h"

/* function implementations */
void
attach(Buffer *b) {
	b->next = buffers;
	buffers = b;
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
	/* TotalLines */
	return y - 1;
}

void
cleanup(void) {
	Buffer *b;

	while(buffers) {
		b = buffers;
		buffers = buffers->next;
		free(b->data);
		free(b);
	}
	tcsetattr(0, TCSANOW, &origti);
}

void
cmd_close(char *cmd, char *s) {
	Buffer *b;

	if(*s) {
		b = getbuf(s);
	}
	else {
		b = sel;
		s = b->name;
	}
	if(!b) {
		printb(status, "%s: unknown buffer.\n", s);
		return;
	}
	if(b == status) {
		printb(status, "Cannot close the status.\n");
		return;
	}
	if(*s == '#' || *s == '&')
		sout("PART :%s", s);
	if(b == sel)
		sel = sel->next ? sel->next : buffers;
	detach(b);
	free(b);
}

void
cmd_msg(char *cmd, char *s) {
	char *to, *txt;

	if(!srv) {
		printb(sel, "You're offline.\n");
		return;
	}
	to = s;
	txt = skip(to, ' ');
	privmsg(to, txt);
}

void
cmd_quit(char *cmd, char *s) {
	if(srv)
		sout("QUIT%s%s", s ? " :" : "", s ? s : "");
	running = 0;
}

void
cmd_server(char *cmd, char *s) {
	char *h, *p;

	p = skip(s, ' ');
	h = s;
	if(!*h)
		h = host;
	if(!*p)
		p = port;
	if(*skip(p, ' ')) {
		printb(status, "Usage: /%s [host] [port]\n", cmd);
		return;
	}
	if(srv)
		sout("QUIT");
	srv = fdopen(dial(h, p), "r+");
	if(!srv) {
		printb(status, "Cannot connect to %s on port %s\n", h, p);
		return;
	}
	setbuf(srv, NULL);
	sout("NICK %s", nick);
	sout("USER %s localhost %s :%s", nick, h, nick);
}

void
cmd_topic(char *cmd, char *s) {
	char *chan, *txt;

	if(!srv) {
		printb(sel, "You're offline.\n");
		return;
	}
	if(!*s) {
		if(sel->name[0] == '#' || sel->name[0] == '&')
			sout("TOPIC %s", sel->name);
		return;
	}
	if(*s == '#' || *s == '&') {
		chan = s;
		txt = skip(s, ' ');
		if(!*txt) {
			sout("TOPIC %s", chan);
			return;
		}
	}
	else {
		if(sel == status) {
			printb(sel, "Usage: /%s [channel] [text]\n", cmd);
			return;
		}
		chan = sel->name;
		txt = s;
	}
	sout("TOPIC %s :%s", chan, txt);
}

void
cmdln_chldel(const Arg *arg) {
	if(!sel->cmdoff)
		return;
	if(sel->cmdoff < sel->cmdlen)
		memmove(&sel->cmd[sel->cmdoff - 1], &sel->cmd[sel->cmdoff],
			sel->cmdlen - sel->cmdoff);
	sel->cmd[--sel->cmdlen] = '\0';
	--sel->cmdoff;
	sel->need_redraw = 1;
}

void
cmdln_chrdel(const Arg *arg) {
	if(!sel->cmdlen)
		return;
	if(sel->cmdoff == sel->cmdlen) {
		--sel->cmdoff;
		sel->need_redraw = 1;
		return;
	}
	memmove(&sel->cmd[sel->cmdoff], &sel->cmd[sel->cmdoff + 1],
		sel->cmdlen - sel->cmdoff - 1);
	sel->cmd[--sel->cmdlen] = '\0';
	if(sel->cmdoff && sel->cmdoff == sel->cmdlen)
		--sel->cmdoff;
	sel->need_redraw = 1;
}

void
cmdln_clear(const Arg *arg) {
	if(!sel->cmdoff)
		return;
	memmove(sel->cmd, &sel->cmd[sel->cmdoff], sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= sel->cmdoff;
	sel->cmd[sel->cmdlen] = '\0';
	sel->cmdoff = 0;
	sel->need_redraw = 1;
}

void
cmdln_cursor(const Arg *arg) {
	if(!arg->i) {
		sel->cmdoff = 0;
	}
	else {
		sel->cmdoff += arg->i;
		if(sel->cmdoff < 0)
			sel->cmdoff = 0;
		else if(sel->cmdoff > sel->cmdlen)
			sel->cmdoff = sel->cmdlen;
	}
	sel->need_redraw = 1;
}

void
cmdln_wdel(const Arg *arg) {
	int i;

	if(!sel->cmdoff)
		return;
	i = sel->cmdoff - 1;
	while(i && sel->cmd[i] == ' ')
		--i;
	while(i && sel->cmd[i] != ' ')
		--i;
	if(i)
		++i;
	memmove(&sel->cmd[i], &sel->cmd[sel->cmdoff], sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= sel->cmdoff - i;
	sel->cmd[sel->cmdlen] = '\0';
	sel->cmdoff = i;
	sel->need_redraw = 1;
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
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void
draw(void) {
	drawbuf();
	drawbar();
	drawcmdln();
}

void
drawbar(void) {
	/* XXX truncate to cols */
	mvprintf(1, 1, "%s@%s:%s - %s%s%s",
		srv ? nick : "", srv ? host : "", srv ? port : "",
		sel->name, sel->line ? " [scrolled]" : "", CLEARRIGHT);
}

void
drawbuf(void) {
	int x, y, c, i;

	if(!sel->len)
		return;
	x = rows - 2;
	y = sel->nlines - x;
	i = sel->line
		? sel->lnoff
		: bufinfo(sel->data, sel->len, 1 + (sel->nlines > x ? y : 0), LineToOffset);
	x = 1;
	y = 2;
	printf(CURSOFF);
	for(; i < sel->len; ++i) {
		c = sel->data[i];
		if(c != '\n' && x <= cols) {
			x += mvprintf(x, y, "%c", c);
			continue;
		}
		if(c == '\n' && x <= cols)
			mvprintf(x, y, "%s", CLEARRIGHT);
		x = 1;
		if(++y == rows)
			break;
		if(c != '\n')
			x += mvprintf(x, y, "%c", c);
		if(x > cols && i < sel->len - 1 && sel->data[i + 1] == '\n')
			++i;
	}
	if(y < rows) {
		mvprintf(x, y, "%s", CLEARRIGHT);
		while(++y < rows)
			mvprintf(1, y, "%s", CLEARLN);
	}
	printf(CURSON);
}

void
drawcmdln(void) {
	int pslen = 4 + strlen(sel->name); /* "[%s] " + 1 for the cursor */
	int cmdsz = cols - pslen;
	int i = sel->cmdlen > cmdsz ? sel->cmdlen - cmdsz : 0;

	mvprintf(1, rows, "[%s] %s%s", sel->name, &sel->cmd[i], CLEARRIGHT);
	printf(CURPOS, rows, pslen + (sel->cmdoff - i));
}

void *
ecalloc(size_t nmemb, size_t size) {
	void *p;

	if(!(p = calloc(nmemb, size)))
		die("Cannot allocate memory.\n");
	return p;
}

void
focusnext(const Arg *arg) {
	sel = sel->next ? sel->next : buffers;
	sel->need_redraw = 1;
}

void 
focusprev(const Arg *arg) {
	Buffer *b;

	if(sel == buffers) {
		for(b = buffers; b; b = b->next)
			sel = b;
	}
	else {
		for(b = buffers; b && b->next; b = b->next)
			if(b->next == sel)
				sel = b;
	}
	sel->need_redraw = 1;
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
	int key = getchar(), c;

	if(key != '\x1b' || getchar() != '[') {
		switch(key) {
		case 127: key = KeyBackspace; break;
		}
		return key;
	}
	switch((c = getchar())) {
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
	return key;
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

void
parsecmd(void) {
	char *p, *tp;
	int i, len;

	/* command */
	p = &sel->cmd[1];
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
		sout("%s %s", p, tp);
	else
		printb(sel, "/%s: not connected.\n", p);
}

void
parsesrv(void) {
	char *cmd, *usr, *par, *txt;
	char buf[BUFSZ];

	if(fgets(buf, sizeof buf, srv) == NULL) {
		srv = NULL;
		printb(sel, "! Remote host closed connection.\n");
		return;
	}
	//printb(sel, "DEBUG | buf=%s\n", buf);
	cmd = buf;
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
	//printb(sel, "DEBUG | cmd=%s nick=%s par=%s usr=%s txt=%s\n", cmd, nick, par, usr, txt);
	for(int i = 0; i < LENGTH(messages); ++i) {
		if(!strcmp(messages[i].name, cmd)) {
			if(messages[i].func)
				messages[i].func(usr, par, txt);
			return;
		}
	}
	par = skip(par, ' ');
	printb(sel, "%s %s\n", par, txt);
}

int
printb(Buffer *b, char *fmt, ...) {
	va_list ap;
	char buf[BUFSZ + 80]; /* 80 should be enough for the timestring... */
	time_t tm;
	int len = 0;

	tm = time(NULL);
        len = strftime(buf, sizeof buf - len, TIMESTAMP_FORMAT, localtime(&tm));
	va_start(ap, fmt);
	len += vsnprintf(&buf[len], sizeof(buf) - len, fmt, ap);
	va_end(ap);
	if(!b->size || b->len >= b->size)
		if(!(b->data = realloc(b->data, b->size += len))) /* XXX optimize */
			die("cannot realloc\n");
	memcpy(&b->data[b->len], buf, len);
	b->len += len;
	b->nlines = bufinfo(b->data, b->len, 0, TotalLines);
	b->need_redraw = 1;
	logw(buf);
	return len;
}

void
privmsg(char *to, char *txt) {
	Buffer *b = getbuf(to);
	
	if(!b)
		b = isalpha(*to) ? newbuf(to) : sel;
	printb(b, "%s: %s\n", nick, txt);
	sout("PRIVMSG %s :%s", to, txt);
}

void
recv_busynick(char *u, char *u2, char *u3) {
	printb(status, "%s is busy, choose a different /nick\n", nick);
	*nick = '\0';
}

void
recv_join(char *who, char *chan, char *txt) {
	Buffer *b;

	if(!strcmp(who, nick)) {
		sel = newbuf(chan);
		b = sel;
	}
	else {
		b = getbuf(chan);
	}
	printb(b, "JOIN %s\n", who);
	if(b == sel)
		sel->need_redraw = 1;
}

void
recv_mode(char *u, char *val, char *u2) {
	if(*nick)
		return;
	strcpy(nick, val);
}

void
recv_motd(char *u, char *u2, char *txt) {
	printb(status, "%s\n", txt);
}

void
recv_nick(char *who, char *u, char *txt) {
	if(!strcmp(who, nick))
		strcpy(nick, txt);
	printb(sel, "NICK %s: %s\n", who, txt);
}

void
recv_notice(char *who, char *u, char *txt) {
	/* XXX redirect to the relative buffer, if possible */
	printb(sel, "NOTICE: %s: %s\n", who, txt);
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
			sel->need_redraw = 1;
		}
		detach(b);
		free(b);
	}
	else {
		printb(b, "PART %s %s\n", who, txt);
	}
}

void
recv_ping(char *u, char *u2, char *txt) {
	sout("PONG %s", txt);
}

void
recv_privmsg(char *from, char *to, char *txt) {
	Buffer *b;

	if(!strcmp(nick, to))
		to = from;
	b = getbuf(to);
	if(!b)
		b = newbuf(to);
	printb(b, "%s: %s\n", from, txt);
}

void
recv_quit(char *who, char *u, char *txt) {
	printb(sel, "QUIT %s (%s)\n", who, txt);
}

void
recv_topic(char *who, char *chan, char *txt) {
	printb(getbuf(chan), "%s changed topic to %s\n", who, txt);
}

void
recv_topicrpl(char *usr, char *par, char *txt) {
	char *chan = skip(par, ' ');
	printb(sel, "Topic on %s is %s\n", chan, txt);
}

void
recv_users(char *usr, char *par, char *txt) {
	char *chan = skip(par, '@') + 1;
	printb(sel, "Users in %s: %s\n", chan, txt);
}

void
resize(int x, int y) {
	rows = x;
	cols = y;
	if(sel) {
		if(sel->line && sel->lnoff)
			sel->lnoff = bufinfo(sel->data, sel->len, sel->line, LineToOffset);
		sel->nlines = bufinfo(sel->data, sel->len, 0, TotalLines);
		draw();
	}
}

void
scroll(const Arg *arg) {
	if(!sel->len)
		return;
	if(arg->i == 0) {
		sel->line = 0;
		sel->lnoff = 0;
		sel->need_redraw = 1;
		return;
	}
	if(!sel->line)
		sel->line = sel->nlines;
	sel->line += arg->i;
	if(sel->line < 1)
		sel->line = 1;
	else if(sel->line > sel->nlines)
		sel->line = sel->nlines;
	sel->lnoff = bufinfo(sel->data, sel->len, sel->line, LineToOffset);
	sel->need_redraw = 1;
}

void
setup(void) {
	struct termios ti;
	struct sigaction sa;
	struct winsize ws;

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
sigwinch(int unused) {
	struct winsize ws;

	ioctl(0, TIOCGWINSZ, &ws);
	resize(ws.ws_row, ws.ws_col);
	sel->need_redraw = 1;
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
	char buf[BUFSZ];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	fprintf(srv, "%s\r\n", buf);
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
	die("Usage: %s ...\n", argv0);
}

void
usrin(void) {
	int key = getkey(), i;

	for(i = 0; i < LENGTH(keys); ++i)
		if(keys[i].key == key)
			break;
	if(i < LENGTH(keys)) {
		keys[i].func(&keys[i].arg);
		while(getkey() != EOF); /* discard remaining input */
		return;
	}
	if(key == '\n') {
		logw(sel->cmd);
		if(sel->cmd[0] == '\0')
			return;
		if(sel->cmd[0] == '/') {
			if(sel->cmdlen == 1)
				return;
			parsecmd();
		}
		else {
			if(sel == status)
				printb(sel, "Cannot send text here.\n");
			else if(!srv)
				printb(sel, "You're not connected.\n");
			else
				privmsg(sel->name, sel->cmd);
		}
		sel->cmd[sel->cmdlen = sel->cmdoff = 0] = '\0';
		sel->need_redraw = 1;
	}
	else if(isgraph(key) || (key == ' ' && sel->cmdlen)) {
		if(sel->cmdlen == sizeof sel->cmd)
			return;
		memmove(&sel->cmd[sel->cmdoff+1], &sel->cmd[sel->cmdoff],
			sel->cmdlen - sel->cmdoff);
		sel->cmd[sel->cmdoff] = key;
		sel->cmd[++sel->cmdlen] = '\0';
		if(sel->cmdoff < sizeof sel->cmd - 1)
			++sel->cmdoff;
		sel->need_redraw = 1;
	}
}

int
main(int argc, char *argv[]) {
	struct timeval tv;
	fd_set rd;
	int n, nfds;
	const char *user = getenv("USER");

	ARGBEGIN {
	case 'h':
		host = EARGF(usage());
		break;
	case 'p':
		port = EARGF(usage());
		break;
	case 'n':
		strncpy(nick, EARGF(usage()), sizeof nick);
		break;
	case 'l':
		strncpy(logfile, EARGF(usage()), sizeof logfile);
		break;
	case 'v':
		die("foo..");
	default:
		usage();
	} ARGEND;

	if(!*nick)
		strncpy(nick, user ? user : "unknown", sizeof nick);
	printf(TTLSET, "circo");
	setup();
	if(*logfile)
		logp = fopen(logfile, "a");
	sel = status = newbuf("status");
	setbuf(stdout, NULL);
	printf(CLEAR);
	draw();
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
			die("circo: error on select()\n");
		}
		else if(n == 0) {
			if(srv)
				sout("PING %s", host);
			continue;
		}
		if(srv && FD_ISSET(fileno(srv), &rd))
			parsesrv();
		if(FD_ISSET(0, &rd))
			usrin();
		if(sel->need_redraw) {
			draw();
			sel->need_redraw = 0;
		}
	}
	mvprintf(1, cols, "\n");
	cleanup();
	return 0;
}
