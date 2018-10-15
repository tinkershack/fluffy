#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
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
#include <ftw.h>
#include <glib.h>
#include <gmodule.h>

#include "fluffy.h"

#define INOTIFY_EVENT_FLAGS	(IN_ALL_EVENTS	| \
				IN_EXCL_UNLINK	| \
				IN_DONT_FOLLOW	| \
				IN_ONLYDIR)
				/* IN_MASK_ADD	*/

#define FTW_FLAGS 		(FTW_PHYS	| \
				FTW_MOUNT	| \
				FTW_ACTIONRETVAL)
				/* FTW_DEPTH	*/
				/* FTW_CHDIR  	*/
				
#define NR_INOTIFY_EVENTS	200
#define NR_EPOLL_EVENTS		20
#define PRINT_STDOUT(fmt, ...)	\
                do { fprintf(stdout, fmt, __VA_ARGS__); \
			if (fflush(stdout)) perror("fflush"); \
		} while(0)

#define PRINT_STDERR(fmt, ...)	\
                do { fprintf(stderr, fmt, __VA_ARGS__); \
			if (fflush(stderr)) perror("fflush"); \
		} while(0)


/* Structure definitions */

/*
 * Struct:	fluffy_track_info
 *
 * This is statically allocated, only one instance. Holds setup info and
 * context info of every fluffy initiated instance.
 */
struct fluffy_track_info {
	int	idx;			/* fluffy_handle incrementer */
	unsigned int	nref;		/* Count of fluffy instances */
	unsigned int	is_init;	/* Check for first time setup */

	/*
	 * A hash table that holds context info of all fluffy instances.
	 *
	 * key:		type fluffy_context_info.handle
	 * value:	pointer of fluffy_context_info
	 */
	GHashTable 	*context_table;

	/*
	 * Used as a global reference of fluffy_context_info from within
	 * function scope where fluffy_handle could not be passed as an
	 * argument; nftw(). This must be accessed with care.
	 */
	struct fluffy_context_info *curr_ctxinfop;

	pthread_mutex_t mutex;	/* Mutex for this struct access */
};

/* Global initialization of tracking information */
struct fluffy_track_info fluffy_track = {
	1,				/* idx */
	0,				/* nref */
	0,				/* is_init */
	NULL,				/* context_table */
	NULL,				/* fluffy_context_info */
	PTHREAD_MUTEX_INITIALIZER};	/* pthread_mutex_t */

/*
 * Struct:	fluffy_context_info
 *
 * Records kept and maintained for a particular fluffy instance
 */
struct fluffy_context_info {
	int handle;			/* fluffy_track.idx */
	int is_persist;			/* Persist fluffy instance */
	int inotify_fd;			/* Associated inotify descriptor */
	int epoll_fd;			/* Associated epoll descriptor */
	unsigned long long nwd;		/* Count of watches set up */

	/*
	 * A hash table that holds watch descriptor info of all watch paths.
	 *
	 * key:		type fluffy_wd_info.wd
	 * value:	pointer of fluffy_wd_info
	 */
	GHashTable 	*wd_table;

	/*
	 * A hash table that holds watch descriptor info of all watch paths.
	 *
	 * key:		type fluffy_wd_info.path
	 * value:	pointer of fluffy_wd_info
	 */
	GHashTable 	*path_table;

	/*
	 * A search tree that's ordered by watch path. This is primarily used
	 * to lookup descendents of a path.
	 *
	 * key:		type fluffy_wd_info.path
	 * value:	NULL
	 */
	GTree 		*path_tree;

	/*
	 * A hash table that holds watch descriptor info of root watch paths.
	 * This contains only what was added through fluffy_add_watch_path().
	 *
	 * key:		type fluffy_wd_info.path
	 * value:	NULL
	 */
	GHashTable 	*root_path_table;

	/* A user defined function that is called on every appropriate event */
	int (*user_event_fn) (const struct fluffy_event_info *eventinfo,
	    void *user_data);

	void 	*user_data;	/* User argument passed to user_event_fn() */

	pthread_mutex_t mutex;		/* Mutex for this struct access */
	pthread_t tid;			/* Thread id of the context thread */
};

/*
 * Struct:	fluffy_wd_info
 *
 * Records kept and maintained for a particular inotify watch descriptor
 */
struct fluffy_wd_info {
	int 		wd;		/* Watch descriptor from inotify */
	uint32_t	mask;		/* Event mask on this wd */
	char 		*path;		/* Associated path */
};


/* Forward function declarations */

static struct fluffy_context_info *fluffy_context_info_new();

static struct fluffy_wd_info *fluffy_wd_info_new();

static void free_wd_table_info_g(struct fluffy_wd_info *wdinfop);

static void free_context_table_info_g(struct fluffy_context_info *ctxinfop);

static void watch_each_root_path_g(gpointer root_path, gpointer value,
    gpointer fluffy_handle);

static void reinit_each_context_g(gpointer root_path, gpointer value,
    gpointer fluffy_handle);

static gint search_tree_g(gpointer pathname, gpointer compare_path);

static char *form_event_path(char *wdpath, uint32_t ilen, char *iname);

static int dir_tree_add_watch(const char *pathname, const struct stat *sbuf,
    int type, struct FTW *ftwb);

static int fluffy_setup_context(int fluffy_handle);

static int fluffy_add_watch(int fluffy_handle, const char *pathtoadd, int
    is_real_path_check, int is_root_path);

static int fluffy_setup_track();

static int fluffy_is_track_setup();

static int fluffy_ref();

static int fluffy_unref(int fluffy_handle);

static void fluffy_destroy_context(void *fluffy_handle);

static struct fluffy_context_info *fluffy_get_context_info(int fluffy_handle);

static int fluffy_setup_context_info_records(int fluffy_handle);

static int fluffy_cleanup_context_info_records(int fluffy_handle);

static int fluffy_initiate_epoll(int fluffy_handle);

static int fluffy_initiate_inotify(int fluffy_handle);

static int fluffy_is_root_path(int fluffy_handle, char *path);

static int fluffy_handle_qoverflow(int fluffy_handle);

static int fluffy_handle_removal(int fluffy_handle, char *removethis);

static int fluffy_handle_addition(int fluffy_handle,
    struct inotify_event *ievent, struct fluffy_wd_info *wdinfop);

static int fluffy_handle_moved_from(int fluffy_handle,
    struct inotify_event *ievent, struct fluffy_wd_info *wdinfop);
;
static int fluffy_handle_ignored(int fluffy_handle,
    struct inotify_event *ievent, struct fluffy_wd_info *wdinfop);

static int fluffy_handoff_event(int fluffy_handle,
    struct inotify_event *ievent, struct fluffy_wd_info *wdinfop);

static int fluffy_process_inotify_queue(int fluffy_handle,
    struct epoll_event *evlist);

static void fluffy_thread_cleanup_unlock(void *mutex);

static void *fluffy_start_context_thread(void *flhandle);

/* Notes & other relevant stuff */

/*
 * NOTE:
 *
 * pthread_cleanup_pop() must be in the same lexical scope as that
 * of pthread_cleanup_push(). This means that the convenience of
 * returning from anywhere, regardless of block scope, is lost. Bummer.
 *
 * Employ do-while(0) to break out and reach the deferred return.
 */


/* function definitions */

/*
 * fluffy.h contains this function description
 */
int
fluffy_set_max_queued_events(const char *maxvalp)
{
	int fd = -1;
	ssize_t wrbytes;

	if (maxvalp == NULL) {
		return FLUFFY_ERROR_INVALID_ARG;
	}

	fd = open("/proc/sys/fs/inotify/max_queued_events",
		O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd == -1) {
		return FLUFFY_ERROR_OPEN_QUEUE_FAILED;
	}

	wrbytes = write(fd, maxvalp, strlen(maxvalp));
	if(wrbytes == -1 || (size_t)wrbytes != strlen(maxvalp)) {
		close(fd);
		return FLUFFY_ERROR_WRITE_QUEUE_FAILED;
	}

	if (close(fd) != 0) {
		return FLUFFY_ERROR_CLOSE_QUEUE_FAILED;
	}

	/* Reinitialize all fluffy context to pick up the newly set value */
	/*
	if(fluffy_reinitiate_all_contexts()) {
		return -1;
	}
	*/

	return 0;
}

