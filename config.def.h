/* See LICENSE file for copyright and license details. */

Command commands[] = {
	/* command     function */
	{ "connect",   cmd_server },
	{ "msg",       cmd_msg },
	{ "quit",      cmd_quit },
	{ "server",    cmd_server },
};

/* key definitions */
static Key keys[] = {
	/* key            function          argument */
        { CTRL('u'),      cmdln_clear,      {0} },
        { CTRL('h'),      cmdln_chdel,      {.i = -1} },
        { CTRL('a'),      cmdln_cursor,     {.i = 0}},
        { CTRL('e'),      cmdln_cursor,     {.i = 999}},
        { CTRL('l'),      cmdln_cursor,     {.i = -1}},
        { KeyLeft,        cmdln_cursor,     {.i = -1}},
        { CTRL('l'),      cmdln_cursor,     {.i = +1}},
        { KeyRight,       cmdln_cursor,     {.i = +1}},
        { CTRL('n'),      focusnext,        {0} },
        { CTRL('p'),      focusprev,        {0} },
        { KeyPgUp,        scroll,           {.i = -1} },
        { KeyPgDw,        scroll,           {.i = +1} },
        { KeyEnd,         scroll,           {.i = 0} },
};
