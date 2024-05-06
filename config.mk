CC = cc
LD = ${CC}

PREFIX = /usr/local
MANDIR = ${PREFIX}/man

CPPFLAGS = -I/usr/X11R6/include -I/usr/local/include
CFLAGS = -Wall -Wextra -pedantic -g
LDFLAGS = -L./libwm -L/usr/X11R6/lib -L/usr/local/lib ${LIBS}
LIBS = -lwm -lxcb-cursor -lxcb-image -lxcb-randr -lxcb

