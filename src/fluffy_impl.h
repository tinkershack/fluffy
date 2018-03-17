#ifndef FLUFFY_IMPL_H
#define FLUFFY_IMPL_H

#include <stdio.h>
#include "fluffy.h"

#define PRINT_STDOUT(fmt, ...)	\
                do { fprintf(out_file, fmt, __VA_ARGS__); \
			if (fflush(out_file)) perror("fflush"); \
		} while(0)

#define PRINT_STDERR(fmt, ...)	\
                do { fprintf(err_file, fmt, __VA_ARGS__); \
			if (fflush(err_file)) perror("fflush"); \
		} while(0)

#define FLUFFY_PRINT_ALL 	FLUFFY_ALL_EVENTS | \
				FLUFFY_ISDIR | \
				FLUFFY_UNMOUNT | \
				FLUFFY_Q_OVERFLOW | \
				FLUFFY_IGNORED | \
				FLUFFY_ROOT_IGNORED | \
				FLUFFY_WATCH_EMPTY


extern const char mqfluffy[];
extern const char id_mqfluffy_exit;
extern const char id_mqfluffy_watch;
extern const char id_mqfluffy_ignore;
extern const char id_mqfluffy_reinitiate;
extern const char id_mqfluffy_list_root_path;
extern const char id_mqfluffy_max_user_watches;
extern const char id_mqfluffy_max_queued_events;
extern const char id_mqfluffy_events_mask;

#endif
