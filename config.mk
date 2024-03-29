# circo
VERSION = 0.2

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=2 -DVERSION=\"${VERSION}\"
#CFLAGS   = -std=c99 -g -pedantic -Wall -O0 ${CPPFLAGS} -DDEBUG
CFLAGS  = -std=c99 -pedantic -Wall -Wno-deprecated-declarations -Os ${CPPFLAGS}

# compiler and linker
CC = cc
