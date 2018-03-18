In the spirit of true freedom, Fluffy is [Unlicensed][]. Free as in 
*do what ever pleases you* sort of freedom and free beer as well! Fluffy 
believes that a piece of software is free(freedom?) only when it has 
severed ties with licenses, copyrights, intellectual property rights 
and other crap of similar kind. Attribution is a kind gesture, Fluffy 
appreciates it but doesn't fret if you fail to say "[good dog][]!"


*Fluffy reports on-disk filesystem events faithfully and celebrates 
craftsmanship.*

# Fluffy - A 'free' watchdog for Linux on-disk filesystems [WIP]

__Fluffy__ is a convenient CLI tool, __libfluffy__ is what you 
will find under the hood. libfluffy uses the inotify kernel subsystem.

There are challenges in using the native [inotify][] library 
effectively; [Michael Kerrisk][] provides a lucid description. Fluffy's 
magic cuts through all the challenges cleanly.

### Why Fluffy?

There are other implementations of filesystem watchers already, why 
Fluffy?

 - Linux specific; Fluffy is not cross platform, but loyal to Linux.  
   Linux has its tricks, \*BSD has its, so does Solaris. It only makes
   sense to have separate implementations for each of it to fully 
   utilize the exclusive features which aren't available across all
   platforms. If the features aren't very diverse, then it's only a 
   matter of porting; though it may be painful it's relatively less 
   painful than an all-in-one model. Besides, a platform specific 
   library helps keep the code base clean and lean. It's simpler. If you 
   are thinking POSIX, please think again why many of the popular 
   operating systems aren't POSIX/SUS compliant.

 - Reports events faithfully.  
   There are popular libraries/tools built already that do a poor job 
   at reporting the events - wrong event path, erroneous event handling, 
   erroneous event reporting, oblivious to filesystem 
   structure/hierarchy changes(dir moves especially), can't add watches 
   on the fly without re-executing(reinitiating) the program(library).
   After considering various aspects of the process, building from 
   scratch seemed better than fixing the broken ones. 

 - Add/remove watch paths on the fly.

 - A fully functional library that utilizes the native inotify kernel 
   subsystem properly. This means, unlike few tools, the events are not 
   limited to just file 'modifications'. Every possible event action 
   like 'open', 'access', 'close', 'no write', 'delete', 'move' is 
   caught. User has the flexibility(control?) to discard/process select 
   events. 

 - Freedom. NO GPL shit.  
   Fluffy has three heads. It likes to flip the middle one to licensing.


_libfluffy_ is a better choice if you are planning to use it in 
production. [fluffy.h][] has the interface description and callables.  
[example.c][] provides a sample.


### Don't mind getting your hands dirty?

```
# Fork and clone this repo
# Ensure glib-2.0 has been installed
# cd to fluffy dir
make            # `make clean` to cleanup

# If you see errors, please consider creating an issue here.
# No erros? Cool, let's proceed.
make install    # `make uninstall` to uninstall

# The framework, along with the library, has been installed.

# Run help
fluffyctl --help-all

fluffy --help

# Try
# Execute fluffy first
fluffy

# Open a new terminal to execute fluffyctl
# Let's watch /var/log & /tmp
fluffyctl -w /var/log -w /tmp

# You must see some action at the terminal where fluffy is run.
# Nothing yet?
ls -l /var/log  # Still nothing? We may have a problem!

# Let's ignore /tmp, not interested watching anymore.
fluffyctl -I /tmp

# More? Let's quit fluffy so that you can start over & explore.
fluffy exit

# If you are interested only in the library, you can choose to compile 
# just the library.
cd ./libfluffy
make

# There will now be a libfluffy.a archive file(static library), you can
# link against it in your projects.

# example.c shows a simple usage of the library
make example
./fluffy-example

# Modify example.c to play around. Run `make example` when you wish
# to compile the modified example.c for testing.

```

#### fluffy usage
```
root@six-k:~# fluffy -h
Usage:
  fluffy [OPTION...] [exit]

Help Options:
  -h, --help                     Show help options

Application Options:
  -O, --outfile=./out.fluffy     File to print output [default:stdout]
  -E, --errfile=./err.fluffy     File to print errors [default:stderr]

```

#### fluffyctl usage

