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
#define ISCHANPFX(P)    ((P) == '#' || (P) == '&')
#define ISCHAN(B)       ISCHANPFX((B)->name[0])
#define BUFSZ           512

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
	char *hist;
	int size;
	int len;
	int line;
	int nlines;
	int lnoff;
	int cmdlen;
	int cmdoff;
	int cmdcur;
	int histsz;
	int histlnoff;
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
void freebuf(Buffer *b);
int getkey(void);
void hangsup(void);
void history(const Arg *arg);
void histpush(char *buf, int len);
void logw(char *txt);
int mvprintf(int x, int y, char *fmt, ...);
Buffer *newbuf(char *name);
void parsecmd(void);
void parsesrv(void);
void privmsg(char *to, char *txt);
void quit(char *msg);
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
char bufin[4096];
char bufout[4096];
struct termios origti;
time_t trespond;
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
	char buf[BUFSZ + 80]; /* 80 should be enough for the timestring... */
	time_t tm;
	int len = 0;

	tm = time(NULL);
        len = strftime(buf, sizeof(buf), TIMESTAMP_FORMAT, localtime(&tm));
	va_start(ap, fmt);
	len += vsnprintf(&buf[len], sizeof(buf) - len - 1, fmt, ap);
	va_end(ap);
	if(!b->size || b->len + len >= b->size)
		if(!(b->data = realloc(b->data, b->size += len + BUFSZ)))
			die("realloc()\n");
	memcpy(&b->data[b->len], buf, len);
	b->len += len;
	b->nlines = bufinfo(b->data, b->len, 0, TotalLines);
	sel->need_redraw |= REDRAW_BUFFER;
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
	if(srv && ISCHAN(b))
		sout("PART :%s", b->name);
	if(b == sel) {
		sel = sel->next ? sel->next : buffers;
		sel->need_redraw |= REDRAW_ALL;
	}
	detach(b);
	freebuf(b);
}

