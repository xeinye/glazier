VERSION = 0.0

CC = cc
LD = ${CC}

PREFIX = /usr/local
MANDIR = ${PREFIX}/man

CPPFLAGS = -I./libwm -DVERSION=\"${VERSION}\"
CFLAGS = ${CPPFLAGS} -Wall -Wextra -pedantic
LDFLAGS =
LIBS = -lxcb