/*
 * fluffy.h contains this function description
 */
int
fluffy_set_max_user_instances(const char *maxvalp)
{
	int fd = -1;
	ssize_t wrbytes;

	if (maxvalp == NULL) {
		return FLUFFY_ERROR_INVALID_ARG;
	}

	fd = open("/proc/sys/fs/inotify/max_user_instances",
		O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd == -1) {
		return FLUFFY_ERROR_OPEN_QUEUE_FAILED;
	}

	wrbytes = write(fd, maxvalp, strlen(maxvalp));
	if(wrbytes == -1 || (size_t)wrbytes != strlen(maxvalp)) {
		close(fd);
		return FLUFFY_ERROR_WRITE_QUEUE_FAILED;
	}

	if (close(fd) != 0) {
		return FLUFFY_ERROR_CLOSE_QUEUE_FAILED;
	}

	return 0;
}

/*
 * fluffy.h contains this function description
 */
int
fluffy_set_max_user_watches(const char *maxvalp)
{
	int fd = -1;
	ssize_t wrbytes;

	if (maxvalp == NULL) {
		return FLUFFY_ERROR_INVALID_ARG;
	}

	fd = open("/proc/sys/fs/inotify/max_user_watches",
		O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd == -1) {
		return FLUFFY_ERROR_OPEN_QUEUE_FAILED;
	}

	wrbytes = write(fd, maxvalp, strlen(maxvalp));
	if(wrbytes == -1 || (size_t)wrbytes != strlen(maxvalp)) {
		close(fd);
		return FLUFFY_ERROR_WRITE_QUEUE_FAILED;
	}

	if (close(fd) != 0) {
		return FLUFFY_ERROR_CLOSE_QUEUE_FAILED;
	}

	return 0;
}


static struct fluffy_wd_info *
fluffy_wd_info_new()
{
	struct fluffy_wd_info *wdinfop;
	wdinfop = calloc(1, sizeof(struct fluffy_wd_info));
	if (wdinfop == NULL) {
		perror("calloc in fluffy_wd_info_new(), FILE: fluffly.c");
		return NULL;
	}
	wdinfop->wd	= 0;
	wdinfop->mask	= 0;
	wdinfop->path	= NULL;

	return wdinfop;
}

/*
 * Function:	fluffy_context_info_new
 *
 * Allocate memory for a fluffy_context_info and assign appropriate values.
 *
 * args:
 * 	- None
 * return:
 * 	- A pointer to fluffy_context_info when successful, NULL otherwise
 */
static struct fluffy_context_info *
fluffy_context_info_new()
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = calloc(1, sizeof(struct fluffy_context_info));
	if (ctxinfop == NULL) {
		perror("calloc in fluffy_context_info_new(), FILE: fluffly.c");
		/*
		 * When OOM is not handled, it's a common practice to call
		 * exit() to bail out. We will return NULL instead of exit()
		 * to hand off the reponsibility to the caller.
		 */
		return NULL;
	}

	if (pthread_mutex_init(&ctxinfop->mutex, NULL)) {
		free(ctxinfop);
		return NULL;
	}

	ctxinfop->is_persist	= 0;
	ctxinfop->inotify_fd	= -1;
	ctxinfop->epoll_fd	= -1;
	ctxinfop->nwd		= 0;
	ctxinfop->handle	= -1;

	ctxinfop->user_data	= NULL;
	ctxinfop->wd_table	= NULL;
	ctxinfop->path_table	= NULL;
	ctxinfop->path_tree	= NULL;
	ctxinfop->root_path_table = NULL;

	return ctxinfop;
}

/*
 * Function:	free_context_table_info_g
 *
 * Called by glib when an entry is removed from
 * fluffy_track.context_table. Frees up records.
 */
static void
free_context_table_info_g(struct fluffy_context_info *ctxinfop)
{
	free(ctxinfop->wd_table);
	free(ctxinfop->path_table);
	free(ctxinfop->path_tree);
	free(ctxinfop->root_path_table);
	free(ctxinfop);
}


static void
free_wd_table_info_g(struct fluffy_wd_info *wdinfop)
{
	free(wdinfop->path);
	free(wdinfop);
}

/*
 * Function:	fluffy_handoff_event
 *
 * Each inotify event is handed off to the client callback function
 * before internal processing. Since the event processing within a context is
 * serial, the client will have to fork/thread and return as soon as a possible
 * in order to avoid event queue overflows; that is if event order isn't a
 * concern. To main event order, serial processing is the only way.
 *
 * args:
 * 	- int: fluffy context handle
 * 	- struct inotify_event *: a pointer to the inotify event
 * 	- struct fluffy_wd_info *: a pointer to the associated watch info. Null
 * 		when the event is a queue overflow
 * return:
 * 	- int: 0 when successful, error value otherwise to terminate context
 */
static int
fluffy_handoff_event(int fluffy_handle, struct inotify_event *ie,
    struct fluffy_wd_info *wdinfop)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}
	if (ctxinfop->user_event_fn == NULL) {
		return 0;
	}

	/*
	 * Along with the appropriate inotify event mask, OR fluffy context
	 * event mask when required. This mask will be passed on to the client
	 * event callback function.
	 */
	uint32_t handoff_mask = ie->mask;
	
	int is_not_root = 0;	/* Positive when it isn't a root path */
	char *eventpathp = NULL;

	/* No event path when it's a queue overflow event */
	if (!(ie->mask & IN_Q_OVERFLOW)) {
		is_not_root = fluffy_is_root_path(fluffy_handle,
				wdinfop->path);

		eventpathp = form_event_path(wdinfop->path,
				ie->len,
				ie->name);
		if (eventpathp == NULL) {
			return FLUFFY_ERROR_NO_MEMORY; // form_event_path will only fail for this reason. 18/06/19
		}

		/*
		 * Ignore IN_MOVE_SELF & IN_DELETE_SELF unless it's on the root
		 * path. The parent watch path will catch these events and
		 * report them, including this regardless is redundant.
		 */
		if ((is_not_root)		&&
		    ((ie->mask & IN_MOVE_SELF)	||
		    (ie->mask & IN_DELETE_SELF))) {
			free(eventpathp);
			return 0;
		}

		/*
		 * If event path is a root path and the watch on it is removed,
		 * send a FLUFFY_ROOT_IGNORED event. If there's no more paths
		 * to watch, send a FLUFFY_WATCH_EMPTY event as well.
		 */
		if ((is_not_root == 0)	&&
		    (ie->mask & IN_IGNORED)) {
			handoff_mask |= FLUFFY_ROOT_IGNORED;
			if(g_hash_table_size(ctxinfop->root_path_table) == 1) {
				handoff_mask |= FLUFFY_WATCH_EMPTY;
			}
		}

		/*
		 * If there's an event on a dir, and there's a watch set on its
		 * parent directory, then the events are reported twice- one by
		 * the child, one by the parent. Don't report twice.
		 */
		if (!(ie->mask & IN_MODIFY)	&&
		    !(ie->mask & IN_MOVED_FROM)	&&
		    !(ie->mask & IN_MOVED_TO)	&&
		    (ie->mask & IN_ISDIR)	&&
		    (ie->len > 0)) {
			if (g_hash_table_contains(ctxinfop->path_table,
			    eventpathp)) {
				free(eventpathp);
				return 0;
			}
		}
	}

	/* Pass on the event info to the client's callback function. */
	struct fluffy_event_info *evtinfop;
	evtinfop = calloc(1, sizeof(struct fluffy_event_info));
	if (evtinfop == NULL) {
		free(eventpathp);
		pthread_exit((void *)FLUFFY_ERROR_NO_MEMORY);
		// TODO: previous FLUFFY_ERROR_NO_MEMORY just return error code.
	}
	evtinfop->event_mask = handoff_mask;
	evtinfop->path = eventpathp;

	int ret = 0;
	ret = (ctxinfop->user_event_fn)(evtinfop, (void *)ctxinfop->user_data);

	free(eventpathp);
	free(evtinfop);
	return ret;	/* return whatever the client returned */

}

