#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fluffy.h>


int
main(int argc, char *argv[])
{
	int ret = 0;
	int fh1 = 0;

	if (argc < 3) {
		fprintf(stderr, "Atleast two paths required.\n");
		exit(EXIT_FAILURE);
	}


	fh1 = fluffy_init(fluffy_print_event, (void *)&fh1);
	if (fh1 < 1) {
		fprintf(stderr, "fluffy_init fh1 fail\n");
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "fluffy_init fh1 done\n");

	ret = fluffy_set_max_user_instances("4096");
	if (ret != 0) {
		fprintf(stderr, "fluffy_set_max_user_instances fail\n");
	}
	fprintf(stdout, "fluffy_set_max_user_instances done\n");

	ret = fluffy_set_max_user_watches("524288");
	if (ret != 0) {
		fprintf(stderr, "fluffy_set_max_user_watches fail\n");
	}
	fprintf(stdout, "fluffy_set_max_user_watches done\n");

	ret = fluffy_set_max_queued_events("524288");
	if (ret != 0) {
		fprintf(stderr, "fluffy_set_max_queued_events fail\n");
	}
	fprintf(stdout, "fluffy_set_max_queued_events done\n");

	ret = fluffy_add_watch_path(fh1, argv[1]);
	if (ret != 0) {
		fprintf(stderr, "fluffy_add_watch_path fh1:%s fail\n", argv[1]);
	}
	fprintf(stdout, "fluffy_add_watch_path 1 done\n");

	ret = fluffy_add_watch_path(fh1, argv[2]);
	if (ret != 0) {
		fprintf(stderr, "fluffy_add_watch_path fh1:%s fail\n", argv[2]);
	}
	fprintf(stdout, "fluffy_add_watch_path 2 done\n");

	sleep(20);

	ret = fluffy_reinitiate_context(fh1);
	if (ret != 0) {
		fprintf(stderr, "fluffy_reinitiate_context fh1 fail\n");
	}
	fprintf(stdout, "fluffy_reinitiate_context done\n");

	sleep(20);

	ret = fluffy_reinitiate_all_contexts();
	if (ret != 0) {
		fprintf(stderr, "fluffy_reinitiate_all_contexts fail\n");
	}
	fprintf(stdout, "fluffy_reinitiate_all_contexts done\n");

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
	fprintf(stdout, "fluffy_wait_until_done ok\n");

	exit(EXIT_SUCCESS);
}
