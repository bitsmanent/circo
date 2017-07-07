/*
 * Copy me if you can.
 * by 20h
 */

#ifndef __ARG_H__
#define __ARG_H__

extern char *argv0;

#define USED(x) ((void)(x))

#define ARGBEGIN	for (argv0 = *argv, argv++, argc--;\
					argv[0] && argv[0][1]\
					&& argv[0][0] == '-';\
					argc--, argv++) {\
				char _argc;\
				char **_argv;\
				if (argv[0][1] == '-' && argv[0][2] == '\0') {\
					argv++;\
					argc--;\
					break;\
				}\
				for (argv[0]++, _argv = argv; argv[0][0];\
						argv[0]++) {\
					if (_argv != argv)\
						break;\
					_argc = argv[0][0];\
					switch (_argc)

#define ARGEND			}\
				USED(_argc);\
			}\
			USED(argv);\
			USED(argc);

#define EARGF(x)	((argv[1] == NULL)? ((x), abort(), (char *)0) :\
			(argc--, argv++, argv[0]))

#endif

