include config.mk

all: glazier 

glazier: glazier.o
	$(LD) -o $@ glazier.o $(LDFLAGS)

ewmh: ewmh.o
	$(LD) -o $@ ewmh.o $(LDFLAGS)

glazier.o: glazier.c config.h

config.h: config.def.h
	cp config.def.h config.h

clean:
	rm -f config.h glazier *.o 

install: glazier 
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f glazier $(DESTDIR)$(PREFIX)/bin/glazier
	chmod 755 $(DESTDIR)$(PREFIX)/bin/glazier

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/glazier
