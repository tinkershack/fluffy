#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <glib.h>
#include <gmodule.h>
#include "fluffy_impl.h"

#define NR_EPOLL_EVENTS	20

FILE *out_file = NULL;	/* File stream for PRINT_STDOUT macro */
FILE *err_file = NULL;	/* File stream for PRINT_STDERR macro */
int epollfd = -1;
mqd_t mqd = -1;		/* Message queue descriptor */
int flh = -1;		/* Fluffy handle */
uint32_t print_events_mask = FLUFFY_PRINT_ALL;

/* Forward function declarations */
int mqsend_exit();
void terminate_run();
int process_fluffy_mqueue(struct epoll_event *evlist);
int print_events(const struct fluffy_event_info *eventinfo, void *user_data);
int set_print_events_mask(char *mask);
int reinitiate_run();

/* Function definitions */

int
set_print_events_mask(char *mask)
{
	if (mask == NULL) {
		return -1;
	}

	unsigned long val = 0;
	val = strtoul(mask, NULL, 0);
	if (val == ULONG_MAX) {		/* value overflow */
		return -1;
	}

	print_events_mask = (uint32_t)val;
	return 0;
}

int
print_events(const struct fluffy_event_info *eventinfo,
    void *user_data)
{
	if (!(eventinfo->event_mask & print_events_mask)) {
		return 0;
	}

	if (eventinfo->event_mask & FLUFFY_ACCESS &&
	    print_events_mask & FLUFFY_ACCESS)
		PRINT_STDOUT("ACCESS,", "");
	if (eventinfo->event_mask & FLUFFY_ATTRIB &&
	    print_events_mask & FLUFFY_ATTRIB)
		PRINT_STDOUT("ATTRIB,", "");
	if (eventinfo->event_mask & FLUFFY_CLOSE_NOWRITE &&
	    print_events_mask & FLUFFY_CLOSE_NOWRITE)
		PRINT_STDOUT("CLOSE_NOWRITE,", "");
	if (eventinfo->event_mask & FLUFFY_CLOSE_WRITE &&
	    print_events_mask & FLUFFY_CLOSE_WRITE)
		PRINT_STDOUT("CLOSE_WRITE,", "");
	if (eventinfo->event_mask & FLUFFY_CREATE &&
	    print_events_mask & FLUFFY_CREATE)
		PRINT_STDOUT("CREATE,", "");
	if (eventinfo->event_mask & FLUFFY_DELETE &&
	    print_events_mask & FLUFFY_DELETE)
		PRINT_STDOUT("DELETE,", "");
	if (eventinfo->event_mask & FLUFFY_ROOT_DELETE &&
	    print_events_mask & FLUFFY_ROOT_DELETE)
		PRINT_STDOUT("ROOT_DELETE,", "");
	if (eventinfo->event_mask & FLUFFY_MODIFY &&
	    print_events_mask & FLUFFY_MODIFY)
		PRINT_STDOUT("MODIFY,", "");
	if (eventinfo->event_mask & FLUFFY_ROOT_MOVE &&
	    print_events_mask & FLUFFY_ROOT_MOVE)
		PRINT_STDOUT("ROOT_MOVE,", "");
	if (eventinfo->event_mask & FLUFFY_MOVED_FROM &&
	    print_events_mask & FLUFFY_MOVED_FROM)
		PRINT_STDOUT("MOVED_FROM,", "");
	if (eventinfo->event_mask & FLUFFY_MOVED_TO &&
	    print_events_mask & FLUFFY_MOVED_TO)
		PRINT_STDOUT("MOVED_TO,", "");
	if (eventinfo->event_mask & FLUFFY_OPEN &&
	    print_events_mask & FLUFFY_OPEN)
		PRINT_STDOUT("OPEN,", "");

	if (eventinfo->event_mask & FLUFFY_IGNORED &&
	    print_events_mask & FLUFFY_IGNORED)
		PRINT_STDOUT("IGNORED,", "");
	if (eventinfo->event_mask & FLUFFY_ISDIR &&
	    print_events_mask & FLUFFY_ISDIR)
		PRINT_STDOUT("ISDIR,", "");
	if (eventinfo->event_mask & FLUFFY_UNMOUNT &&
	    print_events_mask & FLUFFY_UNMOUNT)
		PRINT_STDOUT("UNMOUNT,", "");
	if (eventinfo->event_mask & FLUFFY_ROOT_IGNORED &&
	    print_events_mask & FLUFFY_ROOT_IGNORED)
		PRINT_STDOUT("ROOT_IGNORED,", "");
	if (eventinfo->event_mask & FLUFFY_WATCH_EMPTY &&
	    print_events_mask & FLUFFY_WATCH_EMPTY)
		PRINT_STDOUT("WATCH_EMPTY,", "");
	if (eventinfo->event_mask & FLUFFY_Q_OVERFLOW &&
	    print_events_mask & FLUFFY_Q_OVERFLOW)
		PRINT_STDOUT("Q_OVERFLOW,", "");
	PRINT_STDOUT("\t", "");
	PRINT_STDOUT("%s\n", eventinfo->path ? eventinfo->path : "", "");

	/*
	if (eventinfo->event_mask & FLUFFY_WATCH_EMPTY) {
		int m = 0;
		int fluffy_handle = *(int *)user_data;
		m = fluffy_destroy(fluffy_handle);
		return m;
	}
	*/

	return 0;
}

