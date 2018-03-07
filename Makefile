CC = gcc
CFLAGS = -g \
	 -I./ \
	 -Wall \
	 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-format-extra-args \
	 -pthread \
	 $(shell pkg-config --cflags glib-2.0)

LDFLAGS = $(shell pkg-config --libs glib-2.0)

PROG = fluffy
HDRS = fluffy.h
SRCS = fluffy.c example.c

OBJS = $(SRCS:.c=.o)

all : $(PROG)

$(PROG) : $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(PROG)

fluffy.o : fluffy.c fluffy.h

exmaple.o : example.c fluffy.h

.PHONY : clean install uninstall

clean :
	rm -f core $(PROG) $(OBJS)

install : uninstall
	mkdir -p $(DESTDIR)/usr/bin
	install -m 0755 fluffy $(DESTDIR)/usr/bin/fluffy

uninstall :
	rm -f $(DESTDIR)/usr/bin/fluffy
