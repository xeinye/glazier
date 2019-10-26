include config.mk

glazier: glazier.o libwm/libwm.a
glazier.o: glazier.c config.h

config.h: config.def.h
	cp config.def.h config.h

clean:
	rm -f glazier *.o

install: glazier
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp glazier $(DESTDIR)$(PREFIX)/bin/glazier
	chmod 755 $(DESTDIR)$(PREFIX)/bin/glazier

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/glazier
