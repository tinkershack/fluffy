#ifndef HUMBLE_FLUFFY_H
#define HUMBLE_FLUFFY_H

#include <stdint.h>
#include <sys/inotify.h>

/* The following are legal, implemented events that client can watch for */
#define FLUFFY_ACCESS		IN_ACCESS	/* File was accessed */
#define FLUFFY_MODIFY		IN_MODIFY	/* File was modified */
#define FLUFFY_ATTRIB		IN_ATTRIB	/* Metadata changed */
#define FLUFFY_CLOSE_WRITE	IN_CLOSE_WRITE	/* Writtable file was closed */
#define FLUFFY_CLOSE_NOWRITE	IN_CLOSE_NOWRITE /* Unwrittable file closed */
#define FLUFFY_OPEN		IN_OPEN		/* File was opened */
#define FLUFFY_MOVED_FROM	IN_MOVED_FROM	/* File was moved from X */
#define FLUFFY_MOVED_TO		IN_MOVED_TO	/* File was moved to Y */
#define FLUFFY_CREATE		IN_CREATE	/* File was created */
#define FLUFFY_DELETE		IN_DELETE	/* File was deleted */
#define FLUFFY_ROOT_DELETE	IN_DELETE_SELF	/* Root file was deleted */
#define FLUFFY_ROOT_MOVE	IN_MOVE_SELF	/* Root file was moved */

/* All the above events */
#define FLUFFY_ALL_EVENTS	IN_ALL_EVENTS

/* Helper events */
#define FLUFFY_CLOSE	(FLUFFY_CLOSE_WRITE | FLUFFY_CLOSE_NOWRITE) /* close */
#define FLUFFY_MOVE	(FLUFFY_MOVED_FROM | FLUFFY_MOVED_TO) /* moves */
#define FLUFFY_ISDIR	 IN_ISDIR	/* Event occurred against dir  */

/* The following are legal events. They are sent as needed to any watch */
#define FLUFFY_UNMOUNT		IN_UNMOUNT	/* Backing fs was unmounted */
#define FLUFFY_Q_OVERFLOW	IN_Q_OVERFLOW	/* Event queue overflowed */
#define FLUFFY_IGNORED		IN_IGNORED	/* File not watched anymore */ 
#define FLUFFY_ROOT_IGNORED	0x00010000	/* Root file was ignored */
#define FLUFFY_WATCH_EMPTY	0x00020000	/* All watches removed */

/* Error codes */
#define FLUFFY_ERROR_INVALID_ARG		1
#define FLUFFY_ERROR_OPEN_QUEUE_FAILED		2	/* errno is set */
#define FLUFFY_ERROR_WRITE_QUEUE_FAILED		3	/* errno is set */
#define FLUFFY_ERROR_CLOSE_QUEUE_FAILED		4	/* errno is set */
#define FLUFFY_ERROR_NO_MEMORY			5
#define FLUFFY_ERROR_MUTEX_LOCK_FAILED		6
#define FLUFFY_ERROR_INOTIFY_ADD_WATCH		7	/* errno is set */
#define FLUFFY_ERROR_REAL_PATH_RESOLVE		8	/* errno is set */
#define FLUFFY_ERROR_NFTW_WALKING		9	/* errno is set */
#define FLUFFY_ERROR_CLOSE_INOTIFY_FD		10	/* errno is set */
#define FLUFFY_ERROR_INIT_INOTIFY_FD		11	/* errno is set */
#define FLUFFY_ERROR_EPOLL_CTL			12	/* errno is set */
#define FLUFFY_ERROR_TRACK_NOT_INIT		13
#define FLUFFY_ERROR_EPOLL_CREATE		14	/* errno is set */
#define FLUFFY_ERROR_G_HASHTABLE_LOOKUP		15	/* internal error? */
#define FLUFFY_ERROR_RM_INOTIFY_WATCH		16	/* errno is set */
#define FLUFFY_ERROR_READ_INOTIFY_FD		17	/* errno is set */
#define FLUFFY_ERROR_TRACK_EMPTY		18
#define FLUFFY_ERROR_MUTEX_UNLOCK_FAILED	19
#define FLUFFY_ERROR_THREAD_CREATE		20	/* errno is set */
#define FLUFFY_ERROR_THREAD_JOIN		21	/* According to OpenGroup, the only reason is that system detected a deadlock */
#define FLUFFY_ERROR_THREAD_DETACH		22	/* According to OpenGroup, the only reason is that a invalid argument is provided */
#define FLUFFY_ERROR_EPOLL_WAIT			13	/* errno is set */

#define FLUFFY_ERROR_GENERIC			-1

