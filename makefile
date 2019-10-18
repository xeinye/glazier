include config.mk

SRC = glazier.c

glazier: glazier.o libwm/libwm.a

clean:
	rm -f glazier *.o

install: glazier
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp glazier $(DESTDIR)$(PREFIX)/bin/glazier
	chmod 755 $(DESTDIR)$(PREFIX)/bin/glazier
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp glazier.1 $(DESTDIR)$(MANPREFIX)/man1/glazier.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/glazier.1

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/glazier
	rm $(DESTDIR)$(MANPREFIX)/man1/glazier.1
