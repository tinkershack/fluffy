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


extern const char mqfluffy[];
extern const char id_mqfluffy_exit;
extern const char id_mqfluffy_watch;
extern const char id_mqfluffy_ignore;
extern const char id_mqfluffy_list_root_path;

#endif