#define FLUFFY_ERROR_CTXINFOP		FLUFFY_ERROR_G_HASHTABLE_LOOKUP // Not sure if it is the correct error code (neold2022)
#define FLUFFY_ERROR_G_HASH_TABLE	FLUFFY_ERROR_NO_MEMORY // Not sure if it is the correct error code (neold2022)
#define FLUFFY_ERROR_G_TREE		FLUFFY_ERROR_NO_MEMORY // Not sure if it is the correct error code (neold2022)

		


struct fluffy_event_info {
	/*
	 * Any of the above FLUFFY_* macros are applicable. More than one event
	 * is possible on a watch path, in which case, the values are ORed.
	 *
	 * The checks must be made like,
	 * 	if (event_mask & FLUFFY_ACCESS)		// RIGHT!
	 * rather than
	 * 	if (event_mask == FLUFFY_ACCESS)	// WRONG!
	 */
	uint32_t event_mask;

	/* Path where event occured. Absolute path. */
	char *path;
};


/*
 * Function:	fluffy_init
 *
 * Initiates fluffy and returns a context handle. The returned context handle
 * is a reference that's valid until the context is destroyed by the client or
 * Fluffy terminates it for some reason.
 *
 * Multiple paths can be watched on a single context. Each call to
 * fluffy_init() spawns a new context and returns its handle.  Each context is
 * run in a separate thread, a fluffy_wait_until_done() call may be required
 * at some point to join the context.
 *
 * The user_event_fn() pointer passed as the first argument is defined by the
 * user and this function will be called by Fluffy for every event on the
 * context; sort of a callback. user_event_fn() receives the first argument
 * *eventinfo from Fluffy along with user provided data as the second argument.
 * Fluffy doesn't touch whatever *user_data is holding, stores the pointer and
 * returns it. It's just for the convenience of not having to maintain global
 * structures/variables so that user_data can be accessed from within
 * user_event_fn() scope. user_event_fn() must return 0 for Fluffy to continue
 * processing the next event. Any other integer value returned will cause
 * Fluffy to destroy the associated context; A non zero return is equivalent to
 * calling fluffy_destroy(). There's a helper function definition from Fluffy
 * which comes handy for testing or for just printing the events on the
 * standard output - fluffy_print_event().
 *
 * args:
 * 	- int (*user_event_fn)( const struct fluffy_event_info *eventinfo,
 * 		void *user_data): explained above
 * 	- void *user_data: A pointer that's passed to user_event_fn on callback
 * return:
 * 	- int:	0 to continue with the next event processing,
 * 		any other integer(preferably -1) to destroy the context
 */
 // TODO: fluffy_init actually returns flhandle (a positive integer) when successful (see the implementation). Edit the function description? (neold2022)
extern int fluffy_init(int (*user_event_fn) (
    const struct fluffy_event_info *eventinfo,
    void *user_data), void *user_data);

/*
 * Function:	fluffy_add_watch_path
 *
 * Watch the path and its descendants recursively. If the provided path is not
 * being watched already, this path will be considered as a root path.
 *
 * args:
 * 	- const char *:	a path to watch recursively
 * return:
 * 	- int:		0 on success, error value otherwise
 */
extern int fluffy_add_watch_path(int fluffy_handle,
    const char *pathtoadd);

/*
 * Function:	fluffy_remove_watch_path
 *
 * Remove the watch on the path and its descendants recursively.
 *
 * args:
 * 	- const char *:	remove watches of the path recursively
 * return:
 * 	- int:		0 on success, error value otherwise
 */
extern int fluffy_remove_watch_path(int fluffy_handle,
    const char *pathtoremove);

/*
 * Function:	fluffy_wait_until_done
 *
 * Each context returned by fluffy_init() runs as a separate thread. If the
 * caller thread or main thread exits before the context thread terminates,
 * the context thread becomes a zombie thread and continues to hog resources.
 *
 * A call to fluffy_wait_until_done() blocks(waits) until the associated
 * context is destroyed or terminated.
 *
 * NOTE: fluffy_wait_until_done() and fluffy_no_wait() are mutually exclusive.
 *
 * args:
 * 	- int:	fluffy context handle
 * return:
 * 	- int:	0 on successful context exit, error value otherwise
 */
extern int fluffy_wait_until_done(int fluffy_handle);

/*
 * Function:	fluffy_no_wait
 *
 * Each context returned by fluffy_init() runs as a separate thread. If the
 * caller thread or main thread exits before the context thread terminates,
 * the context thread becomes a zombie thread and continues to hog resources.
 *
 * A call to fluffy_no_wait() detaches the associated context thread, ensuring
 * that the resources are freed when the thread dies/exits;
 * fluffy_wait_until_done() can't be used on that context.
 *
 * NOTE: fluffy_wait_until_done() and fluffy_no_wait() are mutually exclusive.
 *
 * args:
 * 	- int:	fluffy context handle
 * return:
 * 	- int:	0 on successful detach, error value otherwise
 */
