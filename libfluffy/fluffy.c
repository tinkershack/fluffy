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

int dir_tree_add_watch(const char *pathname, const struct stat *sbuf,
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

static int fluffy_handle_move(int fluffy_handle,
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


/* function definitions */

int
fluffy_set_max_queued_events(const char *maxvalp)
{
	int fd = -1;
	ssize_t wrbytes;

	if (maxvalp == NULL) {
		return -1;
	}

	fd = open("/proc/sys/fs/inotify/max_queued_events",
		O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd == -1) {
		return -1;
	}

	wrbytes = write(fd, maxvalp, strlen(maxvalp));
	if(wrbytes == -1 || (size_t)wrbytes != strlen(maxvalp)) {
		close(fd);
		return -1;
	}

	if (close(fd) != 0) {
		return -1;
	}

	/* Reinitialize all fluffy context to pick up the newly set value */
	/*
	if(fluffy_reinitiate_all_contexts()) {
		return -1;
	}
	*/

	return 0;
}


int
fluffy_set_max_user_instances(const char *maxvalp)
{
	int fd = -1;
	ssize_t wrbytes;

	if (maxvalp == NULL) {
		return -1;
	}

	fd = open("/proc/sys/fs/inotify/max_user_instances",
		O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd == -1) {
		return -1;
	}

	wrbytes = write(fd, maxvalp, strlen(maxvalp));
	if(wrbytes == -1 || (size_t)wrbytes != strlen(maxvalp)) {
		close(fd);
		return -1;
	}

	if (close(fd) != 0) {
		return -1;
	}

	return 0;
}


int
fluffy_set_max_user_watches(const char *maxvalp)
{
	int fd = -1;
	ssize_t wrbytes;

	if (maxvalp == NULL) {
		return -1;
	}

	fd = open("/proc/sys/fs/inotify/max_user_watches",
		O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd == -1) {
		return -1;
	}

	wrbytes = write(fd, maxvalp, strlen(maxvalp));
	if(wrbytes == -1 || (size_t)wrbytes != strlen(maxvalp)) {
		close(fd);
		return -1;
	}

	if (close(fd) != 0) {
		return -1;
	}

	return 0;
}


static struct fluffy_wd_info *
fluffy_wd_info_new()
{
	struct fluffy_wd_info *wdinfop;
	wdinfop = calloc(1, sizeof(struct fluffy_wd_info));
	if (wdinfop == NULL) {
		perror("calloc");
		return NULL;
	}
	wdinfop->wd	= 0;
	wdinfop->mask	= 0;
	wdinfop->path	= NULL;

	return wdinfop;
}


static struct fluffy_context_info *
fluffy_context_info_new()
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = calloc(1, sizeof(struct fluffy_context_info));
	if (ctxinfop == NULL) {
		perror("calloc");
		/*
		 * When OOM is not handled, it's a common practice to call
		 * exit() to bail out. We will return NULL instead of exit()
		 * to hand off the reponsibility to the caller.
		 */
		return NULL;
	}

	if (pthread_mutex_init(&ctxinfop->mutex, NULL)) {
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


static int
fluffy_handoff_event(int fluffy_handle, struct inotify_event *ie,
    struct fluffy_wd_info *wdinfop)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}
	if (ctxinfop->user_event_fn == NULL) {
		return 0;
	}

	uint32_t handoff_mask = ie->mask;
	
	int is_not_root = 0;

	char *eventpathp = NULL;
	if (!(ie->mask & IN_Q_OVERFLOW)) {
		is_not_root = fluffy_is_root_path(fluffy_handle,
				wdinfop->path);

		eventpathp = form_event_path(wdinfop->path,
				ie->len,
				ie->name);
		if (eventpathp == NULL) {
			return -1;
		}

		if ((is_not_root)		&&
		    ((ie->mask & IN_MOVE_SELF)	||
		    (ie->mask & IN_DELETE_SELF))) {
			free(eventpathp);
			return 0;
		}

		if ((is_not_root == 0)	&&
		    (ie->mask & IN_IGNORED)) {
			handoff_mask |= FLUFFY_ROOT_IGNORED;
			if(g_hash_table_size(ctxinfop->root_path_table) == 1) {
				handoff_mask |= FLUFFY_WATCH_EMPTY;
			}
		}

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

	struct fluffy_event_info *evtinfop;
	evtinfop = calloc(1, sizeof(struct fluffy_event_info));
	if (evtinfop == NULL) {
		pthread_exit((void *)-1);
	}
	evtinfop->event_mask = handoff_mask;
	evtinfop->path = eventpathp;

	int ret = 0;
	ret = (ctxinfop->user_event_fn)(evtinfop, (void *)ctxinfop->user_data);

	free(eventpathp);
	free(evtinfop);
	return ret;

}


int
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
		return -1;
	}

	int reterr = 0;
	int iwd = -1;
	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	if (ftwb->level != 0) {
		if (g_hash_table_contains(ctxinfop->root_path_table,
		    pathname)) {
			/*
			 * This had been a root watch path previously.
			 * Since it is a regular descendent path now, remove
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
			perror("inotify_add_watch");
			reterr = -1;
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
			reterr = -1;
			break;
		}

		wdinfop->wd = iwd;
		wdinfop->mask = INOTIFY_EVENT_FLAGS;
		wdinfop->path = strdup(pathname);
		if (wdinfop->path == NULL) {
			perror("strdup");
			reterr = -1;
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

	if (reterr) {
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
		pthread_exit((void *)-1);
	}

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(GPOINTER_TO_INT(fluffy_handle));
	if (ctxinfop == NULL) {
		pthread_exit((void *)-1);
	}

	int reterr = 0;
	reterr = fluffy_add_watch(GPOINTER_TO_INT(fluffy_handle),
			(char *)root_path,
			0,
			0);
	if (reterr) {
		pthread_exit((void *)-1);
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


static gint
search_tree_g(gpointer pathname, gpointer compare_path)
{
	char *cmp_for_each_path = (char *)compare_path;
	char *tocmp = NULL;
	size_t cmp_size = 0;
	tocmp = calloc(1, strlen(cmp_for_each_path) + 2);
	if (tocmp == NULL) {
		perror("calloc");
		pthread_exit((void *)-1);
	}

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


static char *
form_event_path(char *wdpath, uint32_t ilen, char *iname)
{
	char *currpath = NULL;
	currpath = calloc(1, (size_t) (strlen(wdpath) + 1 
				+ ((ilen) ?  ilen : 0) + 2));
	if (currpath == NULL) {
		perror("calloc");
		return NULL;
	}

	currpath = strncpy(currpath, wdpath, strlen(wdpath) + 1);
	if (ilen) {
		currpath = strncat(currpath, "/", 2);
		currpath = strncat(currpath, iname, strlen(iname) + 1);
	}
	return currpath;
}


static int
fluffy_add_watch(int fluffy_handle, const char *pathtoadd,
    int is_real_path_check, int is_root_path)
{
	char *addpath = NULL;
	int reterr = 0;

	if (is_real_path_check) {
		addpath = realpath(pathtoadd, NULL);
		if (addpath == NULL) {
			reterr = errno;
			perror("realpath");
			return reterr;
		}
	} else {
		addpath = strdup(pathtoadd);
		if (addpath == NULL) {
			reterr = errno;
			perror("strdup");
			return reterr;
		}
	}

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

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
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	do {
		fluffy_track.curr_ctxinfop = ctxinfop;

		if (nftw(addpath, dir_tree_add_watch, 30, FTW_FLAGS) == -1) {
			reterr = errno;
			perror("nftw");
			break;
		}
	} while(0);
	fluffy_track.curr_ctxinfop = NULL;
	pthread_cleanup_pop(1);		/* Unlock mutex */
	free(addpath);
	return reterr;
}


static int
fluffy_cleanup_context_info_records(int fluffy_handle)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int m = -1;
	int reterr = 0;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}
	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		if (close(ctxinfop->inotify_fd) == -1) {
			reterr = errno;
			perror("close");
			break;
		}
		ctxinfop->nwd = 0;
		
		g_hash_table_remove_all(ctxinfop->wd_table);
		g_hash_table_remove_all(ctxinfop->path_table);
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
		return -1;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
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
			ret = 1;
			break;
		}

		ctxinfop->path_table = g_hash_table_new_full(
					g_str_hash,
					g_str_equal,
					(GDestroyNotify)free,
					NULL);
		if (ctxinfop->path_table == NULL) {
			ret = 1;
			break;
		}

		ctxinfop->root_path_table = g_hash_table_new_full(
						g_str_hash,
						g_str_equal,
						(GDestroyNotify)free,
						NULL);
		if (ctxinfop->root_path_table == NULL) {
			ret = 1;
			break;
		}

		ctxinfop->path_tree = g_tree_new_full(
					(GCompareDataFunc)strcmp,
					NULL,
					(GDestroyNotify)free,
					NULL);
		if (ctxinfop->path_tree == NULL) {
			ret = 1;
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
		return -1;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		/* Initialize inotify, get its descriptor */
		ctxinfop->inotify_fd = inotify_init1(IN_CLOEXEC);
		if (ctxinfop->inotify_fd == -1) {
			ret = errno;
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
			ret = errno;
			break;
		}
	} while (0);

	pthread_cleanup_pop(1);		/* Unlock mutex */
	return ret;
}


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
			perror("close");
			/* best effort */
		}


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
		return -1;
	}

	if(g_hash_table_size(fluffy_track.context_table) > 0) {
		g_hash_table_foreach(fluffy_track.context_table,
		    (GHFunc) reinit_each_context_g, NULL);
	}

	return 0;
}