void
cmd_exit(char *cmd, char *s) {
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
cmd_quit(char *cmd, char *s) {
	Buffer *b;

	if(!srv) {
		bprintf(sel, "/%s: not connected.\n", cmd);
		return;
	}
	quit(s);
	for(b = buffers; b; b = b->next)
		bprintf(b, "Quit.\n");
}

void
cmd_rejoinall(char *cmd, char *s) {
	Buffer *b;

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
		quit(NULL);
	if(!*host)
		bprintf(status, "/%s: no host specified.\n", cmd);
	else if(!*port)
		bprintf(status, "/%s: no port specified.\n", cmd);
	else if((fd = dial(host, port)) < 0)
		bprintf(status, "Cannot connect to %s on port %s.\n", host, port);
	else {
		srv = fdopen(fd, "r+");
		printf(TTLSET, host);
		setbuf(srv, NULL);
		sout("NICK %s", nick);
		sout("USER %s localhost %s :%s", nick, host, nick);
	}
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
	if(!sel->cmdoff)
		return;
	if(sel->cmdoff < sel->cmdlen)
		memmove(&sel->cmd[sel->cmdoff - 1], &sel->cmd[sel->cmdoff],
			sel->cmdlen - sel->cmdoff);
	sel->cmd[--sel->cmdlen] = '\0';
	--sel->cmdoff;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_chrdel(const Arg *arg) {
	if(!sel->cmdlen)
		return;
	if(sel->cmdoff == sel->cmdlen) {
		--sel->cmdoff;
		sel->need_redraw |= REDRAW_CMDLN;
		return;
	}
	memmove(&sel->cmd[sel->cmdoff], &sel->cmd[sel->cmdoff + 1],
		sel->cmdlen - sel->cmdoff - 1);
	sel->cmd[--sel->cmdlen] = '\0';
	if(sel->cmdoff && sel->cmdoff == sel->cmdlen)
		--sel->cmdoff;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_clear(const Arg *arg) {
	if(!sel->cmdoff)
		return;
	memmove(sel->cmd, &sel->cmd[sel->cmdoff], sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= sel->cmdoff;
	sel->cmd[sel->cmdlen] = '\0';
	sel->cmdoff = 0;
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_cursor(const Arg *arg) {
	if(!arg->i) {
		sel->cmdoff = 0;
	}
	else {
		sel->cmdoff += arg->i;
		if(sel->cmdoff < 0) {
			sel->cmdoff = 0;
		}
		else if(sel->cmdoff > sel->cmdlen - 1) {
			sel->cmdoff = sel->cmdlen - 1;
			if(sel->cmdlen < sizeof sel->cmd - 1)
				++sel->cmdoff;
		}
	}
	sel->need_redraw |= REDRAW_CMDLN;
}

void
cmdln_wdel(const Arg *arg) {
	int i;

	if(!sel->cmdoff)
		return;
	i = sel->cmdoff - 1;
	while(i && sel->cmd[i] == ' ')
		--i;
	if(i && isalnum(sel->cmd[i])) {
		while(--i && isalnum(sel->cmd[i]));
		if(i)
			++i;
	}
	memmove(&sel->cmd[i], &sel->cmd[sel->cmdoff], sel->cmdlen - sel->cmdoff);
	sel->cmdlen -= sel->cmdoff - i;
	sel->cmd[sel->cmdlen] = '\0';
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
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
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
	printf(CURPOS CURSON, rows, sel->cmdcur);
}

void
drawbar(void) {
	char buf[cols+1];
	int len;

	if(!(cols && rows))
		return;
	len = snprintf(buf, sizeof buf, "%s@%s:%s - %s%s",
		nick, srv ? host : "", srv ? port : "",
		sel->name, sel->line ? " [scrolled]" : "");
	mvprintf(1, 1, "%s%s", buf, len < cols ? CLEARRIGHT : "");
}

void
drawbuf(void) {
	int x, y, c, i;

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
}

void
drawcmdln(void) {
	char buf[cols+1];
	char prompt[64];
	int pslen, cmdsz, cur, i, len;

	if(!(cols && rows))
		return;
	pslen = snprintf(prompt, sizeof prompt, "[%s] ", sel->name);
	cmdsz = pslen < cols ? cols - pslen : 0;
	if(cmdsz) {
		cur = pslen + (sel->cmdoff % cmdsz) + 1;
		i = cmdsz * (sel->cmdoff / cmdsz);
	}
	else {
		cur = cols;
		i = 0;
	}
	sel->cmdcur = cur;
	len = snprintf(buf, sizeof buf, "%s%s", prompt, &sel->cmd[i]);
	mvprintf(1, rows, "%s%s", buf, len < cols ? CLEARRIGHT : "");
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
	Buffer *nb;

	nb = sel->next ? sel->next : buffers;
	if(nb == sel)
		return;
	sel = nb;
	sel->need_redraw |= REDRAW_ALL;
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
	sel = nb;
	sel->need_redraw |= REDRAW_ALL;
}

void
freebuf(Buffer *b) {
	free(b->hist);
	free(b->data);
	free(b);
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
			histpush(sel->cmd, sel->cmdlen);
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
		memcpy(sel->cmd, &sel->hist[i], sel->cmdlen);
	}
	else {
		sel->cmdlen = 0;
	}
	sel->cmdoff = 0;
	sel->cmd[sel->cmdlen] = '\0';
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
	i = sel->histsz;
	if(sel->histsz)
		i = ++sel->histsz;
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

void
parsecmd(void) {
	char *p, *tp;
	int i, len;

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
		bprintf(sel, "/%s: not connected.\n", p);
}

void
parsesrv(void) {
	char *cmd, *usr, *par, *txt;

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
	if(srv)
		sout("QUIT :%s", msg ? msg : QUIT_MESSAGE);
	hangsup();
}

void
recv_busynick(char *u, char *u2, char *u3) {
	bprintf(status, "%s is busy, choose a different /nick\n", nick);
	*nick = '\0';
	sel->need_redraw |= REDRAW_BAR;
}

void
recv_join(char *who, char *chan, char *txt) {
	Buffer *b;

	if(!*chan)
		chan = txt;
	b = getbuf(chan);
	if(!b) {
		if(strcmp(who, nick)) /* this should never happen here */
			return;
		b = newbuf(chan);
		if(!b) /* malformed message */
			return;
		sel = b;
	}
	bprintf(b, "JOIN %s\n", who);
	if(b == sel)
		sel->need_redraw |= REDRAW_ALL;
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
recv_nick(char *who, char *u, char *txt) {
	if(!strcmp(who, nick)) {
		strcpy(nick, txt);
		sel->need_redraw |= REDRAW_BAR;
	}
	bprintf(sel, "NICK %s: %s\n", who, txt);
}

void
recv_notice(char *who, char *u, char *txt) {
	/* XXX redirect to the relative buffer? */
	bprintf(sel, "NOTICE: %s: %s\n", who, txt);
}

void
recv_part(char *who, char *chan, char *txt) {
	Buffer *b = getbuf(chan);

	/* cmd_close() destroys the buffer before PART is received */
	if(!b)
		return;
	if(strcmp(who, nick)) {
		bprintf(b, "PART %s %s\n", who, txt);
		return;
	}
	if(b == sel) {
		sel = sel->next ? sel->next : buffers;
		sel->need_redraw |= REDRAW_ALL;
	}
	detach(b);
	freebuf(b);
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
	bprintf(b, "%s: %s\n", from, txt);
}

void
recv_quit(char *who, char *u, char *txt) {
	bprintf(sel, "QUIT %s (%s)\n", who, txt);
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
recv_users(char *usr, char *par, char *txt) {
	char *chan = skip(par, '@') + 1;
	bprintf(sel, "Users in %s: %s\n", chan, txt);
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
	Buffer *b;
	int key = getkey(), i;

	for(i = 0; i < LENGTH(keys); ++i) {
		if(keys[i].key == key) {
			keys[i].func(&keys[i].arg);
			while(getkey() != -1); /* discard remaining input */
			return;
		}
	}
	do {
		if(key == '\n') {
			b = sel;
			logw(sel->cmd);
			if(sel->cmd[0] == '\0')
				return;
			if(sel->cmd[0] == '/') {
				if(sel->cmdlen == 1)
					return;
				histpush(sel->cmd, sel->cmdlen);
				/* Note: network latency may delay drawings
				 * causing visual glitches. */
				parsecmd();
			}
			else {
				histpush(sel->cmd, sel->cmdlen);
				if(sel == status)
					bprintf(sel, "Cannot send text here.\n");
				else if(!srv)
					bprintf(sel, "Not connected.\n");
				else
					privmsg(sel->name, sel->cmd);
			}
			if(b == sel) {
				b->cmd[sel->cmdlen = sel->cmdoff = sel->histlnoff = 0] = '\0';
				b->need_redraw |= REDRAW_CMDLN;
			}
		}
		else if(isgraph(key) || (key == ' ' && sel->cmdlen)) {
			if(sel->cmdlen == sizeof sel->cmd - 1) {
				sel->cmd[sel->cmdoff] = key;
				sel->need_redraw |= REDRAW_CMDLN;
				return;
			}
			memmove(&sel->cmd[sel->cmdoff+1], &sel->cmd[sel->cmdoff],
				sel->cmdlen - sel->cmdoff);
			sel->cmd[sel->cmdoff] = key;
			sel->cmd[++sel->cmdlen] = '\0';
			if(sel->cmdlen < sizeof sel->cmd - 1)
				++sel->cmdoff;
			sel->need_redraw |= REDRAW_CMDLN;
		}
	} while((key = getkey()) != -1);
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
	printf(TTLSET, "circo");
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
	printf(TTLSET, "");
	return 0;
}