extern int fluffy_no_wait(int fluffy_handle);

/*
 * Function:	fluffy_destroy
 *
 * Frees up resources and terminates the Fluffy context.
 *
 * args:
 * 	- int:	fluffy context handle
 * return:
 * 	- int:	0 on successful context exit, error value otherwise
 */
extern int fluffy_destroy(int fluffy_handle);


/* Helper functions */

/*
 * Function:	fluffy_reinitiate_context
 *
 * On a queue overflow, FLUFFY_Q_OVERFLOW event, fluffy inherently reinitiates
 * the context. While reinitiation, all watches of the context is removed and
 * then set from scratch. Fluffy reinitiates automatically only on a queue
 * overflow.
 *
 * This function is exposed to the client and may be called when a reinitiation
 * is required for any other situation/use case. It's a quite expensive call,
 * avoid unless it's absolutely necessary.
 *
 * args:
 * 	- int:	The context handle received from fluffy_init()
 * return:
 * 	- int:	0 on successful reinitiation, error value otherwise
 */
extern int fluffy_reinitiate_context(int fluffy_handle);

/*
 * Function:	fluffy_reinitiate_all_contexts
 *
 * On a queue overflow, FLUFFY_Q_OVERFLOW event, fluffy inherently reinitiates
 * the context. While reinitiation, all watches of the context is removed and
 * then set from scratch. Fluffy reinitiates a context automatically only on a
 * queue overflow event.
 *
 * This function is exposed to the client and may be called when a reinitiation
 * of all contexts is required. It's a very expensive call, avoid unless it's
 * absolutely necessary.
 *
 * args:
 *	- void
 * return:
 * 	- int:	0 on successful reinitiation, error value otherwise
 */
extern int fluffy_reinitiate_all_contexts();

/*
 * Function:	fluffy_set_max_queued_events
 *
 * The native inotify kernel implementation sends a IN_Q_OVERFLOW event when
 * the queue overflows. The queue overflows when the limit set in
 * /proc/sys/fs/inotify/max_queued_events is reached. man inotify for more
 * info on the limits.
 *
 * This function is a helper to set/update this limit. This limit holds until
 * the next restart. Note that the function doesn't validate the input
 * argument, it simply writes the value to the file.
 *
 * Consider calling fluffy_reinitiate_context() or
 * fluffy_reinitate_all_contexts() for the changes to be picked up because the
 * kernel library sets this only at the time of initiation.
 *
 * args:
 * 	- const char *:	a character string representing numerical value.
 * return:
 * 	- int:		0 on successful update, error value otherwise
 */
extern int fluffy_set_max_queued_events(const char *max_size);

/*
 * Function:	fluffy_set_max_user_instances
 *
 * This specifies an upper limit on the number of inotify instances that can
 * be created per real user ID. Initiation fails when the limit set in
 * /proc/sys/fs/inotify/max_user_instances is reached. man inotify for more
 * info on the limits. Fluffy initiation fails when inotify initiation fails.
 *
 * This function is a helper to set/update this limit. This limit holds until
 * the next restart. Note that the function doesn't validate the input
 * argument, it simply writes the value to the file.
 *
 * args:
 * 	- const char *:	a character string representing numerical value.
 * return:
 * 	- int:		0 on successful update, error value otherwise
 */
extern int fluffy_set_max_user_instances(const char *max_size);

/*
 * Function:	fluffy_set_max_user_watches
 *
 * This specifies an upper limit on the number of inotify watches that can
 * be created per real user ID. Initiation fails when the limit set in
 * /proc/sys/fs/inotify/max_user_watches is reached. man inotify for more
 * info on the limits. Fluffy initiation fails when inotify initiation fails.
 *
 * This function is a helper to set/update this limit. This limit holds until
 * the next restart. Note that the function doesn't validate the input
 * argument, it simply writes the value to the file.
 *
 * args:
 * 	- const char *:	a character string representing numerical value.
 * return:
 * 	- int:		0 on successful update, error value otherwise
 */
extern int fluffy_set_max_user_watches(const char *max_size);

/*
 * Function:	fluffy_print_event
 *
 * A helper function to print the event path and event action to the standard
 * output. This function must be passed as an argument only to the fluffy_init()
 * call, not to be called separately. Fluffy calls the print function on every
 * event as long as the watch list is not empty. Fluffy context is destroyed
 * when the watch list becomes empty.
 *
 * args:
 * 	- const struct fluffy_event_info *:	This will be provided by fluffy.
 * 						It carries event path & action.
 * 	- void *:	This will be provided by fluffy with context handle.
 * return:
 * 	- int:	0 to continue with the next event processing,
 * 		any other integer(preferably -1) to destroy the context
 */
extern int fluffy_print_event(const struct fluffy_event_info *eventinfo,
    void *user_data);

#endif
