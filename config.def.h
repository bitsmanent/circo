/* See LICENSE file for copyright and license details. */

/* defaults */
char host[32] = "irc.libera.chat";
char port[8] = "6667";
char nick[32] = {0}; /* 0 means getenv("USER") */
char logfile[64] = "/tmp/circo.log";

/* Timestamp format; see strftime(3). */
#define TIMESTAMP_FORMAT "%d/%m %T | "

/* Used if no message is specified */
#define QUIT_MESSAGE "circo"

/* color scheme */
static int colors[ColorLast][5] = {
	[NickNormal] = {255, -1},
	[NickMention] = {5, 0, 1, -1},
	[IRCMessage] = {8, 0, -1},
};

/* available commands */
static Command commands[] = {
	/* command     function */
	{ "close",     cmd_close },
	{ "connect",   cmd_server },
	{ "exit",      cmd_exit },
	{ "msg",       cmd_msg },
	{ "quit",      cmd_quit },
	{ "server",    cmd_server },
	{ "topic",     cmd_topic },
	{ "rejoinall", cmd_rejoinall },
};

/* key definitions */
static Key keys[] = {
	/* key            function          argument */
        { CTRL('u'),      cmdln_clear,      {0} },
        { KeyBackspace,   cmdln_chldel,     {0} },
        { CTRL('d'),      cmdln_chrdel,     {0} },
        { CTRL('a'),      cmdln_cursor,     {.i = 0}},
        { CTRL('e'),      cmdln_cursor,     {.i = 999}},
        { CTRL('h'),      cmdln_cursor,     {.i = -1}},
        { KeyLeft,        cmdln_cursor,     {.i = -1}},
        { CTRL('l'),      cmdln_cursor,     {.i = +1}},
        { KeyRight,       cmdln_cursor,     {.i = +1}},
        { CTRL('w'),      cmdln_wdel,       {0} },
        { CTRL('n'),      focusnext,        {0} },
        { CTRL('p'),      focusprev,        {0} },
        { KeyPgUp,        scroll,           {.i = -20} },
        { KeyPgDw,        scroll,           {.i = +20} },
        { KeyEnd,         scroll,           {.i = 0} },
        { KeyUp,          history,          {.i = -1} },
        { KeyDown,        history,          {.i = +1} },
};