/*
 * Function:	dir_tree_add_watch
 *
 * This function must not be called directly, rather passed as a function
 * pointer to nftw() which in turn calls this function on every path it
 * encounters.
 *
 * args:
 * 	nftw() provides the arguments
 * return:
 * 	- int: 0 to continue the tree walk, none zero to terminate the walk
 */
static int
dir_tree_add_watch(const char *pathname, const struct stat *sbuf, int type,
    struct FTW *ftwb)
{
	/* Process only if the entry is a directory */
	if (!(type == FTW_DP || type == FTW_D)) {
		return FTW_CONTINUE;
	}

	struct fluffy_context_info *ctxinfop = NULL;
	ctxinfop = fluffy_get_context_info(fluffy_track.curr_ctxinfop->handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int reterr = 0;
	int iwd = -1;
	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	if (ftwb->level != 0) {
		if (g_hash_table_contains(ctxinfop->root_path_table,
		    pathname)) {
			/*
			 * This had been a root watch path previously.
			 * Since it is a regular descendant path now, remove
			 * it from the root path list.
			 */
			if (!g_hash_table_remove(ctxinfop->root_path_table,
			    (const char *)pathname)) {
				PRINT_STDERR("Couldnot remove %s from the " \
				    "root table\n", pathname);
			}
		} else {
			/*
			 * This path is not in the root path list, don't
			 * have to remove anything now.
			 */
		}
	}

	do {
		iwd = inotify_add_watch(ctxinfop->inotify_fd, pathname,
				INOTIFY_EVENT_FLAGS);
		if (iwd == -1) {
			perror("inotify_add_watch in dir_tree_add_watch(), FILE: fluffly.c");
			reterr = FLUFFY_ERROR_INOTIFY_ADD_WATCH;
			break;
		}

		if(g_hash_table_contains(ctxinfop->wd_table,
		    GINT_TO_POINTER(iwd))) {
			/* Entry already exists */
			struct fluffy_wd_info *oldwdinfop = NULL;
			oldwdinfop = (struct fluffy_wd_info *)
					g_hash_table_lookup(ctxinfop->wd_table,
						GINT_TO_POINTER(iwd));
			if (oldwdinfop->wd != iwd) {
				oldwdinfop->wd = iwd;
			}

			if (oldwdinfop->mask != INOTIFY_EVENT_FLAGS) {
				oldwdinfop->mask = INOTIFY_EVENT_FLAGS;
			}

			if (oldwdinfop->path != pathname) {
				oldwdinfop->path = strdup(pathname);
			}
			oldwdinfop = NULL;

			break;
		}

		/* Table does not hold this entry already */
		(ctxinfop->nwd)++;

		/* Add the new entry */
		struct fluffy_wd_info *wdinfop;
		wdinfop = fluffy_wd_info_new();
		if (wdinfop == NULL) {
			reterr = FLUFFY_ERROR_NO_MEMORY; // fluffy_wd_info_new will only fail for this reason. 18/06/19
			break;
		}

		wdinfop->wd = iwd;
		wdinfop->mask = INOTIFY_EVENT_FLAGS;
		wdinfop->path = strdup(pathname);
		if (wdinfop->path == NULL) {
			perror("strdup in dir_tree_add_watch(), FILE fluffy.c");
			reterr = FLUFFY_ERROR_NO_MEMORY;
			break;
		}

		g_hash_table_replace(ctxinfop->wd_table,
		    GINT_TO_POINTER(wdinfop->wd), wdinfop);
		g_hash_table_replace(ctxinfop->path_table,
		    strdup(wdinfop->path), wdinfop);
		g_tree_replace(ctxinfop->path_tree,
		    strdup(wdinfop->path),
		    GINT_TO_POINTER(wdinfop->wd));
	} while(0);

	pthread_cleanup_pop(1);		/* Unlock mutex */

	if (reterr) { // TODO: make sure FTW_CONTINUE does not collide with error code. (neold2022)
		return reterr;
	} else {
		return FTW_CONTINUE;
	}
}


static void
watch_each_root_path_g(gpointer root_path, gpointer value,
    gpointer fluffy_handle)
{
	if (root_path == NULL) {
		pthread_exit((void *)FLUFFY_ERROR_INVALID_ARG); // TODO: Is it correct here? (neold2022)
	}

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(GPOINTER_TO_INT(fluffy_handle));
	if (ctxinfop == NULL) {
		pthread_exit((void *)FLUFFY_ERROR_CTXINFOP);
	}

	int reterr = 0;
	reterr = fluffy_add_watch(GPOINTER_TO_INT(fluffy_handle),
			(char *)root_path,
			0,
			0);
	if (reterr) {
		pthread_exit((void *)((size_t)reterr)); // remove warning
	}
}


static void
reinit_each_context_g(gpointer fluffy_handle, gpointer value,
    gpointer user_data)
{
	int reterr = 0;
	reterr = fluffy_reinitiate_context(GPOINTER_TO_INT(fluffy_handle));
	if (reterr) {
		/* nothing? */
	}
}

/*
 * Function:	search_tree_g
 *
 * This function is not called directly, rather glib g_search_tree function
 * calls this on every entry it encounters from the tree. The return value
 * decides the search path because the tree is a search tree; ordered.
 *
 * args:
 * 	- gpointer: entry from the search tree
 * 	- gpointer: string to compare the path prefix
 * return:
 *	- gint: 0,-1 or 1 appropriately. Same logic as that of strmp()'s
 */
static gint
search_tree_g(gpointer pathname, gpointer compare_path)
{
	char *cmp_for_each_path = (char *)compare_path;
	char *tocmp = NULL;
	size_t cmp_size = 0;
	tocmp = calloc(1, strlen(cmp_for_each_path) + 2);
	if (tocmp == NULL) {
		perror("calloc in search_tree_g(), FILE: fluffy.c");
		pthread_exit((void *)FLUFFY_ERROR_NO_MEMORY);
	}

	/*
	 * compare_path is the prefix string that we will match with pathname.
	 * Each descendant will be a directory, so, '/' is the separator we
	 * must concatenate before performing the comparison.
	 */
	tocmp = strncpy(tocmp, cmp_for_each_path, strlen(cmp_for_each_path) +
		    1);
	tocmp = strncat(tocmp, "/", 2);
	cmp_size = strlen(tocmp);

	int cmp_ret = 0;
	cmp_ret = strncmp(tocmp, (char *)pathname, cmp_size);
	free(tocmp);
	tocmp = NULL;
	if (cmp_ret == 0) {
		return 0;
	} else if (cmp_ret < 0) {
		return -1;
	} else if (cmp_ret > 0) {
		return 1;
	}
	return cmp_ret;
}

/*
 * Function:	form_event_path
 *
 * Form the event path by appending the filename where the action occured with
 * the path which is reporting it. inotify watch descriptor is on the reporting
 * path. If the action occured on the reporting path itself, then there's
 * nothing to append because ilen will be zero and iname will point to nothing.
 *
 * args:
 * 	- char *:	pointer to the reporting path
 * 	- uint32_t:	length of the filename in concern if it's a descendant
 * 	- char *:	pointer to the descendant filename if any
 * return:
 * 	- char *:	pointer to the event path, must be freed by the caller
 */
static char *
form_event_path(char *wdpath, uint32_t ilen, char *iname)
{
	char *currpath = NULL;
	currpath = calloc(1, (size_t) (strlen(wdpath) + 1 
				+ ((ilen) ?  ilen : 0) + 2));
	if (currpath == NULL) {
		perror("calloc failed in form_event_path(), FILE: fluffy.c");
		return NULL;
	}

	currpath = strncpy(currpath, wdpath, strlen(wdpath) + 1);
	if (ilen) {
		/* Action was on a descendant file, so, append it. */
		currpath = strncat(currpath, "/", 2);
		currpath = strncat(currpath, iname, strlen(iname) + 1);
	}
	return currpath;
}

/*
 * Function:	fluffy_add_watch
 *
 * Watch a path recursively.
 *
 * args:
 * 	- int: fluffy_context handle
 * 	- const char *: path to watch recursively
 * 	- int is_real_path_check: non zero value when the path needs to be a 
 * 		canonicalized absolute path
 * 	- int is_root_path: non zero value if the path is a root path
 * return:
 * 	- int: 0 when successful, error value otherwise
 */
static int
fluffy_add_watch(int fluffy_handle, const char *pathtoadd,
    int is_real_path_check, int is_root_path)
{
	char *addpath = NULL;
	int reterr = 0;

	/*
	 * Check whether the path is an input argument received from the user.
	 * All paths received from the client/user directly must be checked &
	 * transformed to a real path.
	 */
	if (is_real_path_check) {
		addpath = realpath(pathtoadd, NULL);
		if (addpath == NULL) {
			reterr = FLUFFY_ERROR_REAL_PATH_RESOLVE;
			perror("realpath in fluffy_add_watch(), FILE: fluffy.c");
			return reterr;
		}
	} else {
		/*
		 * This path need not be resolved. It's assumed that it has
		 * been resolved already.
		 */
		addpath = strdup(pathtoadd);
		if (addpath == NULL) {
			reterr = FLUFFY_ERROR_NO_MEMORY;
			perror("strdup in fluffy_add_watch(), FILE: fluffy.c");
			return reterr;
		}
	}

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		free(addpath);
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		free(addpath);
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	/* Add appropriate records if it's a root path */
	if (is_root_path) {
		/* Add only if the path is not in the watch list already */
		if (!g_hash_table_contains(ctxinfop->path_table,
		    addpath)) {
			g_hash_table_replace(ctxinfop->root_path_table,
			    strdup(addpath), NULL);
		} else {
			/*
			 * This path is already a descendent of another root
			 * path, so, don't add a new root path entry
			 */
		}
	}

	pthread_cleanup_pop(1);		/* Unlock mutex */

	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		free(addpath);
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	do {
		fluffy_track.curr_ctxinfop = ctxinfop;

		/* Watch the path recursively */
		if (nftw(addpath, dir_tree_add_watch, 30, FTW_FLAGS) == -1) {
			reterr = FLUFFY_ERROR_NFTW_WALKING;
			perror("nftw in fluffy_add_watch(), FILE: fluffy.c");
			break;
		}
	} while(0);
	fluffy_track.curr_ctxinfop = NULL;
	pthread_cleanup_pop(1);		/* Unlock mutex */
	free(addpath);
	return reterr;
}

/*
 * Function:	fluffy_cleanup_context_info_records
 *
 * The intention is to clean up the context records, not to destroy them.
 *
 * args:
 * 	- int: fluffy context handle
 * return:
 * 	- int: 0 when successful, error value otherwise
 */
static int
fluffy_cleanup_context_info_records(int fluffy_handle)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	int reterr = 0;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}
	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		/* Close inotify. This will be reinitiated if required */
		if (close(ctxinfop->inotify_fd) == -1) {
			reterr = FLUFFY_ERROR_CLOSE_INOTIFY_FD;
			perror("close in fluffy_cleanup_context_info_records(), FILE: fluffy.c");
			break;
		}
		ctxinfop->nwd = 0;
		
		g_hash_table_remove_all(ctxinfop->wd_table);
		g_hash_table_remove_all(ctxinfop->path_table);
		/* There's is no 'remove all' function call for glib trees */
		g_tree_destroy(ctxinfop->path_tree);
		ctxinfop->path_tree = NULL;
		ctxinfop->path_tree = g_tree_new_full((GCompareDataFunc)strcmp,
					NULL, (GDestroyNotify)free, NULL);
	} while (0);
	pthread_cleanup_pop(1);		/* Unlock mutex */
	return reterr;
}


