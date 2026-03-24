#ifndef RETRO_OS_H
#define RETRO_OS_H

#include "../apps/include/libc.h"
#include "../apps/include/stdio.h"
#include <stddef.h>
#include <stdint.h>
#include "../apps/include/types.h"
#include "../apps/include/syscall.h"

#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif

static inline int clock_gettime(int clk_id, struct timespec *tp) {
    return syscall_clock_gettime((clockid_t)clk_id, tp);
}

#define QueryPerformanceFrequency(x)
#define QueryPerformanceCounter(x)

#define _SC_NPROCESSORS_ONLN 1
static inline long sysconf(int name) {
    if (name == _SC_NPROCESSORS_ONLN) return 1;
    return -1;
}

#ifndef ENOBUFS
#define ENOBUFS 105
#endif
#ifndef ENOMSG
#define ENOMSG 42
#endif


// POSIX-like replacements for edge264

// Dummy pthread (since we'll run single-threaded)
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_cond_t;

inline int pthread_mutex_init(pthread_mutex_t *m, void *a) { return 0; }
inline int pthread_mutex_destroy(pthread_mutex_t *m) { return 0; }
inline int pthread_mutex_lock(pthread_mutex_t *m) { return 0; }
inline int pthread_mutex_unlock(pthread_mutex_t *m) { return 0; }
inline int pthread_cond_init(pthread_cond_t *c, void *a) { return 0; }
inline int pthread_cond_destroy(pthread_cond_t *c) { return 0; }
inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
  return 0;
}
inline int pthread_cond_signal(pthread_cond_t *c) { return 0; }
inline int pthread_cond_broadcast(pthread_cond_t *c) { return 0; }
inline int pthread_create(pthread_t *t, void *a, void *(*f)(void *),
                          void *arg) {
  return -1;
}
inline int pthread_cancel(pthread_t t) { return 0; }

// Memory - already in libc.h

// Misc
#undef assert
#define assert(x)

// atomic stubs for single threaded
#ifndef __ATOMIC_RELAXED
#define __atomic_sub_fetch(ptr, val, mem) (*(ptr) -= (val))
#define __atomic_store_n(ptr, val, mem) (*(ptr) = (val))
#define __ATOMIC_RELAXED 0
#define __ATOMIC_SEQ_CST 1
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#define __ATOMIC_ACQ_REL 4
#endif

// missing from our libc
#define ENODATA 61
#define EBADMSG 74
#define ENOTSUP 95

#endif
