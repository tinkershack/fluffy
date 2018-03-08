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

/* Helper events */
#define FLUFFY_CLOSE	(FLUFFY_CLOSE_WRITE | FLUFFY_CLOSE_NOWRITE) /* close */
#define FLUFFY_MOVE	(FLUFFY_MOVED_FROM | FLUFFY_MOVED_TO) /* moves */
#define FLUFFY_ISDIR	 IN_ISDIR	/* Event occurred against dir  */

/* The following are legal events.  They are sent as needed to any watch */
#define FLUFFY_UNMOUNT		IN_UNMOUNT	/* Backing fs was unmounted */
#define FLUFFY_Q_OVERFLOW	IN_Q_OVERFLOW	/* Event queue overflowed */
#define FLUFFY_IGNORED		IN_IGNORED	/* File not watched anymore */ 
#define FLUFFY_ROOT_IGNORED	0x00010000	/* Root file was ignored */
#define FLUFFY_WATCH_EMPTY	0x00020000	/* Root file was ignored */


struct fluffy_event_info {
	uint32_t event_mask;
	char *path;
};


extern int fluffy_init(int (*event_fn) (
    const struct fluffy_event_info *eventinfo,
    void *user_data), void *user_data);

extern int fluffy_add_watch_path(int fluffy_handle,
    const char *pathtoadd);

extern int fluffy_remove_watch_path(int fluffy_handle,
    const char *pathtoremove);

extern int fluffy_wait_until_done(int fluffy_handle);

extern int fluffy_destroy(int fluffy_handle);


/* Helper functions */

extern int fluffy_reinitiate_context(int fluffy_handle);

extern int fluffy_reinitiate_all_contexts();

extern int fluffy_set_max_queued_events(const char *max_size);

extern int fluffy_set_max_user_instances(const char *max_size);

extern int fluffy_set_max_user_watches(const char *max_size);

extern int fluffy_print_event(const struct fluffy_event_info *eventinfo,
    void *user_data);


#endif
