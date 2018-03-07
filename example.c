#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "fluffy.h"

/* temp function */
int print_event(const struct fluffy_event_info *eventinfo, void *user_data);

int
print_event(const struct fluffy_event_info *eventinfo,
    void *user_data)
{
	static int count = 0;
	if (count > 300) {
		return -1;
		/*
		int m = 0;
		int fluffy_handle = *(int *)user_data;
		m = fluffy_destroy(fluffy_handle);
		return m;
		*/
	}
	fprintf(stdout, "path:\t%s\n", eventinfo->path);
	fprintf(stdout, "mask:\t");

	if (eventinfo->event_mask & FLUFFY_ACCESS)
		fprintf(stdout, "ACCESS, ");
	if (eventinfo->event_mask & FLUFFY_ATTRIB)
		fprintf(stdout, "ATTRIB, ");
	if (eventinfo->event_mask & FLUFFY_CLOSE_NOWRITE)
		fprintf(stdout, "CLOSE_NOWRITE, ");
	if (eventinfo->event_mask & FLUFFY_CLOSE_WRITE)
		fprintf(stdout, "CLOSE_WRITE, ");
	if (eventinfo->event_mask & FLUFFY_CREATE)
		fprintf(stdout, "CREATE, ");
	if (eventinfo->event_mask & FLUFFY_DELETE)
		fprintf(stdout, "DELETE, ");
	/*
	if (eventinfo->event_mask & FLUFFY_DELETE_SELF)
		fprintf(stdout, "FLUFFY_DELETE_SELF, ");
	*/
	if (eventinfo->event_mask & FLUFFY_IGNORED)
		fprintf(stdout, "IGNORED, ");
	if (eventinfo->event_mask & FLUFFY_ISDIR)
		fprintf(stdout, "ISDIR, ");
	if (eventinfo->event_mask & FLUFFY_MODIFY)
		fprintf(stdout, "MODIFY, ");
	/*
	if (eventinfo->event_mask & FLUFFY_MOVE_SELF)
		fprintf(stdout, "FLUFFY_MOVE_SELF, ");
	*/
	if (eventinfo->event_mask & FLUFFY_MOVED_FROM)
		fprintf(stdout, "MOVED_FROM, ");
	if (eventinfo->event_mask & FLUFFY_MOVED_TO)
		fprintf(stdout, "MOVED_TO, ");
	if (eventinfo->event_mask & FLUFFY_OPEN)
		fprintf(stdout, "OPEN, ");
	if (eventinfo->event_mask & FLUFFY_Q_OVERFLOW)
		fprintf(stdout, "Q_OVERFLOW, ");
	if (eventinfo->event_mask & FLUFFY_UNMOUNT)
		fprintf(stdout, "UNMOUNT, ");
	fprintf(stdout, "\n\n");
	count++;
	return 0;
}


int
main(int argc, char *argv[])
{
	int ret = 0;
	int fh1 = 0;

	fh1 = fluffy_init(print_event, (void *)&fh1);
	if (fh1 < 1) {
		fprintf(stderr, "fluffy_init fh1 fail\n");
	}

	ret = fluffy_add_watch_path(fh1, "/opt/d1");
	if (ret != 0) {
		fprintf(stderr, "fluffy_add_watch_path fh1:/opt/d1 fail\n");
	}

	ret = fluffy_add_watch_path(fh1, "/opt/d2");
	if (ret != 0) {
		fprintf(stderr, "fluffy_add_watch_path fh1:/opt/d2 fail\n");
	}

	/*
	sleep(20);
	int m = 0;
	m = fluffy_destroy(fh1);
	if (ret != 0) {
		fprintf(stderr, "fluffy_destroy fh1 fail\n");
	}
	*/

	ret = fluffy_wait_until_done(fh1);
	if (ret != 0) {
		fprintf(stderr, "fluffy_wait_until_done fh1 fail\n");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
