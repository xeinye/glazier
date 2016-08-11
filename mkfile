<config.mk

LIBWM_SRC = `{find libwm/ -name '*.c'}

SRC = glazier.c $LIBWM_SRC
OBJ = ${SRC:%.c=%.o}

glazier: $OBJ
	$LD -o $target $prereq $LDFLAGS $LIBS

%.o: %.c
	$CC $CFLAGS -c $stem.c -o $stem.o

clean:V:
	rm -f $OBJ glazier

install:V: glazier
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp glazier ${DESTDIR}${PREFIX}/bin/glazier
	chmod 755 ${DESTDIR}${PREFIX}/bin/glazier
	mkdir -p ${DESTDIR}${MANDIR}/man1
	cp glazier.1 ${DESTDIR}${MANDIR}/man1/glazier.1
	chmod 644 ${DESTDIR}${MANDIR}/man1/glazier.1

uninstall:V:
	rm ${DESTDIR}${PREFIX}/bin/glazier
	rm ${DESTDIR}${MANDIR}/man1/glazier.1
