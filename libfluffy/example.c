#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fluffy.h>


int
main(int argc, char *argv[])
{
	int ret = 0;
	int flhandle = 0;       /* Fluffy handle */
	int rc = EXIT_SUCCESS;	/* Return code */

	if (argc < 2) {
		fprintf(stderr, "Atleast a path required.\n");
		exit(EXIT_FAILURE);
	}

        /* /proc/sys/fs/inotify/max_user_watches */
	ret = fluffy_set_max_user_watches("524288");
	if (ret != 0) {
		fprintf(stderr, "fluffy_set_max_user_watches fail\n");
	}

        /* /proc/sys/fs/inotify/max_queued_events */
	ret = fluffy_set_max_queued_events("524288");
	if (ret != 0) {
		fprintf(stderr, "fluffy_set_max_queued_events fail\n");
	}

        /*
	 * Initiate fluffy and print events on the standard out with the help
	 * of libfluffy's print event helper function.
	 *
	 * Look at fluffy.h for help with plugfing your custom event handler
	 */
	flhandle = fluffy_init(fluffy_print_event, (void *)&flhandle);
	if (flhandle < 1) {
		fprintf(stderr, "fluffy_init fail\n");
		exit(EXIT_FAILURE);
	}

	do {
		/* Watch argv[1] */
		ret = fluffy_add_watch_path(flhandle, argv[1]);
		if (ret != 0) {
			fprintf(stderr, "fluffy_add_watch_path %s fail\n",
					argv[1]);
			rc = EXIT_FAILURE;
			break;
		}

		/* Let us not exit until Fluffy exits/returns */
		ret = fluffy_wait_until_done(flhandle);
		if (ret != 0) {
			fprintf(stderr, "fluffy_wait_until_done fail\n");
			rc = EXIT_FAILURE;
			break;
		}
	} while(0);

	if (rc != EXIT_SUCCESS) {
		ret = fluffy_destroy(flhandle);	/* Destroy Fluffy context */
	}

	exit(rc);
}