/*
 * Function:	fluffy_setup_context_info_records
 *
 * Setup the required bookkeeping records(tables & trees) of a fluffy context.
 *
 * args:
 * 	int - fluffy_context_info.handle
 * return:
 * 	int - 0 if successful, error value otherwise
 */
static int
fluffy_setup_context_info_records(int fluffy_handle)
{
	int ret = 0;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		/*
		 * All records required for the context are created here.
		 * For more info, refer fluffy_context_info structure
		 * definition block.
		 */
		ctxinfop->wd_table	= NULL;
		ctxinfop->path_table	= NULL;
		ctxinfop->path_tree	= NULL;
		ctxinfop->root_path_table = NULL;

		ctxinfop->wd_table = g_hash_table_new_full(
					g_direct_hash,
					g_direct_equal,
					NULL,
					(GDestroyNotify)free_wd_table_info_g);
		if (ctxinfop->wd_table == NULL) {
			ret = FLUFFY_ERROR_G_HASH_TABLE;
			break;
		}

		ctxinfop->path_table = g_hash_table_new_full(
					g_str_hash,
					g_str_equal,
					(GDestroyNotify)free,
					NULL);
		if (ctxinfop->path_table == NULL) {
			ret = FLUFFY_ERROR_G_HASH_TABLE;
			break;
		}

		ctxinfop->root_path_table = g_hash_table_new_full(
						g_str_hash,
						g_str_equal,
						(GDestroyNotify)free,
						NULL);
		if (ctxinfop->root_path_table == NULL) {
			ret = FLUFFY_ERROR_G_HASH_TABLE;
			break;
		}

		ctxinfop->path_tree = g_tree_new_full(
					(GCompareDataFunc)strcmp,
					NULL,
					(GDestroyNotify)free,
					NULL);
		if (ctxinfop->path_tree == NULL) {
			ret = FLUFFY_ERROR_G_TREE;
			break;
		}
	} while(0);

	pthread_cleanup_pop(1);		/* Unlock mutex */
	return ret;
}


/*
 * Function:	fluffy_initiate_inotify
 *
 * Initiate an inotify instance for this fluffy context identified by
 * the fluffy_handle. Also, add the received descriptor to the context's
 * epoll instance.
 *
 * args:
 * 	int - fluffy_context_info.handle
 * return:
 * 	int - 0 if successful, error value otherwise
 */
static int
fluffy_initiate_inotify(int fluffy_handle) 
{
	int ret = 0;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		/* Initialize inotify, get its descriptor */
		ctxinfop->inotify_fd = inotify_init1(IN_CLOEXEC);
		if (ctxinfop->inotify_fd == -1) {
			ret = FLUFFY_ERROR_INIT_INOTIFY_FD;
			break;
		}

		/* Poll for activity on the inotify descriptor */
		struct epoll_event evtmp = {0};
		evtmp.events	   = EPOLLIN;
		evtmp.data.fd	   = ctxinfop->inotify_fd;
		if (epoll_ctl(
		    ctxinfop->epoll_fd,
		    EPOLL_CTL_ADD,
		    ctxinfop->inotify_fd,
		    &evtmp) == -1) {
			ret = FLUFFY_ERROR_EPOLL_CTL;
			break;
		}
	} while (0);

	pthread_cleanup_pop(1);		/* Unlock mutex */
	return ret;
}

/*
 * Function:	fluffy_destroy_context
 *
 * Releases resources attached with a fluffy context
 *
 * args:
 * 	- void *: fluffy context handle carrying integer value
 * return:
 * 	- void
 */