int
fluffy_reinitiate_context(int fluffy_handle)
{
	int reterr = 0;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	reterr = fluffy_cleanup_context_info_records(fluffy_handle);
	if (reterr) {
		return reterr;
	}

	reterr = fluffy_initiate_inotify(fluffy_handle);
	if (reterr) {
		return reterr;
	}

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
		return -1;
	}

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	ctxinfop->epoll_fd = epoll_create(3);
	if (ctxinfop->epoll_fd == -1) {
		ret = errno;
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


static int
fluffy_is_root_path(int fluffy_handle, char *path)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int m = -1;
	int reterr = 0;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		if (!g_hash_table_contains(ctxinfop->root_path_table, path)) {
			reterr = 1;
			break;
		}
	} while(0);
	pthread_cleanup_pop(1);		/* Unlock mutex */
	return reterr;
}


static int
fluffy_handle_qoverflow(int fluffy_handle)
{
	int reterr = 0;
	reterr = fluffy_reinitiate_context(fluffy_handle);
	if (reterr) {
		PRINT_STDERR("Reinitiation failed!\n", "");
		return reterr;
	}
	return 0;
}


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
		return -1;
	}

	reterr = fluffy_add_watch(fluffy_handle, currpath, 0, 0);
	if (reterr) {
		PRINT_STDERR("%s\n", strerror(reterr));
	}

	free(currpath);
	currpath = NULL;
	return reterr;
}


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

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
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


