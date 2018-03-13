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
#include <sys/types.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <glib.h>
#include <gmodule.h>
#include "fluffy_impl.h"

FILE *out_file = NULL;
FILE *err_file = NULL;

int mqsend_watch(char *watch_path);
int mqsend_ignore(char *ignore_path);
int mqsend_list_root_path();

int
mqsend_watch(char *watch_path)
{
	int reterr = 0;
	mqd_t mqwatch = -1;
	mqwatch = mq_open(mqfluffy, O_WRONLY);
	if (mqwatch == (mqd_t) -1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, strlen(watch_path) + 2);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}
	char idstr[1];
	sprintf(idstr, "%c", id_mqfluffy_watch);
	mqmsg = strncpy(mqmsg, idstr, 2);
	mqmsg = strncat(mqmsg, watch_path, strlen(watch_path) + 1);
	if (mq_send(mqwatch, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		free(mqmsg);
		perror("mq_send");
		return reterr;
	}
	free(mqmsg);
	if (mq_close(mqwatch)) {
		perror("mq_close");
	}
	return 0;
}


int
mqsend_ignore(char *ignore_path)
{
	int reterr = 0;
	mqd_t mqwatch = -1;
	mqwatch = mq_open(mqfluffy, O_WRONLY);
	if (mqwatch == (mqd_t)-1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, strlen(ignore_path) + 2);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}
	char idstr[1];
	sprintf(idstr, "%c", id_mqfluffy_ignore);
	mqmsg = strncpy(mqmsg, idstr, 2);
	mqmsg = strncat(mqmsg, ignore_path, strlen(ignore_path) + 1);
	if (mq_send(mqwatch, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		free(mqmsg);
		perror("mq_send");
		return reterr;
	}
	free(mqmsg);
	if (mq_close(mqwatch)) {
		perror("mq_close");
	}
	return 0;
}


int
mqsend_list_root_path()
{
	int reterr = 0;
	mqd_t mqlist = -1;
	mqlist = mq_open(mqfluffy, O_WRONLY);
	if (mqlist == (mqd_t) -1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, sizeof(id_mqfluffy_list_root_path) + 1);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}

	sprintf(mqmsg, "%c", id_mqfluffy_list_root_path);
	mqmsg[strlen(mqmsg) + 1] = '\0';
	if (mq_send(mqlist, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		free(mqmsg);
		return reterr;
	}

	if (mq_close(mqlist)) {
		perror("mq_close");
	}
	free(mqmsg);
	return 0;
}


int
main(int argc, char *argv[])
{
	gchar option_context[] = "[\"/path/to/hogwarts/kitchen\"]";
	gchar context_description[] = "Please report bugs at " \
				     "https://github.com/six-k/fluffy or " \
				     "raam@tinkershack.in\n";
	gchar *print_out = NULL;
	gchar *print_err = NULL;
	gchar **watch_paths = NULL;
	gchar **ignore_paths = NULL;
	gboolean list_root_path = FALSE;
	gboolean get_watch_nos = FALSE;
	GOptionEntry entries_g[]= {
		{ "outfile", 'O', 0, G_OPTION_ARG_FILENAME, &print_out,
			"File to print output [default:stdout]",
			"./out.fluffy" },
		{ "errfile", 'E', 0, G_OPTION_ARG_FILENAME, &print_err,
			"File to print errors [default:stderr]",
			"./err.fluffy" },
		{ "watch", 'w', 0, G_OPTION_ARG_FILENAME_ARRAY, &watch_paths,
			"Paths to watch recursively. Repeat flag for " \
				"multiple paths.",
			"/grimmauld/place/12" },
		{ "ignore", 'I', 0, G_OPTION_ARG_FILENAME_ARRAY, &ignore_paths,
			"Paths to ignore recursively. Repeat flag for " \
				"multiple paths.",
			"/knockturn/alley/borgin/brukes" },
		{ "list-root-paths", 'l', 0, G_OPTION_ARG_NONE, &list_root_path,
			"List root paths being watched", NULL },
		{ "get-watch-nos", 'n', 0, G_OPTION_ARG_NONE, &get_watch_nos,
			"Print the number of watches currently set", NULL },
		{ NULL }
	};

	out_file = stdout;
	err_file = stderr;
        GOptionContext *argctx;
        GError *error_g = NULL;

        argctx = g_option_context_new(option_context);
        g_option_context_add_main_entries(argctx, entries_g, NULL);
        g_option_context_set_description(argctx, context_description);

	if (argc < 2) {
		PRINT_STDERR("%s", g_option_context_get_help(argctx, FALSE,
					NULL));
		exit(EXIT_FAILURE);
	}

        if (!g_option_context_parse(argctx, &argc, &argv, &error_g)) {
                PRINT_STDERR("Failed parsing arguments: %s\n",
				error_g->message);
                exit(EXIT_FAILURE);
        }

        if (print_out != NULL) {
                out_file = freopen(print_out, "we", stdout);
                if (out_file == NULL) {
			g_free(print_out);
			print_out = NULL;
                        perror("freopen");
                        exit(EXIT_FAILURE);
                }
		g_free(print_out);
		print_out = NULL;
        }
        if (print_err != NULL) {
                err_file = freopen(print_err, "we", stderr);
                if (err_file == NULL) {
			g_free(print_err);
			print_err = NULL;
                        perror("freopen");
                        exit(EXIT_FAILURE);
                }
		g_free(print_err);
		print_err = NULL;
        }
	stdin = freopen("/dev/null", "re", stdin);

	do {
		int done = 0;
		int rc = 0;
		if (watch_paths != NULL) {
			int j = 0;
			do {
				char *resvpath = NULL;
				resvpath = realpath(watch_paths[j], NULL);
				if (resvpath == NULL) {
					perror("realpath");
					rc = -1;
					break;
				}

				if (mqsend_watch(resvpath)) {
					rc = -1;
				}
				free(resvpath);
				resvpath = NULL;
			} while (watch_paths[++j] != NULL);

			g_strfreev(watch_paths);
			watch_paths = NULL;
			done = 1;
		}

		if (ignore_paths != NULL) {
			int j = 0;
			do {
				char *resvpath = NULL;
				resvpath = realpath(ignore_paths[j], NULL);
				if (resvpath == NULL) {
					perror("realpath");
					rc = -1;
					break;
				}

				if (mqsend_ignore(resvpath)) {
					rc = -1;
				}
				free(resvpath);
				resvpath = NULL;
			} while (ignore_paths[++j] != NULL);

			g_strfreev(ignore_paths);
			ignore_paths = NULL;
			done = 1;
		}

		if (list_root_path) {
			if (mqsend_list_root_path()) {
				rc = -1;
			}
			done = 1;
		}
		if (get_watch_nos) {
			/* fill in */
			done = 1;
		}

		if (done) {
			exit(rc);
		} else {
			break;
		}

	} while (0);

	exit(EXIT_SUCCESS);
}
