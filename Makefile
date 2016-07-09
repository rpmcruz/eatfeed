# EatFeed
CC := g++
CFLAGS := -g -Wall `pkg-config gtk+-2.0 gthread-2.0 libcurl --cflags`
LIBS := `pkg-config gtk+-2.0 gthread-2.0 libcurl --libs`

# test html renderers... Ugly, but Makefile doesn't seem suited for such tests
# and I don't intend to play with autoconf.

# test WebKit
CFLAGS += `if pkg-config webkit-1.0 --exists ; then echo -DUSE_WEBKIT ; pkg-config webkit-1.0 --cflags ; fi`
LIBS += `if pkg-config webkit-1.0 --exists ; then pkg-config webkit-1.0 --libs ; fi`
# (webkit may be installed under WebKitGtk in some systems.)
CFLAGS += `if pkg-config WebKitGtk --exists ; then echo -DUSE_WEBKIT ; pkg-config WebKitGtk --cflags ; fi`
LIBS += `if pkg-config WebKitGtk --exists ; then pkg-config WebKitGtk --libs ; fi`
# test LibGtkHtml
CFLAGS += `if pkg-config libgtkhtml-2.0 --exists ; then echo -DUSE_LIBGTKHTML ; pkg-config libgtkhtml-2.0 --cflags ; fi`
LIBS += `if pkg-config libgtkhtml-2.0 --exists ; then pkg-config libgtkhtml-2.0 --libs ; fi`

all: eatfeed
	@echo "Compiled"

app.o: app.cpp gtkmodel.h feed.h
	$(CC) $(CFLAGS) app.cpp -c -o app.o

gtkmodel.o: gtkmodel.cpp gtkmodel.h
	$(CC) $(CFLAGS) gtkmodel.cpp -c -o gtkmodel.o

feed.o: feed.cpp feed.h parser.h xmlparser.h
	$(CC) $(CFLAGS) feed.cpp -c -o feed.o

parser.o: parser.cpp parser.h xmlparser.h
	$(CC) $(CFLAGS) parser.cpp -c -o parser.o

xmlparser.o: xmlparser.cpp xmlparser.h
	$(CC) $(CFLAGS) xmlparser.cpp -c -o xmlparser.o

eatfeed: app.o gtkmodel.o feed.o parser.o xmlparser.o
	$(CC) $(LIBS) app.o gtkmodel.o feed.o parser.o xmlparser.o -o eatfeed

clean:
	rm -f eatfeed *.o *~

install:
	install eatfeed /usr/bin
	install eatfeed.desktop /usr/share/applications

uninstall:
	rm -f /usr/bin/eatfeed /usr/share/applications/eatfeed.desktop