static void
fluffy_destroy_context(void *flhandle)
{
	int m = -1;
	int fluffy_handle = *(int *)flhandle;	/* extract int value */

	do {
		struct fluffy_context_info *ctxinfop;
		ctxinfop = fluffy_get_context_info(fluffy_handle);
		if (ctxinfop == NULL) {
			break;
		}

		/* Clean up the records */
		if (fluffy_cleanup_context_info_records(fluffy_handle)) {
			/* best effort */
		}

		m = pthread_mutex_lock(&ctxinfop->mutex);
		if (m != 0) {
			break;
		}

		pthread_cleanup_push(fluffy_thread_cleanup_unlock,
		    &ctxinfop->mutex);

		if (close(ctxinfop->epoll_fd) == -1) {
			perror("close in fluffy_destroy_context(), FILE: fluffy.c");
			/* best effort */
		}

		/* Destroy the cleaned up resources */
		g_hash_table_destroy(ctxinfop->wd_table);
		ctxinfop->wd_table = NULL;
		g_hash_table_destroy(ctxinfop->path_table);
		ctxinfop->path_table = NULL;
		g_hash_table_destroy(ctxinfop->root_path_table);
		ctxinfop->root_path_table = NULL;
		g_tree_destroy(ctxinfop->path_tree);
		ctxinfop->path_tree = NULL;
		pthread_cleanup_pop(1);
	} while(0);

	m = fluffy_unref(fluffy_handle);
	if (m != 0) {
		/* can't do anything about it */
	}
}


int
fluffy_reinitiate_all_contexts()
{
	if (fluffy_is_track_setup()) {
		return FLUFFY_ERROR_TRACK_NOT_INIT;
	}

	if(g_hash_table_size(fluffy_track.context_table) > 0) {
		g_hash_table_foreach(fluffy_track.context_table,
		    (GHFunc) reinit_each_context_g, NULL);
	}

	return 0;
}

/*
 * Function:	fluffy_reinitate_context
 *
 * Cleanup the records and reset watches on the root paths
 *
 * args:
 * 	- int: fluffy context handle
 * return:
 * 	- int: 0 when successful, error value otherwise
 */
int
fluffy_reinitiate_context(int fluffy_handle)
{
	int reterr = 0;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	/* Cleanup the context records and close the inotify instance */
	reterr = fluffy_cleanup_context_info_records(fluffy_handle);
	if (reterr) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	/* Acquire a fresh queue */
	reterr = fluffy_initiate_inotify(fluffy_handle);
	if (reterr) {
		return reterr;
	}

	/* Set watches on the root paths */
	g_hash_table_foreach(ctxinfop->root_path_table,
	    (GHFunc) watch_each_root_path_g, GINT_TO_POINTER(fluffy_handle));

	return 0;
}


/*
 * Function:	fluffy_initiate_epoll
 *
 * Creates an epoll instance for this fluffy context. Does not poll on
 * a fd yet, no epoll_ctl.
 *
 * args:
 * 	int - fluffy_context_info.handle
 * return:
 * 	int - 0 if successful, error value otherwise
 */
static int
fluffy_initiate_epoll(int fluffy_handle)
{
	int ret = 0;
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	ctxinfop->epoll_fd = epoll_create(3);
	if (ctxinfop->epoll_fd == -1) {
		ret = FLUFFY_ERROR_EPOLL_CREATE;
	}

	pthread_cleanup_pop(1);		/* Unlock mutex */
	return ret;
}


/*
 * Function:	fluffy_setup_context
 *
 * Initialize/create context records, epoll instance to listen on the
 * initialized inotify instance for this fluffy_context_info.
 *
 * args:
 * 	int - fluffy_context_info.handle
 * return:
 * 	int - 0 on success, error value otherwise
 */
static int
fluffy_setup_context(int fluffy_handle)
{
	int reterr = 0;

	reterr = fluffy_setup_context_info_records(fluffy_handle);
	if (reterr) {
		return reterr;
	}

	reterr = fluffy_initiate_epoll(fluffy_handle);
	if (reterr) {
		return reterr;
	}

	reterr = fluffy_initiate_inotify(fluffy_handle);
	if (reterr) {
		return reterr;
	}

	return 0;
}

/*
 * Function:	fluffy_is_root_path
 *
 * Check whether the path is a root path. Root paths are what the client
 * adds to the watch.
 *
 * args:
 * 	- int:	fluffy context handle
 * 	- char *: a char pointer to the path
 * return:
 * 	- int:	0 when it's a root path, error value otherwise
 */
static int
fluffy_is_root_path(int fluffy_handle, char *path)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	int reterr = 0;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		if (!g_hash_table_contains(ctxinfop->root_path_table, path)) {
			reterr = FLUFFY_ERROR_GENERIC; // TODO: internal error?
			break;
		}
	} while(0);
	pthread_cleanup_pop(1);		/* Unlock mutex */
	return reterr;
}

/*
 * Function:	fluffy_handle_qoverflow
 *
 * On an IN_Q_OVERFLOW event, reinitate inotify instance and the associated
 * records. This is to setup watches from scratch again because events were
 * possibly dropped when the queue was overflow. 
 *
 * args:
 * 	- int: fluffy context handle
 * return:
 * 	- int: 0 when successful, error value otherwise
 */
static int
fluffy_handle_qoverflow(int fluffy_handle)
{
	int reterr = 0;
	/*
	 * TODO:
	 * This decision must be made by the user. Bring in a field to
	 * accommodate this preference. Context termination probably makes
	 * more sense than reinitation as a default action unless otherwise the
	 * user prefers to reinitiate.
	 */
	reterr = fluffy_reinitiate_context(fluffy_handle);
	if (reterr) {
		PRINT_STDERR("Reinitiation failed!\n", "");
		return reterr;
	}
	return 0;
}


/*
 * Function:	fluffy_handle_addition
 *
 * Set watch on the path. A wrapper function of fluffy_add_watch
 *
 * args:
 * 	- int: fluffy context handle
 * 	- struct inotify_event *: a pointer to the inotify event
 * 	- struct fluffy_wd_info *: a pointer to the associated watch info
 * return:
 * 	- int: 0 when successful, error value otherwise to terminate context
 */
static int
fluffy_handle_addition(int fluffy_handle, struct inotify_event *ievent,
    struct fluffy_wd_info *wdinfop)
{
	int reterr = 0;
	char *currpath = NULL;
	currpath = form_event_path(wdinfop->path,
			ievent->len,
			ievent->name);
	if (currpath == NULL) {
		return FLUFFY_ERROR_NO_MEMORY; // form_event_path will only fail for this reason. 18/06/19
	}

	/* Since this path is already in our records, it's a real path */
	reterr = fluffy_add_watch(fluffy_handle, currpath, 0, 0);
	if (reterr) {
		PRINT_STDERR("%s\n", strerror(reterr));
	}

	free(currpath);
	currpath = NULL;
	return reterr;
}

/*
 * Function:	fluffy_handle_ignored
 *
 * When an ignored event is caught, it means the path is not under inotify's
 * watch anymore. Cleanup fluffy records to remove the path to prevent holding
 * stale entries.
 *
 * args:
 * 	- int: fluffy context handle
 * 	- struct inotify_event *: a pointer to the inotify event
 * 	- struct fluffy_wd_info *: a pointer to the associated watch info
 * return:
 * 	- int: 0 when successful, error value otherwise to terminate context
 */
static int
fluffy_handle_ignored(int fluffy_handle, struct inotify_event *ievent,
    struct fluffy_wd_info *wdinfop)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	char *tp;
	tp = strdup(wdinfop->path);
	if (tp == NULL) {
		return FLUFFY_ERROR_NO_MEMORY;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		free(tp);
		return FLUFFY_ERROR_CTXINFOP;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	if (!g_hash_table_remove(ctxinfop->path_table, (const char *)tp)) {
		PRINT_STDERR("Couldnot remove %s from the table\n", \
				tp);
	}
	if (!g_hash_table_remove(ctxinfop->wd_table,
	    GINT_TO_POINTER(ievent->wd))) {
		PRINT_STDERR("Couldnot remove %d from the table\n", \
				ievent->wd);
	}

	g_hash_table_remove(ctxinfop->root_path_table, (const char *)tp);

	(ctxinfop->nwd)--;
	pthread_cleanup_pop(1);		/* Unlock mutex */
	free(tp);

	return 0;
}

/*
 * Function:	fluffy_handle_moved_from
 *
 * A wrapper to fluffy_handle_removal
 *
 * args:
 * 	- int: fluffy context handle
 * 	- struct inotify_event *: a pointer to the inotify event
 * 	- struct fluffy_wd_info *: a pointer to the associated watch info
 * return:
 * 	- int: 0 when successful, error value otherwise to terminate context
 */