```
root@six-k:~# fluffyctl --help-all
Usage:
  fluffyctl [OPTION...] ["/path/to/hogwarts/kitchen"]

'fluffyctl' controls 'fluffy' program.
fluffy must be invoked before adding/removing watches.  By default all 
file system events are watched and reported. --help-events will show the 
options to modify this behaviour

Help Options:
  -h, --help                                      Show help options
  --help-all                                      Show all help options
  --help-events                                   File system events to report

When an option or more from 'events' group is passed, only those events 
will be reported. When a new invocation of fluffyctl sets any 'events' 
option, previously set events choice is discarded; overrides.

  --all                                           Watch all possible events [default]
  --access                                        Watch file access
  --modify                                        Watch file modifications
  --attrib                                        Watch metadata change
  --close-write                                   Watch closing of file opened for writing
  --close-nowrite                                 Watch closing of file/dir not opened for writing
  --open                                          Watch opening of file/dir
  --moved-from                                    Watch renames/moves: reports old file/dir name
  --moved-to                                      Watch renames/moves: reports new file/dir name
  --create                                        Watch creation of files/dirs
  --delete                                        Watch deletion of files/dirs
  --root-delete                                   Watch root path deletions
  --root-move                                     Watch root path moves/renames
  --isdir                                         Watch for events that occur against a directory
  --unmount                                       Watch for unmount of the backing filesystem ['isdir' not raised]
  --queue-overflow                                Watch for event queue overflows ['isdir' not raised]
  --ignored                                       Watch for paths ignored by Fluffy(not watched) ['isdir' not raised]
  --root-ignored                                  Watch for root paths ignored(not watched) ['isdir' not raised]
  --watch-empty                                   Watch whether all Fluffy watches are removed ['isdir' not raised]

Application Options:
  -O, --outfile=./out.fluffy                      File to print output [default:stdout]
  -E, --errfile=./err.fluffy                      File to print errors [default:stderr]
  -w, --watch=/grimmauld/place/12                 Paths to watch recursively. Repeat flag for multiple paths.
  -W, --watch-glob                                Paths to watch recursively: supports wildcards. Any non-option argument passed will be considered as paths. [/hogwarts/*/towers]
  -i, --ignore=/knockturn/alley/borgin/brukes     Paths to ignore recursively. Repeat flag for multiple paths.
  -I, --ignore-glob                               Paths to ignore recursively: supports wildcards. Any non-option argument passed will be considered as paths. [/hogwarts/*/dungeons]
  -U, --max-user-watches=524288                   Upper limit on the number of watches per uid [fluffy defaults 524288]
  -Q, --max-queued-events=524288                  Upper limit on the number of events [fluffy defaults 524288]
  -z, --reinit                                    Reinitiate watch on all root paths. [Avoid unless necessary]

```

#### fluffy event log snippet

```
event:  CLOSE_NOWRITE, ISDIR, 
path:   /tmp/.Test-unix

event:  ACCESS, ISDIR, 
path:   /tmp/systemd-private-6f74e3b317d0471b8546d2a83261b721-cups.service-3wkDKu

event:  ACCESS, ISDIR, 
path:   /tmp/systemd-private-6f74e3b317d0471b8546d2a83261b721-cups.service-3wkDKu/tmp

event:  MODIFY, 
path:   /var/log/messages

event:  MODIFY, 
path:   /var/log/syslog

event:  OPEN, ISDIR, 
path:   /var/log

event:  ACCESS, ISDIR, 
path:   /var/log

event:  CLOSE_NOWRITE, ISDIR, 
path:   /var/log

event:  OPEN, ISDIR, 
path:   /var/log

event:  ACCESS, ISDIR, 
path:   /var/log

event:  CLOSE_NOWRITE, ISDIR, 
path:   /var/log

event:  CREATE, 
path:   /tmp/sh-thd-1809946088
```

### TODO: Fluffy is a WIP

There's still quite a few more to be done but these are the primary ones

 - Documentation _WIP_
 - Fluffy frontend _WIP_
 - Proper error reporting. For now, most error returns have been set -1 
   deliberately without any error string or value.
 - Valgrind
 - Test cases
 - Doxygen?

 - Fix makefiles, it's poorly formed.
 - Other helper functions
   - Destroy all contexts
   - Get the total number of watches on a context
   - Get the list of root watch paths
 - Option to terminate context thread when watch list becomes empty
 - Ability to modify callback function pointer
 - Replace glib hashtables with a native implementation?


[fluffy.h]:     libfluffy/fluffy.h
[example.c]:    libfluffy/example.c

[Unlicensed]:   https://unlicense.org/
[good dog]:     http://harrypotter.wikia.com/wiki/Fluffy
[inotify]:      http://man7.org/linux/man-pages/man7/inotify.7.html
[Michael Kerrisk]:      https://lwn.net/Articles/605128/
