<config.mk

all:V: glazier ewmh

glazier: glazier.o libwm/libwm.a
	${LD} ${LDFLAGS} $prereq ${LDLIBS} -o $target

ewmh: ewmh.o libwm/libwm.a
	${LD} ${LDFLAGS} $prereq ${LDLIBS} -o $target

glazier.o: glazier.c config.h

%.o: %.c
	${CC} ${CPPFLAGS} ${CFLAGS} -c $stem.c -o $stem.o

config.h: config.def.h
	cp config.def.h config.h

clean:V:
	rm -f glazier ewmh *.o

install:V: glazier ewmh
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp glazier ${DESTDIR}${PREFIX}/bin/glazier
	cp ewmh ${DESTDIR}${PREFIX}/bin/ewmh
	chmod 755 ${DESTDIR}${PREFIX}/bin/glazier
	chmod 755 ${DESTDIR}${PREFIX}/bin/ewmh

uninstall:V:
	rm ${DESTDIR}${PREFIX}/bin/glazier
	rm ${DESTDIR}${PREFIX}/bin/ewmh