static int
fluffy_handle_moved_from(int fluffy_handle, struct inotify_event *ievent,
    struct fluffy_wd_info *wdinfop)
{
	int reterr = 0;
	char *movepath = NULL;
	movepath = form_event_path(wdinfop->path,
				ievent->len, ievent->name);
	if (movepath == NULL) {
		return FLUFFY_ERROR_NO_MEMORY; // form_event_path will only fail for this reason. 18/06/19
	}

	reterr = fluffy_handle_removal(fluffy_handle, movepath);
	if (reterr) {
		/* Nothing? */
	}

	free(movepath);
	movepath = NULL;

	return  reterr;
}

/*
 * Function:	fluffy_handle_removal
 *
 * Remove the watches on a path recursively.
 *
 * args:
 * 	- int: fluffy context handle
 * return:
 * 	- char *: path to remove watch recursively
 */
static int
fluffy_handle_removal(int fluffy_handle, char *removethis)
{
	char *cmp_for_each_path = removethis;
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int reterr = 0;
	struct fluffy_wd_info *toremwd;
	gpointer wdtp = NULL;

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	/*
	 * Say, the path to be removed is /hogwarts/dungeons
	 *
	 * Dir tree is 
	 * 	- /hogwarts/dungeons/
	 * 		- /hogwarts/dungeons/eastwing
	 * 		- /hogwarts/dungeons/grounds
	 * 		- /hogwarts/dungeons/lakeside
	 * 		- /hogwarts/dungeons/southwing
	 *
	 * First /hogwarts/dungeons will be removed from fluffy records. After
	 * which, the path tree will be searched for strings that start with
	 * /hogwarts/dungeons/. One by one, each of the descendants are
	 * removed until there's none left.
	 */
	do {
		/* Get the watch descriptor of the path to be removed */
		toremwd = (struct fluffy_wd_info *)g_hash_table_lookup(
				ctxinfop->path_table, cmp_for_each_path);
		if (toremwd == NULL) {
			PRINT_STDERR("Could not lookup path %s\n", \
			    cmp_for_each_path);
			cmp_for_each_path = NULL;
			reterr = FLUFFY_ERROR_G_HASHTABLE_LOOKUP;
			break;
		}

		/* Remove fluffy records of /hogwarts/dungeons */
		if (!g_tree_remove(ctxinfop->path_tree,
		    (const char *)cmp_for_each_path)) {
			PRINT_STDERR("Couldnot remove %s from the tree\n", \
					cmp_for_each_path);
		}

		int iwdtmp = -1;
		/* Remove inotify watch on /hogwarts/dungeons */
		iwdtmp = inotify_rm_watch(ctxinfop->inotify_fd, toremwd->wd);
		if (iwdtmp == -1) {
			reterr = FLUFFY_ERROR_RM_INOTIFY_WATCH;
			perror("inotify_rm_watch(1) in fluffy_handle_removal(), FILE: fluffy.c");
			cmp_for_each_path = NULL;
			break;
		}
		toremwd = NULL;

		/* Lookup for descendants one after another and remove */
		while (1) {
			/* Return the first descendant entry that matches */
			wdtp = g_tree_search(ctxinfop->path_tree,
					(GCompareFunc)search_tree_g,
					(gpointer)cmp_for_each_path);
			if (wdtp == NULL) {
				break;
			}

			/*
			 * On the first iteration /hogwarts/dungeons/eastwing
			 * will be returned. On consequent iterations, other
			 * descendants will be found and returned as well.
			 */

			int iwd = -1;
			/* Remove the inotify watch on the returned entry */
			iwd = inotify_rm_watch(ctxinfop->inotify_fd,
					GPOINTER_TO_INT(wdtp));
			if (iwd == -1) {
				reterr = FLUFFY_ERROR_RM_INOTIFY_WATCH;
				perror("inotify_rm_watch(2) in fluffy_handle_removal(), FILE: fluffy.c");
				cmp_for_each_path = NULL;
				break;
			}

			struct fluffy_wd_info *thiswd;
			thiswd = (struct fluffy_wd_info *)g_hash_table_lookup(
					ctxinfop->wd_table, wdtp);
			if (thiswd == NULL) {
				PRINT_STDERR("Could not lookup wd %d\n", \
						GPOINTER_TO_INT(wdtp));
				cmp_for_each_path = NULL;
				reterr = FLUFFY_ERROR_G_HASHTABLE_LOOKUP;
				break;
			}
			/* Remove fluffy records of the returned entry */
			if (!g_tree_remove(ctxinfop->path_tree,
			    (const char *)thiswd->path)) {
				PRINT_STDERR("Couldnot remove %s " \
				    "from the tree\n", thiswd->path);
			}
			thiswd = NULL;

			/*
			 * Now that this entry has been removed, continue to
			 * lookup for descendants. No more dungeons in the
			 * eastwing, move along to find the other dungeons.
			 */
		}
	} while(0);
	pthread_cleanup_pop(1);		/* Unlock mutex */
	wdtp = NULL;
	cmp_for_each_path = NULL;

	return reterr;
}

/*
 * Function:	fluffy_process_inotify_queue
 *
 * Each event is processed serially to guarantee event order. If the users
 * event callback function stalls longer, the possibility of a queue overflow
 * is higher. Upon the callback function's return, handle the event with
 * appopriate event action for internal functionality that is transparent to
 * the user.
 *
 * args:
 * 	- int:	fluffy context handle
 * 	- struct epoll_event: structure pointer of the event info from epoll
 * return:
 * 	- int:	0 when successful, error value otherwise to terminate context
 */
