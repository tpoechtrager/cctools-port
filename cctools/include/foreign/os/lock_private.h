#ifndef APPLE_PRIVATE_OS_LOCK_WRAPPER_H
#define APPLE_PRIVATE_OS_LOCK_WRAPPER_H

#ifdef __APPLE__
// Include the original macOS header on macOS
#include <os/lock_private.h>
#else
// Include the pthread header on Unix-like systems (Linux and BSD)
#include <pthread.h>

// Define equivalent types and functions for Unix-like systems
typedef pthread_mutex_t os_lock_ref;
#define os_lock_lock(lock) pthread_mutex_lock(lock)
#define os_lock_unlock(lock) pthread_mutex_unlock(lock)
#define os_lock_trylock(lock) pthread_mutex_trylock(lock)
#define os_lock_lock_inline(lock, counter) pthread_mutex_lock(lock)
#define os_lock_unlock_inline(lock, counter) pthread_mutex_unlock(lock)
#define os_lock_trylock_inline(lock, counter) pthread_mutex_trylock(lock)

// Define some constants if needed
#define OS_LOCK_INIT OS_LOCK_INITIALIZER

// Define equivalent types for os_lock_unfair_s
typedef pthread_mutex_t os_lock_unfair_s;
#define OS_LOCK_UNFAIR_INIT PTHREAD_MUTEX_INITIALIZER

#endif // __APPLE__

#endif // APPLE_PRIVATE_OS_LOCK_WRAPPER_H