static int
fluffy_handle_move(int fluffy_handle, struct inotify_event *ievent,
    struct fluffy_wd_info *wdinfop)
{
	int reterr = 0;
	char *movepath = NULL;
	movepath = form_event_path(wdinfop->path,
				ievent->len, ievent->name);
	if (movepath == NULL) {
		return -1;
	}

	reterr = fluffy_handle_removal(fluffy_handle, movepath);
	if (reterr) {
		/* nothing? */
	}

	free(movepath);
	movepath = NULL;

	return  reterr;
}


static int
fluffy_handle_removal(int fluffy_handle, char *removethis)
{
	char *cmp_for_each_path = removethis;
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int reterr = 0;
	struct fluffy_wd_info *toremwd;
	gpointer wdtp = NULL;

	int m = -1;
	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &ctxinfop->mutex);

	do {
		toremwd = (struct fluffy_wd_info *)g_hash_table_lookup(
				ctxinfop->path_table, cmp_for_each_path);
		if (toremwd == NULL) {
			PRINT_STDERR("Could not lookup path %s\n", \
			    cmp_for_each_path);
			cmp_for_each_path = NULL;
			reterr = -1;
			break;
		}

		if (!g_tree_remove(ctxinfop->path_tree,
		    (const char *)cmp_for_each_path)) {
			PRINT_STDERR("Couldnot remove %s from the tree\n", \
					cmp_for_each_path);
		}

		int iwdtmp = -1;
		iwdtmp = inotify_rm_watch(ctxinfop->inotify_fd, toremwd->wd);
		if (iwdtmp == -1) {
			reterr = errno;
			perror("inotify_rm_watch");
			cmp_for_each_path = NULL;
			break;
		}
		toremwd = NULL;

		while (1) {
			wdtp = g_tree_search(ctxinfop->path_tree,
					(GCompareFunc)search_tree_g,
					(gpointer)cmp_for_each_path);
			if (wdtp == NULL) {
				break;
			}

			int iwd = -1;
			iwd = inotify_rm_watch(ctxinfop->inotify_fd,
					GPOINTER_TO_INT(wdtp));
			if (iwd == -1) {
				reterr = errno;
				perror("inotify_rm_watch");
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
				reterr = -1;
				break;
			}
			if (!g_tree_remove(ctxinfop->path_tree,
			    (const char *)thiswd->path)) {
				PRINT_STDERR("Couldnot remove %s " \
				    "from the tree\n", thiswd->path);
			}
			thiswd = NULL;
		}
	} while(0);
	pthread_cleanup_pop(1);		/* Unlock mutex */
	wdtp = NULL;
	cmp_for_each_path = NULL;

	return reterr;
}


