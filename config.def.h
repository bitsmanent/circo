/* See LICENSE file for copyright and license details. */

Command commands[] = {
	{ "connect", cmd_server },
	{ "msg", cmd_msg },
	{ "quit", cmd_quit },
	{ "server", cmd_server },
};

/* key definitions */
static Key keys[] = {
	/* key         function          argument */
        { CTRL('u'),   editor_clear,     {0} },
        { KeyBackspace,editor_chdel,     {0} },
        { CTRL('n'),   focusnext,        {0} },
        { CTRL('p'),   focusprev,        {0} },
        { KeyPgUp,     scroll,		 {.i = -20} },
        { KeyPgDw,     scroll,		 {.i = +20} },
};
