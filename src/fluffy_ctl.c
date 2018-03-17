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
uint32_t events_mask = 0;


int mqsend_watch(char *watch_path);
int mqsend_ignore(char *ignore_path);
int mqsend_max_user_watches(char *max_watches);
int mqsend_max_queued_events(char *max_events);
int mqsend_reinitiate();
int mqsend_list_root_path();
int mqsend_events_mask(uint32_t mask);

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
	if (mq_send(mqwatch, (const char *)mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		return reterr;
	}
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
		perror("mq_send");
		return reterr;
	}
	if (mq_close(mqwatch)) {
		perror("mq_close");
	}
	return 0;
}


int
mqsend_events_mask(uint32_t mask)
{
	int reterr = 0;
	mqd_t mqdevents = -1;
	mqdevents = mq_open(mqfluffy, O_WRONLY);
	if (mqdevents == (mqd_t)-1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	/* Get the number of characters required for string conversion */
	int mask_size;
	mask_size = snprintf(NULL, 0, "%lu", (unsigned long) mask);
	if (mask_size < 1) {
		PRINT_STDERR("Failed to parse value", "");
		return -1;
	}
	mask_size += 1;		/* Account for the null terminator */
	char mask_str[mask_size];

	int n = 0;
	/* Write the string form of the integer value to mask_str */
	n = snprintf(mask_str, mask_size, "%lu", (unsigned long) mask);
	if (n != mask_size - 1 || mask_str[mask_size-1] != '\0') {
		PRINT_STDERR("Failed to parse value", "");
		return -1;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, mask_size + 2);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}
	char idstr[1];
	sprintf(idstr, "%c", id_mqfluffy_events_mask);
	mqmsg = strncpy(mqmsg, idstr, 2);
	mqmsg = strncat(mqmsg, mask_str, strlen(mask_str) + 1);
	if (mq_send(mqdevents, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		return reterr;
	}
	if (mq_close(mqdevents)) {
		perror("mq_close");
	}
	return 0;
}


int
mqsend_max_user_watches(char *max_watches)
{
	int reterr = 0;
	mqd_t mqdmax = -1;
	mqdmax = mq_open(mqfluffy, O_WRONLY);
	if (mqdmax == (mqd_t)-1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, strlen(max_watches) + 2);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}
	char idstr[1];
	sprintf(idstr, "%c", id_mqfluffy_max_user_watches);
	mqmsg = strncpy(mqmsg, idstr, 2);
	mqmsg = strncat(mqmsg, max_watches, strlen(max_watches) + 1);
	if (mq_send(mqdmax, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		return reterr;
	}
	if (mq_close(mqdmax)) {
		perror("mq_close");
	}
	return 0;
}


int
mqsend_max_queued_events(char *max_events)
{
	int reterr = 0;
	mqd_t mqdmax = -1;
	mqdmax = mq_open(mqfluffy, O_WRONLY);
	if (mqdmax == (mqd_t)-1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, strlen(max_events) + 2);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}
	char idstr[1];
	sprintf(idstr, "%c", id_mqfluffy_max_queued_events);
	mqmsg = strncpy(mqmsg, idstr, 2);
	mqmsg = strncat(mqmsg, max_events, strlen(max_events) + 1);
	if (mq_send(mqdmax, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		return reterr;
	}
	if (mq_close(mqdmax)) {
		perror("mq_close");
	}
	return 0;
}


int
mqsend_reinitiate()
{
	int reterr = 0;
	mqd_t reinit = -1;
	reinit = mq_open(mqfluffy, O_WRONLY);
	if (reinit == (mqd_t) -1) {
		reterr = errno;
		perror("mq_open");
		return reterr;
	}

	char *mqmsg = NULL;
	mqmsg = calloc(1, sizeof(id_mqfluffy_reinitiate) + 1);
	if (mqmsg == NULL) {
		reterr = errno;
		perror("calloc");
		return reterr;
	}

	sprintf(mqmsg, "%c", id_mqfluffy_reinitiate);
	mqmsg[strlen(mqmsg) + 1] = '\0';
	if (mq_send(reinit, mqmsg, strlen(mqmsg) + 1, 0) == -1) {
		reterr = errno;
		perror("mq_send");
		free(mqmsg);
		return reterr;
	}

	if (mq_close(reinit)) {
		perror("mq_close");
	}
	free(mqmsg);
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
		return reterr;
	}

	if (mq_close(mqlist)) {
		perror("mq_close");
	}
	return 0;
}


