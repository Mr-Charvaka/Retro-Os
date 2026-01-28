#ifndef PTHREAD_H
#define PTHREAD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Thread Types
// ============================================================================
typedef uint32_t pthread_t;

// Thread attributes
typedef struct {
  uint32_t stacksize;
  void *stackaddr;
  int detachstate;
  int schedpolicy;
  int schedpriority;
  int scope;
  int inheritsched;
} pthread_attr_t;

// ============================================================================
// Mutex Types
// ============================================================================
typedef struct {
  volatile int locked;
  volatile pthread_t owner;
  int type;
  int recursive_count;
} pthread_mutex_t;

typedef struct {
  int type;
  int pshared;
  int protocol;
  int prioceiling;
  int robust;
} pthread_mutexattr_t;

// Mutex types
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL

// Static mutex initializer
#define PTHREAD_MUTEX_INITIALIZER {0, 0, PTHREAD_MUTEX_DEFAULT, 0}

// ============================================================================
// Condition Variable Types
// ============================================================================
typedef struct {
  volatile int waiters;
  volatile int signal_count;
  pthread_mutex_t *mutex;
} pthread_cond_t;

typedef struct {
  int pshared;
  int clock;
} pthread_condattr_t;

// Static condition initializer
#define PTHREAD_COND_INITIALIZER {0, 0, 0}

// ============================================================================
// Read-Write Lock Types
// ============================================================================
typedef struct {
  volatile int readers;
  volatile int writers;
  volatile int write_waiters;
  volatile pthread_t write_owner;
  pthread_mutex_t lock;
} pthread_rwlock_t;

typedef struct {
  int pshared;
} pthread_rwlockattr_t;

#define PTHREAD_RWLOCK_INITIALIZER {0, 0, 0, 0, PTHREAD_MUTEX_INITIALIZER}

// ============================================================================
// Barrier Types
// ============================================================================
typedef struct {
  unsigned int count;
  unsigned int current;
  volatile int _phase;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} pthread_barrier_t;

typedef struct {
  int pshared;
} pthread_barrierattr_t;

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

// ============================================================================
// Spinlock Types
// ============================================================================
typedef volatile int pthread_spinlock_t;

// ============================================================================
// Thread-specific Data (TSD)
// ============================================================================
typedef uint32_t pthread_key_t;

// ============================================================================
// Once Types
// ============================================================================
typedef struct {
  volatile int done;
  pthread_mutex_t lock;
} pthread_once_t;

#define PTHREAD_ONCE_INIT {0, PTHREAD_MUTEX_INITIALIZER}

// ============================================================================
// Detach State Constants
// ============================================================================
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

// ============================================================================
// Scope Constants
// ============================================================================
#define PTHREAD_SCOPE_SYSTEM 0
#define PTHREAD_SCOPE_PROCESS 1

// ============================================================================
// Inherit Scheduling Constants
// ============================================================================
#define PTHREAD_INHERIT_SCHED 0
#define PTHREAD_EXPLICIT_SCHED 1

// ============================================================================
// Scheduling Policy Constants
// ============================================================================
#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2

// ============================================================================
// Cancel State/Type Constants
// ============================================================================
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1

#define PTHREAD_CANCELED ((void *)-1)

// ============================================================================
// Thread Functions
// ============================================================================
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
void pthread_exit(void *retval);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_cancel(pthread_t thread);
int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel(void);

// ============================================================================
// Thread Attribute Functions
// ============================================================================
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr);
int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr);
int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy);
int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy);

// ============================================================================
// Mutex Functions
// ============================================================================
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const void *abstime);

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);

// ============================================================================
// Condition Variable Functions
// ============================================================================
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const void *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);

// ============================================================================
// Read-Write Lock Functions
// ============================================================================
int pthread_rwlock_init(pthread_rwlock_t *rwlock,
                        const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

// ============================================================================
// Barrier Functions
// ============================================================================
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);

// ============================================================================
// Spinlock Functions
// ============================================================================
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

// ============================================================================
// Thread-Specific Data Functions
// ============================================================================
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

// ============================================================================
// Once Functions
// ============================================================================
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

// ============================================================================
// Yield
// ============================================================================
int pthread_yield(void);
int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif // PTHREAD_H
