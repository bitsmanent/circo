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
	/* key         function          argument */
        { CTRL('u'),   editor_clear,     {0} },
        { KeyBackspace,editor_chdel,     {.i = -1} },
        { CTRL('a'),   editor_cursor,    {.i = 0}},
        { CTRL('e'),   editor_cursor,    {.i = 999}},
        { CTRL('h'),   editor_cursor,    {.i = -1}},
        { KeyLeft,     editor_cursor,    {.i = -1}},
        { CTRL('l'),   editor_cursor,    {.i = +1}},
        { KeyRight,    editor_cursor,    {.i = +1}},
        { CTRL('n'),   focusnext,        {0} },
        { CTRL('p'),   focusprev,        {0} },
        { KeyPgUp,     scroll,		 {.i = -1} },
        { KeyPgDw,     scroll,		 {.i = +1} },
        { KeyEnd,      scroll,		 {.i = 0} },
};