static int
fluffy_process_inotify_queue(int fluffy_handle, struct epoll_event *evlist)
{
	ssize_t nrbytes;
	int reterr = 0, qoverflow;
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}


	if (evlist->data.fd != ctxinfop->inotify_fd) {
		PRINT_STDERR("Incorrect file descriptor\n", "");
		return FLUFFY_ERROR_INVALID_ARG; // TODO: Is it correct here? (neold2022)
	}

	if ((evlist->events & EPOLLERR) || (evlist->events & EPOLLHUP)) {
		reterr = fluffy_cleanup_context_info_records(fluffy_handle);
		if (reterr) {
			return reterr;
		}
		return 0;
	}
	
	if (!(evlist->events & EPOLLIN)) {
		/* Shouldn't be happening */
		return 0;
	}

	char *iebuf;	/* inotify events buffer */
	iebuf = calloc(NR_INOTIFY_EVENTS,
			sizeof(struct inotify_event) + NAME_MAX + 1);
	if (iebuf == NULL) {
		reterr = FLUFFY_ERROR_NO_MEMORY;
		perror("calloc in fluffy_process_inotify_queue(), FILE: fluffy.c");
		return  reterr;
	}

	/* Get the inotify event */
	nrbytes = read(evlist->data.fd, iebuf,
			NR_INOTIFY_EVENTS *
			(sizeof(struct inotify_event) +
			 NAME_MAX + 1));
	if (nrbytes == -1) {
		reterr = FLUFFY_ERROR_READ_INOTIFY_FD;
		perror("read in fluffy_process_inotify_queue(), FILE: fluffy.c");
		free(iebuf);
		return  reterr;
	}
	if (nrbytes == 0) {
		free(iebuf);
		return FLUFFY_ERROR_READ_INOTIFY_FD; // No event? (neold2022)
	}

	/*
	 * inotify event pointer to traverse the events in the buffer, maximum
	 * of NR_INOTIFY_EVENTS events.
	 */
	struct inotify_event *ievent = NULL;
	char *p = NULL;
	for (p = iebuf; p < iebuf + nrbytes; ) {
		ievent = (struct inotify_event *) p;
		/* Prepare the pointer for the next event processing */
		p += sizeof(struct inotify_event) + ievent->len;

		/* Get the associated info of this inotify watch descriptor */
		struct fluffy_wd_info *wdinfop = NULL;
		wdinfop = (struct fluffy_wd_info *)g_hash_table_lookup(
				ctxinfop->wd_table,
				GINT_TO_POINTER(ievent->wd));
		/*
		 * If the event is a IN_Q_OVERFLOW, there will be no associated
		 * watch descriptor. The lookup will fail, so rule out this
		 * case.
		 */
		if (wdinfop == NULL &&
		    !(ievent->mask & IN_Q_OVERFLOW)) {
			PRINT_STDERR("Could not lookup wd %d\n", \
					ievent->wd);
			continue;
		}

		/*
		 * Even if it's a IN_Q_OVERFLOW event, handoff regardless. The
		 * fluffy_handoff_event will take care of it. Do not have to
		 * worry about wdinfop being NULL.
		 */
		if((reterr = fluffy_handoff_event(fluffy_handle,
		    ievent, wdinfop)) != 0) {
		    	free(iebuf);
			return reterr; /* A non-zero return terminates context */
		}

		/*
		 * Event has been reported to the client, now handle it to suit
		 * our functionalities.
		 */

		/* Handle queue overflow */
		if (ievent->mask & IN_Q_OVERFLOW) {
			PRINT_STDERR("Queue overflow, " \
				"reinitiating all watches!\n", "");
			if ((qoverflow = fluffy_handle_qoverflow(fluffy_handle)) != 0) {
				free(iebuf);
				return qoverflow;
			}
			// TODO: set reterr? (neold2022)
			break;
		}

		/*
		 * When a directory is newly created within a watched path,
		 * watch it recursively.
		 * When a directory is moved in to a watched path, watch it
		 * recursively.
		 * Whehn a directory is moved out off a watched path,
		 * remove the watches on it recursively; ignore.
		 *
		 * Self moves are handled indirectly but appropriately.
		 */
		if (((ievent->mask & IN_MOVED_TO) &&
		    (ievent->mask & IN_ISDIR)) ||
		    ((ievent->mask & IN_CREATE) &&
		    (ievent->mask & IN_ISDIR))) {
			reterr = fluffy_handle_addition(fluffy_handle,
					ievent, wdinfop);
			if (reterr) {
				free(iebuf);
				return reterr;
			}
		} else if ((ievent->mask & IN_MOVED_FROM) &&
		    (ievent->mask & IN_ISDIR)) {
			reterr = fluffy_handle_moved_from(fluffy_handle,
					ievent, wdinfop);
			if (reterr) {
				free(iebuf);
				return reterr;
			}

		}

		/*
		 * If a root path itself moves, remove the watches set on it
		 * recursively. This is only for the root path self moves,
		 * descendant path self moves are handled indirectly.
		 */
		if ((ievent->mask & IN_MOVE_SELF)) {
			reterr = fluffy_is_root_path(fluffy_handle,
					wdinfop->path);
			if (reterr == 0) {
				reterr = fluffy_handle_removal(
						fluffy_handle,
						wdinfop->path);
				if (reterr) {
					free(iebuf);
					return reterr;
				}
			}
		}

		if (ievent->mask & IN_IGNORED) {
			reterr = fluffy_handle_ignored(fluffy_handle,
					ievent, wdinfop);
			if (reterr) {
				free(iebuf);
				return reterr;
			}
		}


	}
	free(iebuf);
	iebuf = NULL;
	return  0;
}


/*
 * Function:	fluffy_thread_cleanup_unlock
 *
 * This is a thread cleanup handler passed to pthread_cleanup_push(), which
 * is popped on pthread_cancel() and pthread_exit(). As a coding
 * convenience for unlocking mutexes, this will be popped manually before
 * returning normally from the caller function.
 *
 * args:
 * 	void *mutex - A void pointer which carries pthread_mutex_t type
 *
 * return:
 * 	void
 */
static void
fluffy_thread_cleanup_unlock(void *mutex)
{
	int m = -1;
	m = pthread_mutex_unlock((pthread_mutex_t *)mutex);
	if (m != 0) {
		/* nothing? */
	}
}


/*
 * Function:	fluffy_setup_track
 *
 * Setup fluffy_track for the first time.
 *
 * args:
 * 	void
 * return:
 * 	int - 0 if successful, error value otherwise.
 */
static int
fluffy_setup_track()
{
	int ret = 0;
	int m = -1;
	/* Lock global fluffy_track static struct */
	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	do {
		/* Return if fluffy_track has already been setup */
		if (fluffy_track.is_init != 0) {
			ret = 0;
			break;
		}

		/*
		 * Initialize a table that holds context info of all fluffy
		 * instances.
		 */
		fluffy_track.context_table = g_hash_table_new_full(
						g_direct_hash,
						g_direct_equal,
						NULL,
						(GDestroyNotify)
						free_context_table_info_g);
		if (fluffy_track.context_table == NULL) {
			ret = FLUFFY_ERROR_G_HASH_TABLE;
			break;
		}

		fluffy_track.idx = 1;		/* Initialize fluffy_handle */
		fluffy_track.is_init = 1;	/* Mark setup success */
	} while(0);

	pthread_cleanup_pop(1);		/* Unlock mutex */
	return ret;
}


/*
 * Function:	fluffy_is_track_setup
 *
 * Check if the global fluffy_track has been initialized and setup.
 *
 * args:
 * 	void
 * return:
 * 	int - 0 if already setup, -1 otherwise
 */
static int
fluffy_is_track_setup()
{
	if (fluffy_track.is_init == 0) {
		return -1;
	}
	if (fluffy_track.context_table == NULL) {
		return -1;
	}

	return 0;
}


static struct fluffy_context_info *
fluffy_get_context_info(int fluffy_handle)
{
	struct fluffy_context_info *ctxinfop = NULL;
	ctxinfop = (struct fluffy_context_info *)g_hash_table_lookup(
			fluffy_track.context_table,
			GINT_TO_POINTER(fluffy_handle));

	return ctxinfop;
}


/*
 * Function:	fluffy_ret
 *
 * Allocate fluffy_context_info and return a fluffy_handle.
 *
 * args:
 * 	void
 * return:
 * 	int - fluffy_handle > 0 if successful, error value otherwise
 */
static int
fluffy_ref()
{
	int ret = 0;
	int m = -1;

	if (fluffy_is_track_setup()) {
		return -FLUFFY_ERROR_TRACK_NOT_INIT;
	}

	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		return -FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	do {
		/* Allocate fluffy_context_info */
		struct fluffy_context_info *ctxinfop = NULL;
		ctxinfop = fluffy_context_info_new();
		if (ctxinfop == NULL) {
			break;
		}

		ret = fluffy_track.idx;	/* Get fluffy_handle */
		ctxinfop->handle = ret;	/* Assign fluffy_handle */

		/* Insert fluffy_context_info for the received fluffy_handle */
		g_hash_table_replace(
		    fluffy_track.context_table,
		    GINT_TO_POINTER(ret),
		    ctxinfop);

		(fluffy_track.idx)++;	/* Increment handle for the next ref */
		(fluffy_track.nref)++;	/* Increment ref count */
	} while(0);

	pthread_cleanup_pop(1);	/* Unlock mutex */
	return ret;		/* Return fluffy handle */
}

/*
 * Function:	fluffy_unref
 *
 * Remove the context entry and decrement the ref count
 *
 * args:
 * 	- int: fluffy context handle
 * return:
 * 	- int: 0 when successful, error value otherwise
 */
static int
fluffy_unref(int fluffy_handle)
{
	int reterr = 0;

	if (fluffy_is_track_setup()) {
		return FLUFFY_ERROR_TRACK_NOT_INIT;
	}

	/* Cannot remove something that may not exist */
	if (fluffy_track.nref < 1) {
		return FLUFFY_ERROR_TRACK_EMPTY;
	}

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	int m = -1;
	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		return FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	do {
		/* Destroy the associated mutex variable */
		if(pthread_mutex_destroy(&ctxinfop->mutex)) {
			/* do nothing */
		}
		if (!g_hash_table_remove(fluffy_track.context_table,
		    GINT_TO_POINTER(fluffy_handle))) {
			reterr = FLUFFY_ERROR_INVALID_ARG; // TODO: Is it correct here? (neold2022)
			break;
		}

		(fluffy_track.nref)--;
	} while(0);
	pthread_cleanup_pop(1);		/* Unlock mutex */

	return reterr;
}