static int
fluffy_process_inotify_queue(int fluffy_handle, struct epoll_event *evlist)
{
	ssize_t nrbytes;
	int reterr = 0;
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}


	if (evlist->data.fd != ctxinfop->inotify_fd) {
		PRINT_STDERR("Incorrect file descriptor\n", "");
		return 1;
	}

	if ((evlist->events & EPOLLERR) || (evlist->events & EPOLLHUP)) {
		reterr = fluffy_cleanup_context_info_records(fluffy_handle);
		if (reterr) {
			return reterr;
		}
		return 0;

	} else if (evlist->events & EPOLLIN) {
		char *iebuf;
		iebuf = calloc(NR_INOTIFY_EVENTS,
				sizeof(struct inotify_event) + NAME_MAX + 1);
		if (iebuf == NULL) {
			reterr = errno;
			perror("calloc");
			return  reterr;
		}

		nrbytes = read(evlist->data.fd, iebuf,
				NR_INOTIFY_EVENTS *
				(sizeof(struct inotify_event) +
				 NAME_MAX + 1));
		if (nrbytes == -1) {
			reterr = errno;
			perror("read");
			return  reterr;
		}
		if (nrbytes == 0) {
			return -1;
		}

		struct inotify_event *ievent = NULL;
		char *p = NULL;
		for (p = iebuf; p < iebuf + nrbytes; ) {
			ievent = (struct inotify_event *) p;
			p += sizeof(struct inotify_event) + ievent->len;

			struct fluffy_wd_info *wdinfop = NULL;
			wdinfop = (struct fluffy_wd_info *)g_hash_table_lookup(
					ctxinfop->wd_table,
					GINT_TO_POINTER(ievent->wd));
			if (wdinfop == NULL &&
			    !(ievent->mask & IN_Q_OVERFLOW)) {
				PRINT_STDERR("Could not lookup wd %d\n", \
						ievent->wd);
				continue;
			}

			if(fluffy_handoff_event(fluffy_handle,
			    ievent, wdinfop) != 0) {
				return -1;
			}

			if (ievent->mask & IN_Q_OVERFLOW) {
				PRINT_STDERR("Queue overflow, " \
					"reinitiating all watches!\n", "");
				if (fluffy_handle_qoverflow(fluffy_handle)) {
					return  -1;
				}
				break;
			}

			if (((ievent->mask & IN_MOVED_TO) &&
			    (ievent->mask & IN_ISDIR)) ||
			    ((ievent->mask & IN_CREATE) &&
			    (ievent->mask & IN_ISDIR))) {
				reterr = fluffy_handle_addition(fluffy_handle,
						ievent, wdinfop);
				if (reterr) {
					return reterr;
				}

			} else if ((ievent->mask & IN_MOVED_FROM) &&
			    (ievent->mask & IN_ISDIR)) {
				reterr = fluffy_handle_move(fluffy_handle,
						ievent, wdinfop);
				if (reterr) {
					return reterr;
				}

			}

			if ((ievent->mask & IN_MOVE_SELF)) {
				reterr = fluffy_is_root_path(fluffy_handle,
						wdinfop->path);
				if (reterr == 0) {
					reterr = fluffy_handle_removal(
							fluffy_handle,
							wdinfop->path);
					if (reterr) {
						return reterr;
					}
				}
			}

			if (ievent->mask & IN_IGNORED) {
				reterr = fluffy_handle_ignored(fluffy_handle,
						ievent, wdinfop);
				if (reterr) {
					return reterr;
				}
			}


		}
		free(iebuf);
		iebuf = NULL;
	}
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
	/* Lock global fluffy_track static struct */
	int m = -1;
	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	/*
	 * NOTE:
	 *
	 * pthread_cleanup_pop() must be in the same lexical scope as that
	 * of pthread_cleanup_push(). This means that the convenience of
	 * returning from anywhere, regardless of block scope, is lost. Bummer.
	 *
	 * Employ do-while(0) to break out and reach the deferred return.
	 */

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
			ret = -1;
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
		return -1;
	}

	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		return -1;
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
	return ret;
}


