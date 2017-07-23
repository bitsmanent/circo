/* See LICENSE file for copyright and license details. */

/* Timestamp format; see strftime(3). */
#define TIMESTAMP_FORMAT "%Y-%m-%d %T | "

Command commands[] = {
	/* command     function */
	{ "close",     cmd_close },
	{ "connect",   cmd_server },
	{ "msg",       cmd_msg },
	{ "quit",      cmd_quit },
	{ "server",    cmd_server },
	{ "topic",     cmd_topic },
};

/* key definitions */
static Key keys[] = {
	/* key            function          argument */
        { CTRL('u'),      cmdln_clear,      {0} },
        { KeyBackspace,   cmdln_chldel,     {0} },
        { CTRL('d'),      cmdln_chrdel,     {0} },
        { CTRL('w'),      cmdln_wdel,       {0} },
        { CTRL('a'),      cmdln_cursor,     {.i = 0}},
        { CTRL('e'),      cmdln_cursor,     {.i = 999}},
        { CTRL('h'),      cmdln_cursor,     {.i = -1}},
        { KeyLeft,        cmdln_cursor,     {.i = -1}},
        { CTRL('l'),      cmdln_cursor,     {.i = +1}},
        { KeyRight,       cmdln_cursor,     {.i = +1}},
        { CTRL('n'),      focusnext,        {0} },
        { CTRL('p'),      focusprev,        {0} },
        { KeyPgUp,        scroll,           {.i = -1} },
        { KeyPgDw,        scroll,           {.i = +1} },
        { KeyEnd,         scroll,           {.i = 0} },
};
