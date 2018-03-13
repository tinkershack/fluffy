CC = gcc
CFLAGS	= \
	 -g \
	 -I./ \
	 -I./libfluffy \
	 -L./libfluffy \
	 -Wall \
	 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-format-extra-args \
	 -pthread \
	 $(shell pkg-config --cflags glib-2.0)

LDFLAGS = \
	  -lrt \
	  -lfluffy \
	  $(shell pkg-config --libs glib-2.0)

SRC_PATH	= src/
HDRS 		= $(SRC_PATH)fluffy_impl.h
SRCS 		= $(SRC_PATH)fluffy_impl.c

RUN_NAME	= fluffyrun
PROG_RUN	= bin/$(RUN_NAME)
SRCS_RUN 	= $(SRC_PATH)fluffy_run.c

CTL_NAME	= fluffyctl
PROG_CTL 	= bin/$(CTL_NAME)
SRCS_CTL 	= $(SRC_PATH)fluffy_ctl.c

OBJS		= $(SRCS:.c=.o)
OBJS_RUN 	= $(OBJS) $(SRCS_RUN:.c=.o)
OBJS_CTL 	= $(OBJS) $(SRCS_CTL:.c=.o)


all 		: make_libfluffy create_dir $(PROG_RUN) $(PROG_CTL)

$(PROG_RUN) 	: $(OBJS_RUN)
	$(CC) $(CFLAGS) $(OBJS_RUN) $(LDFLAGS) -o $(PROG_RUN)

$(PROG_CTL) 	: $(OBJS_CTL)
	$(CC) $(CFLAGS) $(OBJS_CTL) $(LDFLAGS) -o $(PROG_CTL)

$(SRC_PATH)fluffy_impl.o 	: $(SRCS) $(HDRS)
$(SRC_PATH)fluffy_run.o 	: $(SRCS_RUN) $(HDRS)
$(SRC_PATH)fluffy_ctl.o 	: $(SRCS_CTL) $(HDRS)

.PHONY: clean uninstall install create_dir make_libfluffy

make_libfluffy:
	make -C ./libfluffy/

clean_libfluffy:
	make -C ./libfluffy/ clean

install_libfluffy:
	make -C ./libfluffy/ install

uninstall_libfluffy:
	make -C ./libfluffy/ uninstall

create_dir :
	mkdir -p ./bin

clean : clean_libfluffy
	rm -f core $(PROG_RUN) $(OBJS_RUN)
	rm -f core $(PROG_CTL) $(OBJS_CTL)
	rmdir ./bin

uninstall : uninstall_libfluffy
	rm -f $(DESTDIR)/usr/bin/$(RUN_NAME)
	rm -f $(DESTDIR)/usr/bin/$(CTL_NAME)

install : uninstall install_libfluffy 
	mkdir -p $(DESTDIR)/usr/bin
	install -m 0755 $(PROG_RUN) $(DESTDIR)/usr/bin/
	install -m 0755 $(PROG_CTL) $(DESTDIR)/usr/bin/