static int
fluffy_unref(int fluffy_handle)
{
	int reterr = 0;

	if (fluffy_is_track_setup()) {
		return -1;
	}

	if (fluffy_track.nref < 1) {
		return -1;
	}

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int m = -1;
	m = pthread_mutex_lock(&fluffy_track.mutex);
	if (m != 0) {
		return -1;
	}

	pthread_cleanup_push(fluffy_thread_cleanup_unlock,
	    &fluffy_track.mutex);

	do {
		if(pthread_mutex_destroy(&ctxinfop->mutex)) {
			/* do nothing */
		}
		if (!g_hash_table_remove(fluffy_track.context_table,
		    GINT_TO_POINTER(fluffy_handle))) {
			reterr = -1;
			break;
		}

		(fluffy_track.nref)--;
	} while(0);
	pthread_cleanup_pop(1);		/* Unlock mutex */

	return reterr;
}


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
		pthread_exit((void *)-1);
	}

	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		pthread_exit((void *)-1);
	}

	ctxinfop->tid = pthread_self();

	m = pthread_mutex_unlock(&ctxinfop->mutex);
	if (m != 0) {
		pthread_exit((void *)-1);
	}

	pthread_cleanup_push(fluffy_destroy_context,
	    (void *)&fluffy_handle);

	while (1) {
		struct epoll_event *evlist = NULL;
		evlist = calloc(NR_EPOLL_EVENTS, sizeof(struct epoll_event));
		if (evlist == NULL) {
			perror("calloc");
			pthread_exit((void *)-1);
		}

		int nready = 0;
		nready = epoll_wait(ctxinfop->epoll_fd,
				evlist,
				NR_EPOLL_EVENTS,
				-1);
		if (nready == -1) {
			if (errno == -1) {
				continue;
			} else {
				perror("epoll_wait");
				pthread_exit((void *)-1);
			}
		}

		int j;
		for (j = 0; j < nready; j++) {
			if (evlist[j].data.fd == ctxinfop->inotify_fd) {
				reterr = fluffy_process_inotify_queue(
						fluffy_handle,
						&evlist[j]);
				if (reterr) {
					pthread_exit((void *)-1);
				}
			}
		}
		free(evlist);
		evlist = NULL;
	}
	pthread_cleanup_pop(1);
	return (void *)0;
}


