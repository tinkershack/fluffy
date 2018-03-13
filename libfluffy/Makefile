CC 	= gcc
CFLAGS	= \
	  -g \
	  -I./ \
	  -Wall \
	  -Wextra \
	  -Wno-unused-parameter \
	  -Wno-format-extra-args \
	  -pthread \
	  $(shell pkg-config --cflags glib-2.0)

LDFLAGS	= $(shell pkg-config --libs glib-2.0)

LIB_HDRS	= fluffy.h
LIB_SRCS	= fluffy.c
LIB_OBJS	= $(LIB_SRCS:.c=.o)
STATIC_LIB	= libfluffy.a

EXAMPLE_SRCS	= example.c
EXAMPLE_OBJS	= $(EXAMPLE_SRCS:.c=.o)
EXAMPLE_OUT	= fluffy-example


all : $(STATIC_LIB)

$(STATIC_LIB) : $(LIB_OBJS)
	ar crs $@ $(LIB_OBJS)

example : $(EXAMPLE_OUT)

$(EXAMPLE_OUT) : $(STATIC_LIB) $(EXAMPLE_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXAMPLE_OBJS) -L./ -lfluffy -o $@ 

fluffy.o : fluffy.c fluffy.h

exmaple.o : example.c $(STATIC_LIB)

.PHONY : clean install uninstall example

clean :
	rm -f core $(STATIC_LIB) $(EXAMPLE_OUT) $(LIB_OBJS) $(EXAMPLE_OBJS)

install : $(STATIC_LIB) uninstall
	mkdir -p $(DESTDIR)/usr/lib
	install -m 0755 $< $(DESTDIR)/usr/lib/
	mkdir -p $(DESTDIR)/usr/include/fluffy
	install -m 0755 $(LIB_HDRS) $(DESTDIR)/usr/include/fluffy/

uninstall :
	rm -f $(DESTDIR)/usr/lib/$(STATIC_LIB)
	rm -f $(DESTDIR)/usr/include/fluffy/$(LIB_HDRS)