int
main(int argc, char *argv[])
{
	gchar option_context[] = "[\"/path/to/hogwarts/kitchen\"]";
	gchar context_description[] = "Please report bugs at " \
				     "https://github.com/six-k/fluffy or " \
				     "raam@tinkershack.in\n";
	gchar context_summary[] = "'fluffyctl' controls 'fluffy' program.\n"
		"fluffy must be invoked before adding/removing watches. \n"
		"By default all file system events are watched and reported. "
		"--help-events will show the options to modify this behaviour";

	/* For main group options */
	gchar *print_out = NULL;
	gchar *print_err = NULL;
	gchar **watch_paths = NULL;
	gchar **ignore_paths = NULL;
	gchar *max_user_watches = NULL;
	gchar *max_queued_events = NULL;
	gboolean reinitiate = FALSE;
	gboolean list_root_path = FALSE;
	gboolean get_watch_nos = FALSE;

	GOptionEntry main_entries_g[]= {
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
		{ "max-user-watches", 'W', 0, G_OPTION_ARG_STRING,
			&max_user_watches,
			"Upper limit on the number of watches per uid " \
				"[fluffy defaults 524288]",
			"524288" },
		{ "max-queued-events", 'Q', 0, G_OPTION_ARG_STRING,
			&max_queued_events,
			"Upper limit on the number of events " \
				"[fluffy defaults 524288]",
			"524288" },
		{ "reinit", 'z', 0, G_OPTION_ARG_NONE,
			&reinitiate,
			"Reinitiate watch on all root paths. " \
				"[Avoid unless necessary]",
			NULL },
		/*
		{ "list-root-paths", 'l', 0, G_OPTION_ARG_NONE, &list_root_path,
			"List root paths being watched", NULL },
		{ "get-watch-nos", 'n', 0, G_OPTION_ARG_NONE, &get_watch_nos,
			"Print the number of watches currently set", NULL },
		*/
		{ NULL }
	};

	out_file = stdout;
	err_file = stderr;
        GOptionContext *argctx;
        GError *error_g = NULL;

        argctx = g_option_context_new(option_context);
        g_option_context_add_main_entries(argctx, main_entries_g, NULL);
        g_option_context_set_description(argctx, context_description);
        g_option_context_set_summary(argctx, context_summary);

	/* For event groups options */
	gboolean event_all = FALSE;
	gboolean event_access = FALSE;
	gboolean event_modify = FALSE;
	gboolean event_attrib = FALSE;
	gboolean event_close_write = FALSE;
	gboolean event_close_nowrite = FALSE;
	gboolean event_open = FALSE;
	gboolean event_moved_from = FALSE;
	gboolean event_moved_to = FALSE;
	gboolean event_create = FALSE;
	gboolean event_delete = FALSE;
	gboolean event_root_delete = FALSE;
	gboolean event_root_move = FALSE;
	gboolean event_isdir = FALSE;
	gboolean event_unmount = FALSE;
	gboolean event_q_overflow = FALSE;
	gboolean event_ignored = FALSE;
	gboolean event_root_ignored = FALSE;
	gboolean event_watch_empty = FALSE;

	GOptionEntry event_entries_g[]= {
		{
			"all", 0, 0, G_OPTION_ARG_NONE,
			&event_all,
			"Watch all possible events [default]",
			NULL },
		{
			"access", 0, 0, G_OPTION_ARG_NONE,
			&event_access,
			"Watch file access",
			NULL },
		{
			"modify", 0, 0, G_OPTION_ARG_NONE,
			&event_modify,
			"Watch file modifications",
			NULL },
		{
			"attrib", 0, 0, G_OPTION_ARG_NONE,
			&event_attrib,
			"Watch metadata change",
			NULL },
		{
			"close-write", 0, 0, G_OPTION_ARG_NONE,
			&event_close_write,
			"Watch closing of file opened for writing",
			NULL },
		{
			"close-nowrite", 0, 0, G_OPTION_ARG_NONE,
			&event_close_nowrite,
			"Watch closing of file/dir not opened for writing",
			NULL },
		{
			"open", 0, 0, G_OPTION_ARG_NONE,
			&event_open,
			"Watch opening of file/dir",
			NULL },
		{
			"moved-from", 0, 0, G_OPTION_ARG_NONE,
			&event_moved_from,
			"Watch renames/moves: reports old file/dir name",
			NULL },
		{
			"moved-to", 0, 0, G_OPTION_ARG_NONE,
			&event_moved_to,
			"Watch renames/moves: reports new file/dir name",
			NULL },
		{
			"create", 0, 0, G_OPTION_ARG_NONE,
			&event_create,
			"Watch creation of files/dirs",
			NULL },
		{
			"delete", 0, 0, G_OPTION_ARG_NONE,
			&event_delete,
			"Watch deletion of files/dirs",
			NULL },
		{
			"root-delete", 0, 0, G_OPTION_ARG_NONE,
			&event_root_delete,
			"Watch root path deletions",
			NULL },
		{
			"root-move", 0, 0, G_OPTION_ARG_NONE,
			&event_root_move,
			"Watch root path moves/renames",
			NULL },
		{
			"isdir", 0, 0, G_OPTION_ARG_NONE,
			&event_isdir,
			"Watch for events that occur against a directory",
			NULL },
		{
			"unmount", 0, 0, G_OPTION_ARG_NONE,
			&event_unmount,
			"Watch for unmount of the backing filesystem " \
				"['isdir' not raised]",
			NULL },
		{
			"queue-overflow", 0, 0, G_OPTION_ARG_NONE,
			&event_q_overflow,
			"Watch for event queue overflows " \
				"['isdir' not raised]",
			NULL },
		{
			"ignored", 0, 0, G_OPTION_ARG_NONE,
			&event_ignored,
			"Watch for paths ignored by Fluffy(not watched) "
				"['isdir' not raised]",
			NULL },
		{
			"root-ignored", 0, 0, G_OPTION_ARG_NONE,
			&event_root_ignored,
			"Watch for root paths ignored(not watched) "
				"['isdir' not raised]",
			NULL },
		{
			"watch-empty", 0, 0, G_OPTION_ARG_NONE,
			&event_watch_empty,
			"Watch whether all Fluffy watches are removed "\
				"['isdir' not raised]",
			NULL },
		{ NULL }
	};

        GOptionGroup *group_g;
	gchar event_group_name[] = "events";
	gchar event_group_description[] = ""
		"When an option or more from 'events' group is passed, only "
		"those events will be reported. When a new invocation "
		"of fluffyctl sets any 'events' option, previously set events "
		"choice is discarded; overrides.\n";

	group_g = g_option_group_new(event_group_name,
			event_group_description,
			"File system events to report",
			NULL,
			NULL);
	g_option_group_add_entries(group_g, event_entries_g);
        g_option_context_add_group(argctx, group_g);

	if (argc < 2) {
		/*
		PRINT_STDERR("%s", g_option_context_get_help(argctx, FALSE,
					NULL));
		*/
		PRINT_STDERR("Usage:	%s --help [-h]\n", argv[0]);

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

		if (max_queued_events != NULL) {
			if (mqsend_max_queued_events(max_queued_events)) {
				rc = -1;
			}
			done = 1;
			g_free(max_queued_events);
			max_queued_events = NULL;
		}

		if (max_user_watches != NULL) {
			if (mqsend_max_user_watches(max_user_watches)) {
				rc = -1;
			}
			done = 1;
			g_free(max_user_watches);
			max_user_watches = NULL;
		}

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

		if (reinitiate) {
			if (mqsend_reinitiate()) {
				rc = -1;
			}
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

		/* Check for event group options */
		if (event_all) {
			events_mask |= FLUFFY_PRINT_ALL;
		}

		if (event_access) {
			events_mask |= FLUFFY_ACCESS;
		}

		if (event_modify) {
			events_mask |= FLUFFY_MODIFY;
		}

		if (event_attrib) {
			events_mask |= FLUFFY_ATTRIB;
		}

		if (event_close_write) {
			events_mask |= FLUFFY_CLOSE_WRITE;
		}

		if (event_close_nowrite) {
			events_mask |= FLUFFY_CLOSE_NOWRITE;
		}

		if (event_open) {
			events_mask |= FLUFFY_OPEN;
		}

		if (event_moved_from) {
			events_mask |= FLUFFY_MOVED_FROM;
		}

		if (event_moved_to) {
			events_mask |= FLUFFY_MOVED_TO;
		}

		if (event_create) {
			events_mask |= FLUFFY_CREATE;
		}

		if (event_delete) {
			events_mask |= FLUFFY_DELETE;
		}

		if (event_root_delete) {
			events_mask |= FLUFFY_ROOT_DELETE;
		}

		if (event_root_move) {
			events_mask |= FLUFFY_ROOT_MOVE;
		}

		if (event_isdir) {
			events_mask |= FLUFFY_ISDIR;
		}

		if (event_unmount) {
			events_mask |= FLUFFY_UNMOUNT;
		}

		if (event_q_overflow) {
			events_mask |= FLUFFY_Q_OVERFLOW;
		}

		if (event_ignored) {
			events_mask |= FLUFFY_IGNORED;
		}

		if (event_root_ignored) {
			events_mask |= FLUFFY_ROOT_IGNORED;
		}

		if (event_watch_empty) {
			events_mask |= FLUFFY_WATCH_EMPTY;
		}

		if (events_mask) {
			if (mqsend_events_mask(events_mask)) {
				rc = -1;
			}
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
