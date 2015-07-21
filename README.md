# ThreadPool
A simple threadpool and associated threading classes using modern C++ features.
Implemented on top of C++11 threads, with enough WIN32 fallback to work in VS 2010.

To use just build ThreadPool.cpp in your project and #include "ThreadPool.h"

## Class Future
Essentially C++11's promise and future in a single class. I wrote my own because
I needed to support VS 2010.

TODO: offer STL promise/futures instead as an option.

## Class ThreadPool
Create one of these for parallel work. Default constructor tries to create one
thread per system core as reported by Win32 sysinfo or std::thread::hardware_concurrency().
If it can't determine the number of cores it defaults to 4. A second constructor takes an integer with the number of worker threads to create.

Queue work using async(). It is templated on the return type of the function passed,
returning a Future of that type. Invoking async<void> returns an empty Future which
can be waited on to determine when the work is complete.

In order to avoid blocking work a Future can be passed to await() rather than calling
wait() on the Future itself. This will result in other work being done until the future
has its result.

TODO: The internal work queue currently uses a mutex, which limits the performance
of the worker threads for every small work units. Replace with something faster.

## Class WorkQueue
This class provides the same interface, async() and await(), as ThreadPool, but it
executes a work item with processQueueUnit() is called. This is useful for running
work on the main thread or for making dedicated worker threads for a task.

## Class Semaphore
This class provides a wrapper around a WIN32 or POSIX semaphore. The constructor
takes the initial value of the semaphore. Use post() to increment, optionally with
an integer to increment multiple at once. Use wait() to decrement, waiting until
a value is available.

TODO: create STL implementation around std::mutex + std::condition_var.
