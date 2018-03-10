In the spirit of true freedom, Fluffy is [Unlicensed][]. Free as in 
'do what ever pleases you' sort of freedom and free beer as well! Fluffy 
believes that a piece of software is free(freedom?) only when it has 
severed ties with licenses, copyrights, intellectual property rights 
and other crap of similar kind. Attribution is a kind gesture, Fluffy 
appreciates it but doesn't fret if you fail to say "[good dog][]!"


Fluffy reports on-disk filesystem events faithfully and celebrates 
craftsmanship.

# Fluffy - A 'free' watchdog for Linux on-disk filesystems [WIP]

__Fluffy__ is a convenient front-end tool, __libfluffy__ is what you 
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
   matter of porting; though it may be painful it's less painful than an 
   all-in-one model. Besides, a platform specific library helps keep the 
   code base clean and lean. It's simpler. If you are thinking POSIX, 
   you haven't yet realized why many of the popular operating systems 
   aren't POSIX/SUS compliant.

 - Reports events faithfully.  
   The are (very)popular libraries/tools built that do an half arsed job 
   at reporting the events - wrong event path, erroneous event handling, 
   erroneous event reporting, oblivious to filesystem 
   structure/hierarchy changes(dir moves especially), can't add watches 
   on the fly without re-executing(reinitiating) the program(library), 
   foul coding practices, poor implementation.
   After considering various aspects of the process, building from 
   scratch seemed better than fixing the broken ones. 

 - Add/remove watch paths on the fly.

 - A fully functional library that utilizes the native inotify subsystem 
   properly. This means, unlike few tools, the events are not limited to 
   just file 'modifications'. Every possible event action like 'open', 
   'access', 'close', 'no write', 'delete', 'move' is caught. User has 
   the flexibility(control?) to discard/process select events. 

 - Freedom. NO GPL bullshit.  
   Fluffy has three heads. It likes to flip the middle one to licensing.


_libfluffy_ is a better choice if you are planning to use it in 
production. [fluffy.h][] has the interface description and callables.  
[example.c][] provides a sample.


### Try it?

_This section will be updated soon_.

If you like getting your hands dirty, then: fork/clone, read 
[fluffy.h][] & [example.c][], run `make`, `./fluffy /watch/this 
watch/this/too`, perform some action on the watch directory(`ls` 
perhaps?). Dependency - glib-2.0

The library interface is quite simple to use. Modify the example file 
and run `make` again.

### TODO: Fluffy is a WIP

There's still quite a few more to be done but these are the primary ones

 - Fluffy frontend _WIP_
 - Documentation _WIP_
 - proper error reporting
 - valgrind
 - other helper functions
 - test cases
 - replace glib hashtables with a native implementation?


[fluffy.h]:     fluffy.h
[example.c]:    example.c

[Unlicensed]:   https://unlicense.org/
[good dog]:     http://harrypotter.wikia.com/wiki/Fluffy
[inotify]:      http://man7.org/linux/man-pages/man7/inotify.7.html
[Michael Kerrisk]:      https://lwn.net/Articles/605128/