/*
 * Function:	fluffy_start_context_thread
 *
 * This is called from fluffy_init to start a new thread which listens to
 * events from inotify and then handles it.
 *
 * args:
 * 	- void *: fluffy handle carrying integer value
 * return:
 * 	- void
 */
static void *
fluffy_start_context_thread(void *flhandle)
{
	int fluffy_handle = 0;
	fluffy_handle = *(int *)flhandle;	/* extract int value */
	free(flhandle);
	int reterr = 0;
	int m = -1;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		pthread_exit((void *)FLUFFY_ERROR_CTXINFOP);
	}

	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		pthread_exit((void *)FLUFFY_ERROR_MUTEX_LOCK_FAILED);
	}

	/* Assign the context's thread ID; used for cancellations & joins */
	ctxinfop->tid = pthread_self();

	m = pthread_mutex_unlock(&ctxinfop->mutex);
	if (m != 0) {
		pthread_exit((void *)FLUFFY_ERROR_MUTEX_UNLOCK_FAILED);
	}

	pthread_cleanup_push(fluffy_destroy_context,
	    (void *)&fluffy_handle);

	/* Listen for events untill terminattion */
	while (1) {
		struct epoll_event *evlist = NULL;
		evlist = calloc(NR_EPOLL_EVENTS, sizeof(struct epoll_event));
		if (evlist == NULL) {
			perror("calloc in fluffy_start_context_thread(), FILE: fluffy.c");
			pthread_exit((void *)FLUFFY_ERROR_NO_MEMORY);
		}

		int nready = 0;
		/* Listen for inotify events; blocks. */
		nready = epoll_wait(ctxinfop->epoll_fd,
				evlist,
				NR_EPOLL_EVENTS,
				-1);
		if (nready == -1) {
			if (errno == -1) {
				continue;
			} else {
				perror("epoll_wait in fluffy_start_context_thread(), FILE: fluffy.c");
				pthread_exit((void *)FLUFFY_ERROR_EPOLL_WAIT);
			}
		}

		int j;
		/* Iterate through event queue and process each event */
		for (j = 0; j < nready; j++) {
			if (evlist[j].data.fd == ctxinfop->inotify_fd) {
				/*
				 * Serially processed so as to guarantee
				 * event ordering.
				 */
				reterr = fluffy_process_inotify_queue(
						fluffy_handle,
						&evlist[j]);
				if (reterr) {
					pthread_exit((void *)((size_t)reterr));
				}
			}
		}
		free(evlist);
		evlist = NULL;
	}
	pthread_cleanup_pop(1);
	return (void *)0;
}

/*
 * fluffy.h contains this function description
 */
int
fluffy_init(int (*user_event_fn) (const struct fluffy_event_info *eventinfo,
    void *user_data), void *user_data)
{
	int m = -1;
	int reterr = 0;

	if (fluffy_setup_track()) {
		return FLUFFY_ERROR_TRACK_NOT_INIT;
	}

	int flhandle = 0;
	/* Obtain a handle */
	flhandle = fluffy_ref();	/* Create a reference context */
	if (flhandle < 1) {
		return flhandle;
	}

	/* Get the fluffy_context_info of this handle */
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(flhandle);
	if (ctxinfop == NULL) {
		return -FLUFFY_ERROR_CTXINFOP;
	}

	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	/* Assign the user provided function pointer */
	ctxinfop->user_event_fn = user_event_fn;
	ctxinfop->user_data = user_data;

	m = pthread_mutex_unlock(&ctxinfop->mutex);
	if (m != 0) {
		return -FLUFFY_ERROR_MUTEX_UNLOCK_FAILED;
	}

	reterr = fluffy_setup_context(flhandle);
	if (reterr) {
		return -reterr; // fluffy_setup_context returns a positive error code.
	}

	/* Freed by fluffy_destroy_context */
	int *flh = calloc(1, sizeof(int));
	*flh = flhandle;

	pthread_t tid;
	/* Start the fluffy context thread */
	reterr = pthread_create(&tid,
			NULL,
			fluffy_start_context_thread,
			(void *)flh);
	if (reterr) {
		fluffy_destroy_context((void *)flh);
		free(flh);
		return -FLUFFY_ERROR_THREAD_CREATE;
	}

	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -FLUFFY_ERROR_MUTEX_LOCK_FAILED;
	}

	ctxinfop->tid = tid;

	m = pthread_mutex_unlock(&ctxinfop->mutex);
	if (m != 0) {
		return -FLUFFY_ERROR_MUTEX_UNLOCK_FAILED;
	}

	return flhandle;
}

/*
 * fluffy.h contains this function description
 */
int
fluffy_wait_until_done(int fluffy_handle)
{
	int m = 0;
	void *ret;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	/* Block until the context thread terminates */
	m = pthread_join(ctxinfop->tid, &ret);
	if (m != 0) {
		return FLUFFY_ERROR_THREAD_JOIN;
	}

	if (ret == PTHREAD_CANCELED) {		/* Deliberate cancell */
		return 0;
	} else if ((long)ret == 0) {
		return 0;
	} else {
		return (long)ret;
	}
}

/*
 * fluffy.h contains this function description
 */
int
fluffy_no_wait(int fluffy_handle)
{
	int m = 0;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return FLUFFY_ERROR_CTXINFOP;
	}

	/* Deatch the context thread, can't be joined anymore  */
	m = pthread_detach(ctxinfop->tid);
	if (m != 0) {
		return FLUFFY_ERROR_THREAD_DETACH;
	}

	return 0;
}

/*
 * fluffy.h contains this function description
 */
int
fluffy_add_watch_path(int fluffy_handle, const char *pathtoadd)
{
	int reterr = 0;

	reterr = fluffy_add_watch(fluffy_handle,
			pathtoadd,
			1,		/* Turn it to real path */
			1);		/* It's a root path */
	return reterr;
}


/*
 * fluffy.h contains this function description
 */
int
fluffy_remove_watch_path(int fluffy_handle, const char *pathtoremove)
{
	int reterr = 0;
	reterr = fluffy_handle_removal(fluffy_handle, pathtoremove);
	return reterr;
}


/*
 * fluffy.h contains this function description
 */
int
fluffy_destroy(int fluffy_handle)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int reterr = FLUFFY_ERROR_CTXINFOP;
	reterr = pthread_cancel(ctxinfop->tid);
	
	return reterr;
}


/*
 * fluffy.h contains this function description
 */
int
fluffy_print_event(const struct fluffy_event_info *eventinfo,
    void *user_data)
{
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
	if (eventinfo->event_mask & FLUFFY_ROOT_DELETE)
		fprintf(stdout, "ROOT_DELETE, ");
	if (eventinfo->event_mask & FLUFFY_IGNORED)
		fprintf(stdout, "IGNORED, ");
	if (eventinfo->event_mask & FLUFFY_ISDIR)
		fprintf(stdout, "ISDIR, ");
	if (eventinfo->event_mask & FLUFFY_MODIFY)
		fprintf(stdout, "MODIFY, ");
	if (eventinfo->event_mask & FLUFFY_ROOT_MOVE)
		fprintf(stdout, "ROOT_MOVE, ");
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
	if (eventinfo->event_mask & FLUFFY_ROOT_IGNORED)
		fprintf(stdout, "ROOT_IGNORED, ");
	if (eventinfo->event_mask & FLUFFY_WATCH_EMPTY)
		fprintf(stdout, "WATCH_EMPTY, ");
	fprintf(stdout, "\t");
	fprintf(stdout, "%s\n", eventinfo->path ? eventinfo->path : "");

	if (eventinfo->event_mask & FLUFFY_WATCH_EMPTY) {
		int m = 0;
		int fluffy_handle = *(int *)user_data;
		m = fluffy_destroy(fluffy_handle);
		return m;
	}

	return 0;
}