int
fluffy_init(int (*user_event_fn) (const struct fluffy_event_info *eventinfo,
    void *user_data), void *user_data)
{
	int m = -1;
	int reterr = 0;

	if (fluffy_setup_track()) {
		return -1;
	}

	int flhandle = 0;
	/* Obtain a handle */
	flhandle = fluffy_ref();
	if (flhandle < 1) {
		return -1;
	}

	/* Get the fluffy_context_info of this handle */
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(flhandle);
	if (ctxinfop == NULL) {
		return -1;
	}

	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	/* Assign the user provided function pointer */
	ctxinfop->user_event_fn = user_event_fn;
	ctxinfop->user_data = user_data;

	m = pthread_mutex_unlock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	reterr = fluffy_setup_context(flhandle);
	if (reterr) {
		return -1;
	}

	/* Freed by fluffy_destroy_context */
	int *flh = calloc(1, sizeof(int));
	*flh = flhandle;

	pthread_t tid;
	reterr = pthread_create(&tid,
			NULL,
			fluffy_start_context_thread,
			(void *)flh);
	if (reterr) {
		fluffy_destroy_context((void *)flh);
		free(flh);
		return -1;
	}

	m = pthread_mutex_lock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	ctxinfop->tid = tid;

	m = pthread_mutex_unlock(&ctxinfop->mutex);
	if (m != 0) {
		return -1;
	}

	return flhandle;
}


int
fluffy_wait_until_done(int fluffy_handle)
{
	int m = 0;
	void *ret;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	/* Block until the context thread terminates */
	m = pthread_join(ctxinfop->tid, &ret);
	if (m != 0) {
		return -1;
	}

	if (ret == PTHREAD_CANCELED) {		/* Deliberate cancell */
		return 0;
	} else if ((long)ret == 0) {
		return 0;
	} else {
		return -1;
	}
}


int
fluffy_no_wait(int fluffy_handle)
{
	int m = 0;

	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	/* Deatch the context thread, can't be joined anymore  */
	m = pthread_detach(ctxinfop->tid);
	if (m != 0) {
		return -1;
	}

	return 0;
}


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


int
fluffy_remove_watch_path(int fluffy_handle, const char *pathtoremove)
{
	int reterr = 0;
	reterr = fluffy_handle_removal(fluffy_handle, pathtoremove);
	return reterr;
}


int
fluffy_destroy(int fluffy_handle)
{
	struct fluffy_context_info *ctxinfop;
	ctxinfop = fluffy_get_context_info(fluffy_handle);
	if (ctxinfop == NULL) {
		return -1;
	}

	int reterr = -1;
	reterr = pthread_cancel(ctxinfop->tid);
	
	return reterr;
}


int
fluffy_print_event(const struct fluffy_event_info *eventinfo,
    void *user_data)
{
	fprintf(stdout, "\n");
	fprintf(stdout, "event:\t");
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
	fprintf(stdout, "\n");
	fprintf(stdout, "path:\t%s\n", eventinfo->path ? eventinfo->path : "");

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