int
process_fluffy_mqueue(struct epoll_event *evlist)
{
	ssize_t nrbytes;
	int reterr = 0;
	struct mq_attr attr;

	if (evlist->data.fd != mqd) {
		PRINT_STDERR("Incorrect file descriptor\n", "");
		return 1;
	}

	if ((evlist->events & EPOLLERR) || (evlist->events & EPOLLHUP)) {
		return -1;
	} else if (evlist->events & EPOLLIN) {
		if (mq_getattr(mqd, &attr) == -1) {
			reterr = errno;
			perror("mq_getattr");
			return reterr;
		}
		char *mqmsg = NULL;
		mqmsg = calloc(1, attr.mq_msgsize);
		if (mqmsg == NULL) {
			reterr = errno;
			perror("calloc");
			return reterr;
		}

		nrbytes = mq_receive(mqd, mqmsg, attr.mq_msgsize, NULL);
		if (nrbytes == -1) {
			reterr = errno;
			perror("mq_receive");
			free(mqmsg);
			return reterr;
		}

		if (mqmsg[0] == id_mqfluffy_exit) {
			free(mqmsg);
			terminate_run();
		}

		if (mqmsg[0] == id_mqfluffy_watch) {
			reterr = fluffy_add_watch_path(flh, mqmsg + 1);
			if (reterr) {
				free(mqmsg);
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_ignore) {
			reterr = fluffy_remove_watch_path(flh, mqmsg + 1);
			if (reterr) {
				free(mqmsg);
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_max_user_watches) {
			reterr = fluffy_set_max_user_watches(mqmsg + 1);
			if (reterr) {
				free(mqmsg);
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_max_queued_events) {
			reterr = fluffy_set_max_queued_events(mqmsg + 1);
			if (reterr) {
				free(mqmsg);
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_events_mask) {
			reterr = set_print_events_mask(mqmsg + 1);
			if (reterr) {
				free(mqmsg);
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_reinitiate) {
			reterr = reinitiate_run();
			if (reterr) {
				free(mqmsg);
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_list_root_path) {
			free(mqmsg);
			return 0;
		}

		free(mqmsg);
	}
	return 0;
}

int
mqsend_exit()
{
	int reterr = 0;
	mqd_t mqdexit = -1;
	mqdexit = mq_open(mqfluffy, O_WRONLY);
	if (mqdexit == (mqd_t)-1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, sizeof(id_mqfluffy_exit) + 1);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}

	sprintf(mqmsg, "%c", id_mqfluffy_exit);
	mqmsg[strlen(mqmsg) + 1] = '\0';
	if (mq_send(mqdexit, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		free(mqmsg);
		return reterr;
	}

	if (mq_close(mqdexit)) {
		perror("mq_close");
	}
	free(mqmsg);
	return 0;
}

int
initiate_run()
{
	int reterr = 0;
	int ret = 0;

	if (mq_unlink(mqfluffy) == -1) {
		/* Just a best effort at removing stale queue */
	}


	epollfd = epoll_create(2);
	if (epollfd == -1) {
		reterr = errno;
		perror("epoll_create");
		return reterr;
	}

	mqd = mq_open(mqfluffy,
	    O_RDWR | O_CREAT | O_NONBLOCK, S_IRUSR | S_IWUSR, NULL);
	if (mqd == (mqd_t) -1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	struct epoll_event evtmp = {0};
	evtmp.events = EPOLLIN;
	evtmp.data.fd = mqd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, mqd, &evtmp) == -1) {
		reterr = errno;
		perror("epoll_ctl");
		goto cleanup;
	}

	flh = fluffy_init(print_events, NULL);
	if (flh < 1) {
		PRINT_STDERR("fluffy_init fail\n", "");
		goto cleanup;
	}

	/* Detach the context, need wait to be joined later */
	ret = fluffy_no_wait(flh);
	if (ret) {
		PRINT_STDERR("fluffy_no_wait fail\n", "");
		goto cleanup;
	}

	ret = fluffy_set_max_user_watches("524288");
	if (ret != 0) {
		/* Best effort */
		PRINT_STDERR("fluffy_set_max_user_watches fail\n", "");
	}

	ret = fluffy_set_max_queued_events("524288");
	if (ret != 0) {
		/* Best effort */
		PRINT_STDERR("fluffy_set_max_queued_events fail\n", "");
	}

	return 0;

cleanup:
	if (close(epollfd) == -1) {
		perror("close");
	}

	if (mq_close(mqd) == -1) {
		reterr = errno;
		perror("mq_close");
	}
	if (mq_unlink(mqfluffy) == -1) {
		reterr = errno;
		perror("mq_unlink");
	}

	return -1;
}

void
terminate_run()
{
	int reterr = 0;

	if (close(epollfd) == -1) {
		reterr = errno;
		perror("close");
	}

	if (mq_close(mqd) == -1) {
		reterr = errno;
		perror("mq_close");
	}

	if (mq_unlink(mqfluffy) == -1) {
		reterr = errno;
		perror("mq_unlink");
	}

	if (fluffy_destroy(flh)) {
		reterr = 1;
	}

	if (reterr) {
		exit(EXIT_FAILURE);
	} else {
		exit(EXIT_SUCCESS);
	}
}

int
reinitiate_run()
{
	if (fluffy_reinitiate_context(flh)) {
		return -1;
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	gchar option_context[] = "[exit]";
	gchar context_description[] = "Please report bugs at " \
				     "https://github.com/tinkershack/fluffy "\
				     "or raam@tinkershack.in\n";
	gchar *print_out = NULL;
	gchar *print_err = NULL;
	GOptionEntry entries_g[]= {
		{ "outfile", 'O', 0, G_OPTION_ARG_FILENAME, &print_out,
			"File to print output [default:stdout]",
			"./out.fluffy" },
		{ "errfile", 'E', 0, G_OPTION_ARG_FILENAME, &print_err,
			"File to print errors [default:stderr]",
			"./err.fluffy" },
		{ NULL }
	};

	int reterr = 0;
	out_file = stdout;
	err_file = stderr;
        GOptionContext *argctx;
        GError *error_g = NULL;

        argctx = g_option_context_new(option_context);
        g_option_context_add_main_entries(argctx, entries_g, NULL);
        g_option_context_set_description(argctx, context_description);
        if (!g_option_context_parse(argctx, &argc, &argv, &error_g)) {
                PRINT_STDERR("Failed parsing arguments: %s\n",
				error_g->message);
                exit(EXIT_FAILURE);
        }

        if (print_out != NULL) {
                out_file = freopen(print_out, "we", stdout);
		g_free(print_out);
		print_out = NULL;
                if (out_file == NULL) {
                        perror("freopen");
                        exit(EXIT_FAILURE);
                }
        }

        if (print_err != NULL) {
                err_file = freopen(print_err, "we", stderr);
		g_free(print_err);
		print_err = NULL;
                if (err_file == NULL) {
                        perror("freopen");
                        exit(EXIT_FAILURE);
                }
        }
	stdin = freopen("/dev/null", "re", stdin);

	if (argc > 1 && strcmp(argv[1], "exit") == 0) {
		if(mqsend_exit()) {
			exit(EXIT_FAILURE);
		}
		exit(EXIT_SUCCESS);
	} else if (argc > 1) {
		exit(EXIT_FAILURE);
	}

	reterr = initiate_run();
	if (reterr) {
		exit(EXIT_FAILURE);
	}
	
	while (1) {
		struct epoll_event *evlist = NULL;
		evlist = calloc(NR_EPOLL_EVENTS, sizeof(struct epoll_event));
		if (evlist == NULL) {
			perror("calloc");
			exit(EXIT_FAILURE);
		}

		int nready	= 0;
		nready	= epoll_wait(epollfd, evlist, NR_EPOLL_EVENTS, -1);
		if (nready == -1) {
			if (errno == -1) {
				continue;
			} else {
				perror("epoll_wait");
				exit(EXIT_FAILURE);
			}
		}

		int j;
		for (j = 0; j < nready; j++) {
			if (evlist[j].data.fd == mqd) {
				reterr = process_fluffy_mqueue(&evlist[j]);
				if (reterr) {
					PRINT_STDERR("Could not process the " \
						"command. Continuing.\n",
						"");
				}
			}
		}
		free(evlist);
		evlist = NULL;
	}

	terminate_run();

	exit(EXIT_SUCCESS);
}
