CC = cc
LD = ${CC}

PREFIX = /usr/local
MANDIR = ${PREFIX}/man

CPPFLAGS = -I./libwm
CFLAGS = -Wall -Wextra -pedantic -g
LDFLAGS = -L./libwm ${LIBS}
LIBS = -lwm -lxcb-cursor -lxcb-image -lxcb-randr -lxcb
