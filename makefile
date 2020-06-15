include config.mk

all: glazier ewmh

glazier: glazier.o randr.o libwm/libwm.a
ewmh: ewmh.o randr.o libwm/libwm.a

glazier.o: glazier.c config.h

config.h: config.def.h
	cp config.def.h config.h

clean:
	rm -f glazier ewmh *.o

install: glazier ewmh
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp glazier $(DESTDIR)$(PREFIX)/bin/glazier
	cp ewmh $(DESTDIR)$(PREFIX)/bin/ewmh
	chmod 755 $(DESTDIR)$(PREFIX)/bin/glazier
	chmod 755 $(DESTDIR)$(PREFIX)/bin/ewmh

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/glazier
	rm $(DESTDIR)$(PREFIX)/bin/ewmh
