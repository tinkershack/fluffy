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

/* Forward function declarations */
int process_fluffy_mqueue(struct epoll_event *evlist);
int mqsend_exit();
void terminate_run();

/* Function definitions */

int
process_fluffy_mqueue(struct epoll_event *evlist)
{
	ssize_t nrbytes;
	int reterr = 0;
	struct mq_attr attr;
	char *mqmsg = NULL;

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
			return reterr;
		}

		if (mqmsg[0] == id_mqfluffy_exit) {
			terminate_run();
		}

		if (mqmsg[0] == id_mqfluffy_watch) {
			reterr = fluffy_add_watch_path(flh, mqmsg + 1);
			if (reterr) {
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_ignore) {
			reterr = fluffy_remove_watch_path(flh, mqmsg + 1);
			if (reterr) {
				return reterr;
			}
		}

		if (mqmsg[0] == id_mqfluffy_list_root_path) {
		}
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

	flh = fluffy_init(fluffy_print_event, (void *)&flh);
	if (flh < 1) {
		PRINT_STDERR("fluffy_init fail\n", "");
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

	if (fluffy_wait_until_done(flh)) {
		reterr = 1;
	}

	if (reterr) {
		exit(EXIT_FAILURE);
	} else {
		exit(EXIT_SUCCESS);
	}
}


int
main(int argc, char *argv[])
{
	gchar option_context[] = "[exit]";
	gchar context_description[] = "Please report bugs at " \
				     "https://github.com/six-k/fluffy or " \
				     "raam@tinkershack.in\n";
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
