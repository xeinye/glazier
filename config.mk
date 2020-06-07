VERSION = 0.0

CC = cc
LD = ${CC}

PREFIX = /usr/local
MANDIR = ${PREFIX}/man

CPPFLAGS = -I./libwm -DVERSION=\"${VERSION}\"
CFLAGS = -Wall -Wextra -pedantic -g
LDFLAGS = -L./libwm
LDLIBS = -lxcb -lxcb-cursor -lwm
